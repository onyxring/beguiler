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
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
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
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    emitOpenBlockCleanups(body);
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    if(body != nullptr) body->statements.push_back(&rs);
    return false;
}

void bglParser::rejectEscapingLambda(const expression* e, const string& where){
    string captured = capturedByLambdaIn(e);
    if(!captured.empty())
        parsingError(format("A lambda {0} runs after the variables it captures are gone, so it cannot "
            "capture them; this one captures '{1}'.", where, captured));
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
    token first = file.getToken();
    expression* retExpr = parseExpression(first, {token::endStatement}, func, body);
    allowVoidReturnExpr = savedAllowVoid;
    rejectEscapingLambda(retExpr, format("returned from '{0}'", currentFunc ? currentFunc->dName() : funcName));
    if(func != nullptr && func->returnType.name == "void"){
        // Allow `return <void-typed expr>;` as the C/I6 idiom shorthand. In loose-mode
        // contexts (`#bgl{}` islands and `.inf` precompiler mode), unresolved identifiers
        // resolve to `var`; treat `var` as compatible too so `return f()` ports verbatim
        // when `f` was declared in I6 and isn't visible to the Beguile resolver.
        string retType = retExpr ? retExpr->resolvedType : "";
        if(retType != "void" && retType != "var")
            parsingError(format("Cannot return a value from void routine '{0}'", funcName));
    }
    // A block-body lambda has no declared return type: its first `return` supplies it, and any later
    // one is checked against that.
    if(retExpr && currentFunc && currentFunc->isLambda && currentFunc->returnType.name.empty()
       && !retExpr->resolvedType.empty())
        currentFunc->returnType.name = literalBaseType(retExpr->resolvedType);
    else if(retExpr && currentFunc) checkReturnValue(retExpr, currentFunc->returnType.name, funcName);
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
    loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, whileStmt.body);
        while(processNextStatement(whileCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, whileCtx);
    }
    loopDepth--;
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
    if(!loopVarName.empty() && tt.is(token::assignment)){
        // `name = expr`: parse the value like any other expression, so calls, member access and
        // emitters in it are translated (the raw token text reached I6 as written).
        expression* initExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        initText = loopVarName + " = " + (initExpr ? initExpr->text() : "");
    } else {
        while(tt.isNot(token::endStatement)){
            if(!initText.empty()) initText += " ";
            initText += tt.value;
            tt = file.getToken();
        }
    }
    forStmt.initText = initText;
    forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
    vector<statement*> condSetup = pendingInjections;
    pendingInjections.clear();
    expression* incrExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
    vector<statement*> incrInjections = pendingInjections;
    pendingInjections.clear();
    string incrText = incrExpr ? incrExpr->text() : "";
    if(!incrInjections.empty()) forStmt.incrementText = "";
    else forStmt.incrementText = incrText;
    forStmt.body = new statementBlock();
    functionDef forCtx;
    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
    forCtx.body = forStmt.body;
    token next = file.getToken();
    if(!loopVarName.empty()) currentLoopVars.insert(loopVarName); loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, forStmt.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, forCtx);
    }
    // If ternary in increment: append injections + increment as last body statements
    if(!incrInjections.empty()){
        for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
        i6RawNode* incrStmt = new i6RawNode();
        incrStmt->text = incrText + ";";
        forStmt.body->statements.push_back(incrStmt);
    }
    loopDepth--; if(!loopVarName.empty()) currentLoopVars.erase(loopVarName);
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

    string arrName = format("_bglfia{0}", forInCounter++);
    variableDeclaration& tmpDecl = *(new variableDeclaration());
    tmpDecl.name = arrName;
    tmpDecl.type = languageService.getType("var");
    tmpDecl.isSynthetic = true;   // for-in array temp — compiler-generated, hidden in the debugger
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

    languageService.forInScratchInUse = true;   // gates the _BGL_FORIN_SCRATCH_CAP constant + scratchSupport block
    forInStatement& fi = *(new forInStatement());
    fi.src = stmtLoc;
    fi.elementVar = elemVarName;
    fi.arrayVar   = arrName;
    fi.counterVar = counterName;
    // Inline-list for-in always iterates the word-based scratch buffer
    // (_bglScratchStack), regardless of element type — char values are stored
    // and read as words there. So the word template is correct even for
    // `for(char c in {'a','b'})`; the byte template would misread the scratch.
    fi.isByteArray = false;
    fi.inlineElements = elements;
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
    currentLoopVars.insert(elemVarName); loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, fi.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, forCtx);
    }
    loopDepth--; currentLoopVars.erase(elemVarName);
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
    expression* arrExpr = parseExpression(file.getToken(), {token::parenClose, "to"}, func, body);
    if(arrExpr->terminator == "to"){
        expression* rangeEnd = parseExpression(file.getToken(), {token::parenClose}, func, body);
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
        currentLoopVars.insert(elemVarName); loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, forStmt.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processBracelessBody(next, forCtx);
        }
        loopDepth--; currentLoopVars.erase(elemVarName);
        if(body != nullptr) body->statements.push_back(&forStmt);
        return false;
    }

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
                arrElemType = (tn.size() > 6 && tn.substr(0,6) == "array<") ? tn.substr(6, tn.size()-7) : "var";
                arrName = arrExprText;
                break;
            }

    // A member word array reached by a path (`cfg.limits`, `self.limits`): iterated in place.
    ArrayReceiver memberSrc;
    if(arrExpr != nullptr && isWordArrayType(arrExpr->resolvedType) && arrExprText.find('(') == string::npos
       && arrExprText.find('.') != string::npos){
        string recv = arrExprText;
        memberSrc = arrayReceiver(recv, arrExprText, arrExpr->resolvedType, func, body);
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
        // Each child is an instance of the class that declares `children` (the world-tree root).
        function<classDef*(classDef*)> declaring = [&](classDef* c) -> classDef* {
            if(c == nullptr) return nullptr;
            for(typeMember* m : c->members) if(m->name == "children") return c;
            for(classDef* b : c->baseClasses) if(classDef* d = declaring(b)) return d;
            return nullptr;
        };
        classDef* decl = declaring(getDispatchClass(resolveIdentifierType(owner, func, body)));
        arrElemType = decl != nullptr ? decl->name : "_bglobject";
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
    bool isStringContainer = resolvesToString(arrExprText);

    fi.elementVar = elemVarName;
    fi.arrayVar   = arrName;
    fi.counterVar = counterName;
    if(memberSrc.isMember){
        fi.memberBase   = memberSrc.owner + ".&" + propertyI6Name(memberSrc.prop);
        fi.memberLength = memberArraySizeText(memberSrc, "length", func, body);
    }
    // A string is iterated through its class's own emitters — `getLength()` bounds the loop and
    // `operator[](i)` reads each char — expanded here with markers the emitter replaces by the
    // container's and the counter's emitted names. `<string>` provides both.
    fi.isStringForIn = isStringContainer;
    if(isStringContainer){
        classDef* strCls = languageService.findClass("string");
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
    currentLoopVars.insert(elemVarName); loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, fi.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, forCtx);
    }
    loopDepth--; currentLoopVars.erase(elemVarName);
    if(body != nullptr) body->statements.push_back(&fi);
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
                    parsingWarning("Loop variable '" + name + "' shadows global of the same name; '::" + name + "' reaches the global.");
                }
            if(currentClass != nullptr){
                for(typeMember* m : currentClass->members)
                    if(m->name == name)
                        parsingWarning("Loop variable '" + name + "' shadows a member of class '" + currentClass->name + "'.");
                // Walk base class hierarchy for inherited members (vars and functions) — warning only
                function<void(classDef*)> checkBases = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(m->name == name)
                            if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                                parsingWarning("Loop variable '" + name + "' shadows inherited member '" + name + "' from class '" + c->dName() + "'.");
                    for(classDef* base : c->baseClasses) checkBases(base);
                };
                for(classDef* base : currentClass->baseClasses) checkBases(base);
            }
            if(currentObject != nullptr)
                for(typeMember* m : currentObject->members)
                    if(m->name == name)
                        parsingWarning("Loop variable '" + name + "' shadows a member of object '" + currentObject->name + "'.");
        };
        checkShadow(nameTok.value);
        if(file.peekToken().is("in")){
            isForIn = true;
            elemVarName = nameTok.value;
            elemVarType = typeTok.value;
            bool alreadyDeclared = false;
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == elemVarName){
                            if(vd->type.name != elemVarType)
                                parsingError(format("Loop variable '{0}' redeclared with different type '{1}' (was '{2}')",
                                    elemVarName, elemVarType, vd->type.name));
                            alreadyDeclared = true;
                            break;
                        }
            if(!alreadyDeclared){
                variableDeclaration& elemDecl = *(new variableDeclaration());
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
                        if(vd->name == nameTok.value){
                            if(vd->type.name != typeTok.value)
                                parsingError(format("Loop variable '{0}' redeclared with different type '{1}' (was '{2}')",
                                    nameTok.value, typeTok.value, vd->type.name));
                            alreadyDeclared = true;
                            break;
                        }
            if(!alreadyDeclared){
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

    // Inline initializer list: for(int j in {1, 2, 3})
    if(file.peekToken(1).is(token::braceOpen)){
        return processForInLiteralList(elemVarName, elemVarType, stmtLoc, func, body);
    }

    return processForIn(elemVarName, elemVarType, stmtLoc, func, body);
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
    loopDepth++;
    size_t continuesBefore = parsedContinues.size();
    int bodyDepth = loopDepth;
    openCompileContext(eCompileContext::codeBlock, doStmt.body);
    while(processNextStatement(doCtx) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    loopDepth--;
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
    while(true){
        token tt = file.getToken();
        if(tt.is(token::braceClose)) break;
        switchCase& sc = *(new switchCase());
        if(tt.is("default")){
            file.getToken(":");
        } else {
            tt.assert("case", "Expected 'case' or 'default' inside switch.");
            auto parseCaseExpr = [&]() -> expression* {
                // Bias case-value resolution toward the condition's type so an ambiguous name
                // resolves the right way — e.g. `switch(action){ case Open: }` picks the `Open`
                // verb, not the same-named `open` attribute.
                string savedExpectedCase = currentExpectedType;
                if(!conditionType.empty()) currentExpectedType = conditionType;
                expression* val = parseExpression(file.getToken(), {":", ",", "to"}, func, body);
                currentExpectedType = savedExpectedCase;
                if(!conditionType.empty() && !val->resolvedType.empty()
                   && !isTypeCompatible(val->resolvedType, conditionType)
                   && val->resolvedType != "verb")
                    parsingError(format("Switch case type '{0}' does not match condition type '{1}'",
                                       val->resolvedType, conditionType));
                return val;
            };
            auto parseNextEntry = [&](expression*& lastExpr) {
                token peek = file.peekToken(1);
                if(peek.is(eTokenType::oper) && (peek.value==">"||peek.value==">="||peek.value=="<"||peek.value=="<=")){
                    token op = file.getToken();
                    expression* val = parseCaseExpr();
                    caseEntry e;
                    e.guardCondition = "_bgl_sw " + op.value + " " + val->text();
                    sc.entries.push_back(e);
                    swStmt.needsIfChain = true;
                    lastExpr = val;
                    return;
                }
                expression* val = parseCaseExpr();
                if(val->terminator == "to"){
                    expression* high = parseCaseExpr();
                    caseEntry e;
                    e.rangeLow = val;
                    e.rangeHigh = high;
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
        while(true){
            token peek = file.peekToken();
            if(peek.is(token::braceClose) || peek.is("case") || peek.is("default")) break;
            token st = file.getToken();
            processStatementDispatch(st, caseCtx);
        }
        closeCompileContext(eCompileContext::codeBlock);
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
        openCompileContext(eCompileContext::codeBlock, tcStmt.tryBody);
        while(processNextStatement(tryCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    }
    token catchTok = file.getToken();
    if(!catchTok.is("catch"))
        parsingError("Expected 'catch' after try block");
    file.getToken(token::parenOpen);
    token catchType = file.getToken(eTokenType::dataType);
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
    string varName = (string)nameTok;
    // A dotted path (`delete holder.pack`) names a member, not a variable. Consume the rest of
    // the path so the type comes from the MEMBER rather than the head of the path — otherwise
    // `delete holder.pack` reported that `holder` is not a pooled class, which it never is.
    while(file.peekToken().is(token::period)){
        file.getToken(token::period);
        token seg = file.getToken({eTokenType::identifier, eTokenType::dataType});
        varName += "." + (string)seg;
    }
    // Resolve the type — must be a pooled class.
    string varTypeName = varName.find('.') == string::npos
                       ? resolveIdentifierType(varName, func, body)
                       : resolvePathType(varName, func, body);
    if(varTypeName.empty())
        parsingError(format("'delete {0}': unknown variable", nameTok.originalValue.empty() ? varName : nameTok.originalValue));
    classDef* cls = languageService.classOf(varTypeName);
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
    node.src = stmtLoc;
    if(body != nullptr) body->statements.push_back(&node);
    return false;
}


// `++x;` / `--x;` — prefix increment or decrement as a whole statement.
bool bglParser::processPrefixIncDec(token op, StatementContext& sc){
    // A target reached through a call or subscript (`++f().n`, `++arr[i].n`): parse it as the path
    // statement it starts, which finishes this operation when it reaches the `;`.
    {
        int k = 2;
        while(file.peekToken(k).is(token::period) && file.peekToken(k + 1).is(eTokenType::identifier)) k += 2;
        if(file.peekToken(1).is(eTokenType::identifier)
           && (file.peekToken(k).is(token::parenOpen) || file.peekToken(k).is(token::bracketOpen))){
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
    while(file.peekToken(1).is(token::period) && file.peekToken(2).is(eTokenType::identifier)){
        file.getToken();
        varName.value += "." + file.getToken().value;
    }
    file.getToken(token::endStatement);
    return finishPrefixIncDec(op, varName, sc);
}

// `ClassName.member` naming a static member: the global it is stored in, or "" for any other path.
string bglParser::staticMemberGlobal(const string& path){
    size_t dot = path.rfind('.');
    if(dot == string::npos) return "";
    string ownerPath = path.substr(0, dot), propName = path.substr(dot + 1);
    classDef* cls = languageService.findClass(ownerPath);
    if(cls == nullptr && ownerPath.find('.') != string::npos)
        if(string t = resolveNamespacedType(ownerPath); !t.empty()) cls = languageService.findClass(t);
    if(cls == nullptr) return "";
    for(typeMember* m : cls->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->isStatic && vd->name == propName) return "_bgl_" + cls->name + "_" + propName;
    return "";
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
    if(isConstVariable(varName.value, func, body))
        parsingError(format("Cannot assign to const variable '{0}'", varName.value));
    // Try emitter lookup for "prefix++" / "prefix--" on the LHS type, falling back to the
    // plain "++" / "--" emitter if no prefix-specific override is defined.
    string lhsTypeName = resolveIdentifierType(varName.value, func, body);
    classDef* lhsClass = languageService.classOf(lhsTypeName);
    bool emitterFound = false;
    string prefixOpName = "prefix" + tok.value;  // e.g. "prefix++"
    auto tryEmitter = [&](const string& opName) -> bool {
        if(!lhsClass) return false;
        typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==opName && opFunc->isEmitter
                   && opFunc->params.empty() && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        });
        if(!m) return false;
        auto* opFunc = dynamic_cast<functionDef*>(m);
        auto* blk = dynamic_cast<i6Block*>(opFunc->body);
        emitterBindings ob; ob.self = lhs; ob.val = lhs;
        i6RawNode& node = *(new i6RawNode());
        node.text = expandEmitterBody(blk, ob) + ";";
        node.src = stmtLoc;
        node.cooked = true;   // names locals: renamed like any expression
        if(body != nullptr) body->statements.push_back(&node);
        return true;
    };
    if(tryEmitter(prefixOpName) || tryEmitter(tok.value)) emitterFound = true;
    if(!emitterFound){
        if(!lhsTypeName.empty() && lhsTypeName != "var")
            parsingError(format("No operator '{0}' defined on type '{1}'", tok.value, typeDisplayName(lhsTypeName)));
        i6RawNode& node = *(new i6RawNode());
        node.text = tok.value + lhs + ";";
        node.src = stmtLoc;
        node.cooked = true;   // names locals: renamed like any expression
        if(body != nullptr) body->statements.push_back(&node);
    }
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
    if(!tok.is(eTokenType::identifier) && !tokIsLiteral)
        parsingError(format("Unrecognized statement starting with token '{0}'", (string) tok));
    if(tok.is(eTokenType::identifier) && !tok.isDataType() && file.peekToken(1).is(eTokenType::identifier))
        unknownTypeError(tok);   // `T name` — only a type can lead a statement followed by a name

    //make sure the identifier is complete, including any member access paths (chain all dots and ?.)
    token symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    int optionalChainDepth = 0; // number of ?. guards opened
    while(symbol.is(token::period) || symbol.is("?.")) {
        if(symbol.is("?.")){
            // Optional chaining at statement level: emit if(nullTest){ as pre-injection, } as post-injection
            string pathSoFar = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
            if(pathSoFar.empty()) pathSoFar = tok.value;
            string pathType = resolveIdentifierType(tok.value, func, body);
            if(pathType.empty()) pathType = resolvePathType(tok.value, func, body);
            classDef* cls = !pathType.empty() ? languageService.classOf(pathType) : nullptr;
            functionDef* nullTestFn = nullptr;
            if(cls != nullptr)
                nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
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
    classDef* arrCls = languageService.classOf(arrType);
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
        callStmt.functionName = subscriptText + "." + (method->i6name.empty() ? memberName : method->i6name);
        callStmt.args = pal.args;
        callStmt.namedArgNames = namedArgNames;
        callStmt.interpSegmentsPerArg = interpSegs;
        if(method->isEmitter && !method->isPrePassStub)
            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                emitterBindings sb; sb.self = subscriptText; sb.val = subscriptText;
                sb.fn = method;
                for(expression* a : pal.args) sb.args.push_back(a->text());
                for(expression* a : pal.args) sb.argTypes.push_back(a->resolvedType);
                callStmt.emitterBody = expandEmitterBody(blk, sb);
            }
        file.getToken(token::endStatement);
        if(body != nullptr) body->statements.push_back(&callStmt);
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
bool bglParser::processChainedSubscriptWrite(const string& arrPath, expression* indexExpr, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string arrType = resolvePathType(arrPath, func, body);
    size_t dotPos  = arrPath.find('.');
    string elemType = (dotPos == string::npos)
        ? resolveArrayElementType(arrPath, func, body)
        : resolveArrayElementTypeDotted(arrPath.substr(0, dotPos), arrPath.substr(dotPos + 1), func, body);
    if(elemType.empty() && arrType == "bytearray") elemType = "char";
    classDef* arrCls = getDispatchClass(arrType);
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
        emitterBindings gb; gb.self = selfV; gb.val = arrPath; gb.prop = pv;
        gb.fn = getM; gb.args.push_back(indexExpr->text());
        readText = expandEmitterBody(gblk, gb);
    }
    string curType = elemType;   // type of grid[0] — an array<...>
    while(true){
        file.getToken(); // consume '['
        expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        string innerElem = curType == "bytearray" ? "char" : arrayInnerType(curType);
        classDef* curCls = getDispatchClass(curType);
        token after = file.peekToken();
        if(after.is(token::bracketOpen)){
            // Intermediate read: extend the pointer expression.
            functionDef* gm = (curCls != nullptr && !innerElem.empty())
                            ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/false) : nullptr;
            i6Block* blk = gm != nullptr ? dynamic_cast<i6Block*>(gm->body) : nullptr;
            if(blk == nullptr)
                parsingError(format("Chained subscript: '{0}' has no readable operator[].", typeDisplayName(curType)));
            string recv = "(" + readText + ")";
            emitterBindings eb2; eb2.self = recv; eb2.val = recv; eb2.prop = "0";
            eb2.fn = gm; eb2.args.push_back(idx->text());
            readText = expandEmitterBody(blk, eb2);
            curType  = innerElem;
            continue;
        }
        if(after.is(token::period)){
            // Element member write/call: grid[i]..[j].member = v  or  .method(args). Fold this
            // final subscript into a READ (yielding the element value), then dispatch on it.
            functionDef* gm = (curCls != nullptr && !innerElem.empty())
                            ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/false) : nullptr;
            i6Block* rblk = gm != nullptr ? dynamic_cast<i6Block*>(gm->body) : nullptr;
            if(rblk == nullptr)
                parsingError(format("Chained subscript: '{0}' has no readable operator[].", typeDisplayName(curType)));
            string recv0 = "(" + readText + ")";
            emitterBindings rb0; rb0.self = recv0; rb0.val = recv0; rb0.prop = "0";
            rb0.fn = gm; rb0.args.push_back(idx->text());
            string recv = "(" + expandEmitterBody(rblk, rb0) + ")";     // the element value (type innerElem)
            file.getToken(); // consume '.'
            token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            string memberName = memberTok.value;
            token afterMember = file.getToken();
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
                cs.functionName = recv + "." + (method->i6name.empty() ? memberName : method->i6name);
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
           && !isArrayElementCompatible(valExpr->resolvedType, innerElem))
            parsingError(format("Cannot assign value of type '{0}' to element of array<{1}>",
                                typeDisplayName(valExpr->resolvedType), typeDisplayName(innerElem)));
        checkByteElementRange(valExpr, innerElem);
        functionDef* setM = (curCls != nullptr && !innerElem.empty())
                          ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/true,
                                                 valExpr ? valExpr->resolvedType : "") : nullptr;
        i6Block* sblk = setM != nullptr ? dynamic_cast<i6Block*>(setM->body) : nullptr;
        if(sblk == nullptr)
            parsingError(format("No operator[]= for element type '{0}' on type '{1}'.",
                                typeDisplayName(innerElem), typeDisplayName(curType)));
        string recv = "(" + readText + ")";
        emitterBindings sb; sb.self = recv; sb.val = recv; sb.prop = "0";
        sb.fn = setM; sb.args = { idx->text(), valExpr->text() };
        sb.elemType = innerElem;
        string b = expandEmitterBody(sblk, sb);
        functionCallStatement& cs = *(new functionCallStatement());
        cs.src = stmtLoc;
        cs.emitterBody = b;
        if(body != nullptr) body->statements.push_back(&cs);
        return false;
    }
    return false;
}

// `a[i] = v;` — a single-subscript element write.
bool bglParser::processSubscriptWrite(string arrPath, expression* indexExpr, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    file.getToken(token::assignment);
    expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
    if(auto* arr = dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(arrPath, func, body)))
        if(arr->literalElements) checkLiteralElements(*arr, {valExpr});

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
    if(!elemType.empty())
        setMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/true, valType);
    if(setMethod == nullptr){
        if(elemType.empty())
            parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrPath));
        parsingError(format("No operator[]= for element type '{0}' on type '{1}'. Add an overload or use a supported element type.",
            typeDisplayName(elemType), typeDisplayName(arrType)));
    }
    // Validate value type against element type
    if(!valType.empty() && !isArrayElementCompatible(valType, elemType))
        parsingError(format("Cannot assign value of type '{0}' to element of array<{1}>",
            typeDisplayName(valType), typeDisplayName(elemType)));
    checkByteElementRange(valExpr, elemType);

    // Compute $self and $prop
    size_t innerDot = arrPath.rfind('.');
    string selfValue = (innerDot == string::npos) ? arrPath : arrPath.substr(0, innerDot);
    string propValue = (innerDot == string::npos)
        ? (isWordArrayType(arrType) ? "0" : "<$prop undefined>")
        : arrPath.substr(innerDot + 1);

    // Member (property) WORD array write uses the orLibrary property convention
    // obj.&prop-->n = v (0-indexed, no count slot), not the global/table form. A dotted
    // path is already a property access; a bare name resolving to a member qualifies to one.
    string memOwner, memProp;
    bool isMemberWordArray = false;
    if(isWordArrayType(arrType)){
        if(innerDot != string::npos){
            memOwner = selfValue; memProp = propValue; isMemberWordArray = true;
            if(func != nullptr)   // the owner as this routine reaches it (a capture slot in a lambda)
                if(string q = qualifyIdentifier(selfValue, func, body); !q.empty()) memOwner = q;
        }
        else isMemberWordArray = splitQualifiedMember(arrPath, func, body, memOwner, memProp);
    if(isMemberWordArray && memberArrayIsRef(memOwner, memProp, func, body)){
        arrPath = memOwner + "." + memProp;   // the pointer the member holds
        isMemberWordArray = false;
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
    if(!isMemberWordArray && !operatorRef(elemType, "=").empty()){
        if(auto* ac = languageService.findClass("array"))
            if(auto* owned = dynamic_cast<functionDef*>(findMemberInHierarchy(ac, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "setownedat" && fn->isEmitter;
                })))
                setMethod = owned;
    }
    if(isMemberWordArray)
        callStmt.emitterBody = memOwner + ".&" + propertyI6Name(memProp) + "-->(" + indexExpr->text() + ") = " + valExpr->text();
    else if(setMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(setMethod->body)) {
            emitterBindings ab; ab.self = selfValue; ab.val = arrPath; ab.prop = propValue;
            ab.fn = setMethod;
            for(expression* a : callStmt.args) ab.args.push_back(a->text());
            for(expression* a : callStmt.args) ab.argTypes.push_back(a->resolvedType);
            ab.elemType = elemType;   // one substitution covers every $opref in the body
            callStmt.emitterBody = expandEmitterBody(blk, ab);
        }
    if(!isMemberWordArray && !setMethod->isEmitter)   // a routine-bodied operator []= is a method
        callStmt.functionName = arrPath + "." + (setMethod->i6name.empty() ? mangleOperatorName(setMethod->name)
                                                                            : setMethod->i6name);
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

// `name[i] …` — an element write, a chained subscript, or member access on the element.
bool bglParser::processSubscriptStatement(token tok, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string arrPath = (string)tok;  // e.g. "scores" or "player.inventory"
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
    if(isConstVariable(lhsOriginal, func, body))
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
            string aliased  = memberI6Name(resolveIdentifierType(recvPath, func, body, mem), mem);
            if(aliased != mem) qualified = qualified.substr(0, d + 1) + aliased;
        }
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
            string aliased  = memberI6Name(resolveIdentifierType(recvPath, func, body), mem);
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
            for(typeMember* m : ownerAsCls->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isStatic && vd->name == propName){
                        if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                        string mangledName = "_bgl_" + ownerAsCls->name + "_" + propName;
                        t.variableLeft = mangledName;
                        emitterSelfForLhs = mangledName;  // $self should be the mangled global, not the owner
                        leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                        break;
                    }
            if(t.variableLeft != "_bgl_" + ownerAsCls->name + "_" + propName)
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
            if(!hideOwnerType.empty())
                enforceHidden(getDispatchClass(hideOwnerType), propName, "=", {}, ownerPath);
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
            if(hierarchyRoot == nullptr){
                if(auto* ownerObj = dynamic_cast<objectDef*>(&ownerTd)){
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
        if(emitterSelfForLhs.rfind("_bgl_", 0) != 0
           && (leftType == nullptr || isPropertyClassType(leftType->name))){
            // $self = the owner object, not the full obj.prop path — and the owner's EMITTED name,
            // since an owner declared `object beacon as lamp` otherwise produced `move beacon to …`,
            // addressing an object Inform 6 never declared.
            string emitOwner = ownerPath;
            if(func != nullptr && ownerPath.find('.') == string::npos){
                string q = qualifyIdentifier(ownerPath, func, body);
                if(!q.empty() && q.find('(') == string::npos && q.find('.') == string::npos)
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
                if(arr.isMember){ emitOwner = arr.owner; t.emitterProp = arr.prop; }
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
            if(auto* vd = languageService.findGlobalAs<variableDeclaration>(lhsOriginal)){ leftType = &vd->type; if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray; }
    }

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
    return t;
}

// Applies `operator =` dispatch (emitter, method, or conversion) to one assignment node.
void bglParser::resolveAssignmentOperator(assignmentStatement& a, expression* val, const AssignTarget& t, bool isBindAssign){
    if(t.leftType != nullptr && !isBindAssign) applyImplicitConversion(val, t.leftType->name);
    if(t.leftType != nullptr && t.leftType->name == "verb") applyActionConstant(val);
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
    // Only `:=` skips operator= dispatch — that is what rebinding means. A plain `=`
    // on a `ref` slot assigns THROUGH the reference: it dispatches the type's
    // operator= into whatever the slot currently points at, exactly as it would on a
    // slot that owned its instance.
    if(classType != nullptr && val != nullptr && !isBindAssign){
        string valueTypeName = val->resolvedType;
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
                    emitterBindings cb; cb.cls = classType->i6Name();
                    a.emitterBody = expandEmitterBody(dynamic_cast<i6Block*>(opFunc->body), cb);
                    a.emitterParam = opFunc->params[0]->name;
                } else {
                    if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
                    string paramName = opFunc->params[0]->name;
                    // An ancestor cast calls THAT class's routine statically (I6 `obj.Base::prop`).
                    string routine = t.ancestorCast != nullptr ? t.ancestorCast->i6Name() + "::" + opFunc->i6name : opFunc->i6name;
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
                        a.emitterBody  = format("$target.{0}(${1});", opFunc->i6name, paramName);
                        a.emitterParam = paramName;
                        found = true;
                    }
                }
            }
            bool foundViaOperatorEq = found;
            // array<char> (byteArray) matches the inherited array<T> word-copy operator=
            // (via the upcast pass — byteArray : array). That word-copy corrupts byte data
            // (copies `length` WORDS, not bytes). Char-array value-copy isn't supported yet —
            // a clean error beats corruption/broken I6. Fires for any operator= match on a
            // byte-array LHS (byteArray has no operator= of its own — only the inherited copy).
            if(foundViaOperatorEq && lhsIsByteArray)
                parsingError("array<char> value-copy (`dst = src`) is not yet supported — the element copy would corrupt byte data. Copy elements explicitly, or use <string>/<buf> for text buffers.");
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
                        val->tokens.push_back(argText + "." + opFn->i6name + "()");
                        val->resolvedType = leftType->name;
                        found = true;
                    }
            }
            if(!found)
                parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName(leftType->name)));
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
    if(sc.func != nullptr && lhsOriginal.find('.') == string::npos)
        if(qualifyIdentifier(lhsOriginal, sc.func, sc.body).rfind("self.", 0) == 0)
            lhsOriginal = "self." + lhsOriginal;
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
            cb.self = recv; cb.val = recv; cb.prop = prop;
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
        file.getToken(token::endStatement);  // consume ';'
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
    expression* rhs = parseExpression(file.getToken(), {token::endStatement, "?"}, func, body);
    currentExpectedType = savedExpectedAssign;
    {
        string head = lhsOriginal.substr(0, lhsOriginal.find('.'));
        bool isLocal = lhsOriginal.find('.') == string::npos
            && (qualifyFromParams(head, func) || qualifyFromBodyLocals(head, body) || qualifyFromAncestorBlocks(head, body));
        if(!isLocal) rejectEscapingLambda(rhs, format("stored in '{0}'", lhsOriginal));
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
        expression* trueVal  = parseExpression(file.getToken(), {":"}, func, body);
        ifStmt.thenBlock = new statementBlock();
        for(statement* inj : pendingInjections) ifStmt.thenBlock->statements.push_back(inj);
        pendingInjections.clear();
        expression* falseVal = parseExpression(file.getToken(), {token::endStatement}, func, body);
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
        string rhsT = rhs != nullptr ? rhs->resolvedType : string();
        classDef* lhsCls = classType;
        if(lhsCls == nullptr)
            parsingError(format("the reference binding operator ':=' needs a class-typed left "
                                "side; '{0}' is not one. Use '=' for ordinary assignment.",
                                lhsOriginal));
        if(rhsT.empty() || getDispatchClass(rhsT) == nullptr)
            parsingError(format("the reference binding operator ':=' binds a reference, so the "
                                "right side must be an instance of a class; got '{0}'.",
                                typeDisplayName(rhsT.empty() ? "unknown" : rhsT)));
        else if(!isTypeCompatible(rhsT, lhsCls->name))
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
            classDef* ac = languageService.classOf(aType);
            typeMember* opm = ac ? findMemberInHierarchy(ac, [&](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m);
                return f && f->name == symbol.value && f->isEmitter && f->params.size() == 1
                       && dynamic_cast<i6Block*>(f->body) != nullptr;
            }) : nullptr;
            if(!opm)
                parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(aType)));
            auto* opFunc = dynamic_cast<functionDef*>(opm);
            emitterBindings lb; lb.prop = prop;
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
    if(isConstVariable(tok.value, func, body))
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
        bool enumLhs = cls0 == nullptr && languageService.findEnum(lhsType0) != nullptr;
        if((cls0 != nullptr || enumLhs) && !declaresOp && func != nullptr && !file.hasPendingToken
           && !file.peekToken().is(token::braceOpen)){
            string rhsSrc = file.getRawTextToStatementEnd();
            string name = tok.originalValue.empty() ? tok.value : tok.originalValue;
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

    expression* rhs = parseExpression(file.getToken(), {token::endStatement}, func, body);
    // Set-up the operand needs (a `?.` test, a temp) runs before the assignment that reads it.
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
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
    bool emitterFound = false;
    if(lhsClass != nullptr && rhs != nullptr && !rhs->resolvedType.empty()){
        string rhsType = rhs->resolvedType;
        // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all
        typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                   && opFunc->params.size()==1 && opFunc->params[0]->type.name==rhsType
                   && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        });
        // Conversion fallback: check if RHS type converts to a type the operator accepts
        if(!m){
            classDef* rhsCls = languageService.classOf(rhsType);
            if(rhsCls != nullptr)
                for(typeMember* rm : rhsCls->members){
                    auto* convFn = dynamic_cast<functionDef*>(rm);
                    if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
                    string convertedType = convFn->returnType.name;
                    m = findMemberInHierarchy(lhsClass, [&](typeMember* m2){
                        auto* opFunc = dynamic_cast<functionDef*>(m2);
                        return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                               && opFunc->params.size()==1 && opFunc->params[0]->type.name==convertedType
                               && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                    });
                    if(m) break;
                }
        }
        // var wildcard fallback
        if(!m) m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                   && opFunc->params.size()==1 && opFunc->params[0]->type.name=="var"
                   && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        });
        if(m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            auto* blk = dynamic_cast<i6Block*>(opFunc->body);
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
            bool cIsMember = splitQualifiedMember(lhs, func, body, cOwner, cProp);
            if(cIsMember && memberArrayIsRef(cOwner, cProp, func, body)) cIsMember = false;
            emitterBindings pb;   // $self stays deferred to emit time; only $prop/$opref resolve here
            if(isWordArrayType(lhsTypeName) || lhsTypeName == "bytearray"){
                pb.prop = cIsMember ? cProp : "0";
                pb.elemType = resolveArrayElementType(lhs, func, body);
            }
            a.emitterBody = expandEmitterBody(blk, pb);
            a.emitterParam = opFunc->params[0]->name;
            a.emitterSelf = cIsMember ? cOwner : lhs;
            if(body != nullptr) body->statements.push_back(&a);
            emitterFound = true;
        }
    }
    // A regular-method `operator op=`: called on the left-hand value.
    if(!emitterFound && lhsClass != nullptr && rhs != nullptr){
        string rhsType = rhs->resolvedType;
        typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn != nullptr && fn->name == symbol.value && !fn->isEmitter && fn->params.size() == 1
                && (fn->params[0]->type.name == rhsType || isTypeCompatible(rhsType, fn->params[0]->type.name));
        });
        if(auto* opFunc = dynamic_cast<functionDef*>(m)){
            if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
            i6RawNode& node = *(new i6RawNode());
            node.text = lhs + "." + opFunc->i6name + "(" + rhs->text() + ");";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
            emitterFound = true;
        }
    }
    if(!emitterFound){
        if(!lhsTypeName.empty() && lhsTypeName != "var")
            parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(lhsTypeName)));
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
    if(isConstVariable(tok.value, func, body))
        parsingError(format("Cannot assign to const variable '{0}'", tok.value));
    // Try emitter lookup for this operator on the LHS type
    string lhsTypeName = resolveIdentifierType(tok.value, func, body);
    classDef* lhsClass = languageService.classOf(lhsTypeName);
    bool emitterFound = false;
    if(lhsClass != nullptr){
        if(typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                   && opFunc->params.empty() && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        })){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            auto* blk = dynamic_cast<i6Block*>(opFunc->body);
            emitterBindings sb2; sb2.self = lhs; sb2.val = lhs;
            i6RawNode& node = *(new i6RawNode());
            node.text = expandEmitterBody(blk, sb2) + ";";
            node.src = stmtLoc;
        node.cooked = true;   // names locals: renamed like any expression
            if(body != nullptr) body->statements.push_back(&node);
            emitterFound = true;
        }
    }
    if(!emitterFound){
        if(!lhsTypeName.empty() && lhsTypeName != "var")
            parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(lhsTypeName)));
        i6RawNode& node = *(new i6RawNode());
        node.text = lhs + symbol.value + ";";
        node.src = stmtLoc;
        node.cooked = true;   // names locals: renamed like any expression
        if(body != nullptr) body->statements.push_back(&node);
    }
    for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
    postInjections.clear();
    return false;
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
    if(rawName == "replaced" && currentFunc && !currentFunc->replacedTarget.empty()){
        rawName = currentFunc->replacedTarget;
        currentFunc->replacedWasCalled = true;
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
        string qualified = qualifyIdentifier(rawName, func, body);
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
        braceHints = braceArgHints(candidates);
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
    } else {
        // Single-hop receiver (`beacon.flash()`). The namespace rewrite above only fires when the
        // qualified name is itself a registered object, which an `as` alias never is — the registry
        // keys on the Beguile name. So `object beacon as lamp` emitted a dangling `beacon.flash()`
        // while its property accesses, which go through qualifyIdentifier, emitted `lamp.wattage`.
        // Going through qualifyIdentifier here keeps local/parameter precedence: a local named
        // `beacon` resolves to itself and nothing is rewritten.
        string q = qualifyIdentifier(objectPath, func, body, methodName);
        // A `#using`-imported member qualifies to its path (`kit.counters`), which is sent as it is.
        if(!q.empty() && q != objectPath && q.find('(') == string::npos
           && (q.find('.') == string::npos || q.rfind("self.", 0) != 0))
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
    // Ancestor-qualified method dispatch (statement form): `(Base)obj.method(args);` emits I6
    // `obj.Base::method(args)`, forcing static dispatch to Base's version — the super-call /
    // ancestor-version idiom. Only when Base is a strict ancestor of the receiver's actual
    // type (a real upcast); identity/downcast/base-typed-local stay dynamic. Consumed at the
    // non-emitter emission below; emitter methods (inlined) are rejected.
    classDef* ancestorDispatchClass = nullptr;
    if(!stmtCastType.empty())
        if(isAncestorClass(getDispatchClass(stmtCastType), getDispatchClass(actualPathType)))
            ancestorDispatchClass = getDispatchClass(stmtCastType);
    stmtCastType = "";  // consume the cast
    if(objectType.empty())
        parsingError(format("Unknown variable '{0}'", objectPath));
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
        ? (isWordArrayType(objectType) ? "0" : "<$prop undefined>")
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
            selfValue = memOwner + "." + memProp;   // the pointer the member holds
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
        if(isMemberArr){ selfValue = memOwner; propValue = memProp; }
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
        if(!method->isEmitter){
            string callName = method->i6name.empty() ? methodName : method->i6name;
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
            }
        chainReturnType = method->returnType.name;
    }
    return false;
}

// `name(args);` — binds a global function call statement.
void bglParser::bindGlobalCallStatement(functionCallStatement& callStmt, token tok, string& chainReturnType, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    // global function call: bind (resolve + validate + finalize) then stage emitter body
    GlobalCallBinding gcb = bindGlobalCall(callStmt.functionName, callStmt.args,
                                            callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                            func, body);
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
        { vector<vector<interpolatedSegment>> interp; finalizeCallArgs(chainArgs, chainPal.namedArgNames, interp, chainMethod); }
        string selfText = callStmt.emitterBody.empty() ? string() : resolveEmitterText(callStmt);
        if(selfText.empty()){
            selfText = (callStmt.displayName.empty() ? callStmt.functionName : callStmt.displayName) + "(";
            for(size_t i = 0; i < callStmt.args.size(); i++) selfText += (i ? ", " : "") + callStmt.args[i]->text();
            selfText += ")";
        }
        if(!chainMethod->isEmitter && !chainMethod->isStatic){
            // A routine method: send it to the prior result.
            string call = parenthesizeReceiver(selfText) + "."
                        + (chainMethod->i6name.empty() ? chainMethod->name : chainMethod->i6name) + "(";
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
    // A lambda body parsed as a statement ends at the enclosing argument list's `,` or `)`, which
    // is handed back for that list to read.
    if(lambdaBodyStatement && (chainTok.is(token::comma) || chainTok.is(token::parenClose))){
        stashedToken = chainTok;
        return false;
    }
    chainTok.assert(token::endStatement);
    return false;
}

// `name(args);` / `recv.method(args);` — a call used as a statement, with method chaining.
bool bglParser::processCallStatement(token tok, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;

    // Guard: a bare union-typed value cannot be called directly — its runtime type is not yet
    // known, so calling it would run whichever member it happens to hold (a string as a routine
    // crashes). Require narrowing first (a `(func<...>)x` cast, after a `typeof(x)` check).
    {
        string calleeType = resolveIdentifierType((string)tok, func, body);
        if(isUnionType(calleeType))
            parsingError(format("Cannot call '{0}' directly — it has union type '{1}'. "
                "Discriminate with typeof() and narrow with a cast first, e.g. "
                "`func<void> f = (func<void>){0}; f();`", (string)tok, calleeType));
    }

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
    if(callStmt.emitterBody.find("$target") != string::npos)
        callStmt.emitterBody = replaceWord(callStmt.emitterBody, "$target", "sp");

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
    // `(T)obj = v;` — assignment through T's `operator =`.
    else if(tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
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

    // Prefix ++ / --
    if(tok.is(eTokenType::oper) && (tok.value == "++" || tok.value == "--"))
        return processPrefixIncDec(tok, sc);

    // `(expr)(args);` — a call on a parenthesized routine value.
    if(tok.is(token::parenOpen)){
        expression* e = parseExpression(tok, {token::endStatement}, sc.func, sc.body);
        string text = e ? e->text() : "";
        if(text.empty() || text.back() != ')' || text.front() != '(' || text.find(")(") == string::npos)
            parsingError("A statement starting with '(' must call the parenthesized value: (f)(args);");
        i6RawNode& node = *(new i6RawNode());
        node.text = text + ";";
        node.src = sc.src;
        if(sc.body != nullptr) sc.body->statements.push_back(&node);
        return false;
    }

    token symbol = parseStatementPath(tok, sc);
    return dispatchPathStatement(tok, symbol, sc);
}

// `path <symbol> …` — the statement a name or dotted path starts, by the token after it: a value
// emitter, subscript, assignment, compound assignment, `++`/`--`, or call.
bool bglParser::dispatchPathStatement(token tok, token symbol, StatementContext& sc){
    if(pendingPrefixOp && symbol.is(token::endStatement)){   // `++<computed receiver>.member;`
        token op = *pendingPrefixOp;
        pendingPrefixOp.reset();
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

    if(symbol.is(token::parenOpen))  //then this is a function call.
        return processCallStatement(tok, sc);

    return parsingError(format("Unhandled token '{0}'",tok.value));
}

#pragma endregion

//-------------------------------------------------------------------------------------------------------------------------------
// Throw an error, formatting the output to point to the current line
