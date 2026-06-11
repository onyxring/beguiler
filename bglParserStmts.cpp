// ===============================================================================
// bglParserStmts.cpp - statement-level parsing.
//
// Extracted from bglParser.cpp (Phase 4 of the refactor). Holds parsers for
// individual statement forms plus the free-standing `processStatement` (which
// handles assignments, function calls, and compound-op statements at the
// expression level).
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
    if(body != nullptr) body->statements.push_back(&cont);
    return false;
}

bool bglParser::processRtrue(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'rtrue' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot use 'rtrue' in void routine '{0}'", func->name));
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
        parsingError(format("Cannot use 'rfalse' in void routine '{0}'", func->name));
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
        parsingError(format("Cannot use '{0}' in void routine '{1}'", which, func->name));
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
            printCall.emitterBody = processBglConditionals(blk->i6Body);
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
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    if(body != nullptr) body->statements.push_back(&rs);
    return false;
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
    if(func != nullptr && func->returnType.name == "void"){
        // Allow `return <void-typed expr>;` as the C/I6 idiom shorthand. In loose-mode
        // contexts (`#bgl{}` islands and `.inf` precompiler mode), unresolved identifiers
        // resolve to `var`; treat `var` as compatible too so `return f()` ports verbatim
        // when `f` was declared in I6 and isn't visible to the Beguile resolver.
        string retType = retExpr ? retExpr->resolvedType : "";
        if(retType != "void" && retType != "var")
            parsingError(format("Cannot return a value from void routine '{0}'", funcName));
    }
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    rs.returnExpression = retExpr ? retExpr->text() : "";
    // Drain any pre-statements the expression created (e.g. ternary lowering into _bgl_temp)
    // BEFORE appending the return, so they execute in the current function rather than leaking
    // into the next one parsed.
    if(body != nullptr){
        for(statement* inj : pendingInjections) body->statements.push_back(inj);
        pendingInjections.clear();
        body->statements.push_back(&rs);
    }
    return false;
}


// ===============================================================================
// Control-flow statements
// ===============================================================================
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
        processStatementDispatch(next, thenCtx);
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
            processStatementDispatch(elseNext, elseCtx);
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
    // Drain any ternary injections from the condition BEFORE the while statement
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
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
        processStatementDispatch(next, whileCtx);
    }
    loopDepth--;
    if(body != nullptr) body->statements.push_back(&whileStmt);
    return false;
}

bool bglParser::processFor(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'for' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller already consumed "for" "("

    bool isForIn = false;
    string elemVarName, elemVarType;

    token peek = file.peekToken();
    if(peek.isDataType()){
        token typeTok = file.getToken(eTokenType::dataType);
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
                    parsingWarning("Loop variable '" + name + "' shadows global of the same name; the global is unreachable from this loop body.");
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
            forStatement& forStmt = *(new forStatement());
            forStmt.src = stmtLoc;
            string initText = nameTok.value;
            token tt = file.getToken();
            while(tt.isNot(token::endStatement)){
                if(!initText.empty()) initText += " ";
                initText += tt.value;
                tt = file.getToken();
            }
            forStmt.initText = initText;
            forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
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
            currentLoopVars.insert(nameTok.value); loopDepth++;
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock, forStmt.body);
                while(processNextStatement(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatementDispatch(next, forCtx);
            }
            // If ternary in increment: append injections + increment as last body statements
            if(!incrInjections.empty()){
                for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
                i6RawNode* incrStmt = new i6RawNode();
                incrStmt->text = incrText + ";";
                forStmt.body->statements.push_back(incrStmt);
            }
            loopDepth--; currentLoopVars.erase(nameTok.value);
            if(body != nullptr) body->statements.push_back(&forStmt);
            return false;
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
                for(typeDef* g : languageService.globals)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == elemVarName){ elemVarType = vd->type.name; break; }
            if(elemVarType.empty())
                parsingError(format("'for in': iteration variable '{0}' is not declared", elemVarName));
        } else {
            // C-style for — nameTok was the first init token
            forStatement& forStmt = *(new forStatement());
            forStmt.src = stmtLoc;
            string initText = nameTok.value;
            token tt = file.getToken();
            while(tt.isNot(token::endStatement)){
                if(!initText.empty()) initText += " ";
                initText += tt.value;
                tt = file.getToken();
            }
            forStmt.initText = initText;
            forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
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
            currentLoopVars.insert(nameTok.value); loopDepth++;
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock, forStmt.body);
                while(processNextStatement(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatementDispatch(next, forCtx);
            }
            // If ternary in increment: append injections + increment as last body statements
            if(!incrInjections.empty()){
                for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
                i6RawNode* incrStmt = new i6RawNode();
                incrStmt->text = incrText + ";";
                forStmt.body->statements.push_back(incrStmt);
            }
            loopDepth--; currentLoopVars.erase(nameTok.value);
            if(body != nullptr) body->statements.push_back(&forStmt);
            return false;
        }
    }

    if(!isForIn){
        // C-style for — init starts with non-identifier or empty
        forStatement& forStmt = *(new forStatement());
        forStmt.src = stmtLoc;
        string initText;
        token tt = file.getToken();
        while(tt.isNot(token::endStatement)){
            if(!initText.empty()) initText += " ";
            initText += tt.value;
            tt = file.getToken();
        }
        forStmt.initText = initText;
        forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
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
        loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, forStmt.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        if(!incrInjections.empty()){
            for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
            i6RawNode* incrStmt = new i6RawNode();
            incrStmt->text = incrText + ";";
            forStmt.body->statements.push_back(incrStmt);
        }
        loopDepth--;
        if(body != nullptr) body->statements.push_back(&forStmt);
        return false;
    }

    // for-in shared (Form 1 and Form 2)
    file.getToken("in");

    // Inline initializer list: for(int j in {1, 2, 3})
    if(file.peekToken(1).is(token::braceOpen)){
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
        if(body != nullptr) body->statements.push_back(&tmpDecl);

        string counterName = format("_bglfi{0}", forInCounter++);
        variableDeclaration& counterDecl = *(new variableDeclaration());
        counterDecl.name = counterName;
        counterDecl.type = languageService.getType("var");
        if(body != nullptr) body->statements.push_back(&counterDecl);

        if(elemVarType == "auto" && !elements.empty() && !elements[0]->resolvedType.empty()){
            elemVarType = elements[0]->resolvedType;
            classDef* elemCls = dynamic_cast<classDef*>(&languageService.getType(elemVarType));
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
        forCtx.params.push_back(&elemParam);
        token next = file.getToken();
        currentLoopVars.insert(elemVarName); loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, fi.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        loopDepth--; currentLoopVars.erase(elemVarName);
        if(body != nullptr) body->statements.push_back(&fi);
        return false;
    }

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
            processStatementDispatch(next, forCtx);
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
        for(typeDef* g : languageService.globals)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(g))
                if(ad->name == arrExprText){ arrElemType = ad->elementType; arrName = arrExprText; break; }
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
                arrElemType = (tn.size() > 6 && tn.substr(0,6) == "array<") ? tn.substr(6, tn.size()-7) : "var";
                arrName = arrExprText;
                break;
            }

    if(arrName.empty()){
        arrName = format("_bglfia{0}", forInCounter++);
        variableDeclaration& tmpDecl = *(new variableDeclaration());
        tmpDecl.name = arrName;
        tmpDecl.type = languageService.getType("var");
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
            classDef* elemCls = dynamic_cast<classDef*>(&languageService.getType(elemVarType));
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
    // A <string> is a managed object: iterate via getLength()/getChar() dispatch
    // (handled in the emitter), not the raw byte template.
    fi.isStringForIn = isStringContainer;
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
    forCtx.params.push_back(&elemParam);
    forCtx.body = fi.body;
    token next = file.getToken();
    currentLoopVars.insert(elemVarName); loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, fi.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processStatementDispatch(next, forCtx);
    }
    loopDepth--; currentLoopVars.erase(elemVarName);
    if(body != nullptr) body->statements.push_back(&fi);
    return false;
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
    string conditionType = swStmt.condition->resolvedType;
    classDef* condCls = !conditionType.empty() ? dynamic_cast<classDef*>(&languageService.getType(conditionType)) : nullptr;
    if(condCls != nullptr){
        std::function<void(classDef*)> findSwitchOps = [&](classDef* c){
            for(typeMember* m : c->members)
                if(auto* fn = dynamic_cast<functionDef*>(m))
                    if(fn->name == "switch" && fn->isEmitter && fn->params.size() == 1)
                        if(auto* blk = dynamic_cast<i6Block*>(fn->body)){
                            string paramType = fn->params[0]->type.name;
                            if(swStmt.switchEmitters.find(paramType) == swStmt.switchEmitters.end()){
                                string b = processBglConditionals(blk->i6Body);
                                swStmt.switchEmitters[paramType] = fn->params[0]->name + "\t" + b;
                            }
                        }
            for(classDef* base : c->baseClasses) findSwitchOps(base);
        };
        findSwitchOps(condCls);
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
                expression* val = parseExpression(file.getToken(), {":", ",", "to"}, func, body);
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
        while(true){
            token peek = file.peekToken();
            if(peek.is(token::braceClose) || peek.is("case") || peek.is("default")) break;
            token st = file.getToken();
            if(st.is("break")){ file.getToken(token::endStatement); continue; }
            processStatementDispatch(st, caseCtx);
        }
        swStmt.cases.push_back(&sc);
    }
    if(swStmt.needsIfChain) languageService.switchTempNeeded = true;
    if(body != nullptr) body->statements.push_back(&swStmt);
    return false;
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
    // Resolve the variable's type — must be a pooled class.
    string varTypeName = resolveIdentifierType(varName, func, body);
    if(varTypeName.empty())
        parsingError(format("'delete {0}': unknown variable", nameTok.originalValue.empty() ? varName : nameTok.originalValue));
    classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varTypeName));
    if(cls == nullptr || cls->poolSize == 0)
        parsingError(format("'delete {0}': '{1}' is not a pooled class. delete is only valid for instances of classes declared with `[N]` or `extern[]`.",
            nameTok.originalValue.empty() ? varName : nameTok.originalValue, varTypeName));
    file.getToken(token::endStatement);
    // Emit as `ClassName.destroy(varName);`
    string qualifiedVar = func != nullptr ? qualifyIdentifier(varName, func, body) : varName;
    if(qualifiedVar.empty()) qualifiedVar = varName;
    i6RawNode& node = *(new i6RawNode());
    node.text = cls->i6Name() + ".destroy(" + qualifiedVar + ");";
    node.src = stmtLoc;
    if(body != nullptr) body->statements.push_back(&node);
    return false;
}


// ===============================================================================
// processStatement - free-standing expression-statement parser
// ===============================================================================
bool bglParser::processStatement(token tok, abstractObject& contextObj){
    sourceLocation stmtLoc = tok.src.line > 0 ? tok.src : file.currentLocation();
    currentStatementSrc = stmtLoc;
    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    string stmtCastType; // set when statement begins with (TypeName) cast prefix

    // Cast prefix: (TypeName)obj.method(args); — overrides type used for method dispatch
    if(tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
        stmtCastType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
        tok = file.getToken();  // the actual object identifier
    }

    // Static member access: ClassName.member — reclassify the class as an identifier so
    // dot-access works. Object instances are already identifiers after the type/instance split.
    if(tok.is(eTokenType::dataType)){
        if(file.peekToken(1).is(token::period) || file.peekToken(1).is("?."))
            tok.tokenType = eTokenType::identifier;
    }

    // Prefix ++ / --
    if(tok.is(eTokenType::oper) && (tok.value == "++" || tok.value == "--")){
        token varName = file.getToken(eTokenType::identifier);
        string lhs = func != nullptr ? qualifyIdentifier(varName.value, func, body) : varName.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", varName.value));
        if(isConstVariable(varName.value, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", varName.value));
        file.getToken(token::endStatement);
        // Try emitter lookup for "prefix++" / "prefix--" on the LHS type, falling back to the
        // plain "++" / "--" emitter if no prefix-specific override is defined.
        string lhsTypeName = resolveIdentifierType(varName.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
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
            string b = processBglConditionals(blk->i6Body);
            b = replaceWord(b, "$self", lhs);
            b = replaceWord(b, "$val",  lhs);
            i6RawNode& node = *(new i6RawNode());
            node.text = b + ";";
            node.src = stmtLoc;
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
            if(body != nullptr) body->statements.push_back(&node);
        }
        return false;
    }

    // Determine if this token is a literal with a registered class (e.g. intLiteral, stringLiteral).
    // If so, it may head a method call: "hello".print() or 42.someMethod().
    // literalSelfText holds the I6 text to substitute for $self in emitter bodies.
    string literalTypeName, literalSelfText;
    {
        auto resolveLiteralType = [&]() -> pair<string,string> {
            if(tok.is(eTokenType::integer))       return {"intliteral",    tok.value};
            if(tok.isString())                    return {"stringliteral", tok.value};
            if(tok.is(eTokenType::charLiteral))   { bool bare = (!tok.value.empty() && all_of(tok.value.begin(),tok.value.end(),::isdigit)) || tok.value.rfind("@",0)==0; return {"charliteral", bare ? tok.value : "'" + tok.value + "'"}; }
            return {"",""};
        };
        auto [tn, st] = resolveLiteralType();
        // Only treat as a typed literal if a classDef is actually registered for it.
        if(!tn.empty() && dynamic_cast<classDef*>(&languageService.getType(tn)) != nullptr){
            literalTypeName = tn;
            literalSelfText = st;
        }
    }
    bool tokIsLiteral = !literalTypeName.empty();
    if(!tok.is(eTokenType::identifier) && !tokIsLiteral)
        return parsingError(format("Unrecognized statement starting with token '{0}'", (string) tok));

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
            classDef* cls = !pathType.empty() ? dynamic_cast<classDef*>(&languageService.getType(pathType)) : nullptr;
            functionDef* nullTestFn = nullptr;
            if(cls != nullptr)
                nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
            if(nullTestFn == nullptr)
                parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", pathType));
            auto* blk = dynamic_cast<i6Block*>(nullTestFn->body);
            string guard = processBglConditionals(blk->i6Body);
            guard = i6Emitter::replaceWord(guard, "$self", pathSoFar);
            guard = i6Emitter::replaceWord(guard, "$val",  pathSoFar);
            { size_t s=guard.find_first_not_of(" \t\n\r"); if(s!=string::npos) guard=guard.substr(s);
              size_t e=guard.find_last_not_of(" \t\n\r;"); if(e!=string::npos) guard=guard.substr(0,e+1); }
            i6RawNode* openNode = new i6RawNode();
            openNode->text = "if (" + guard + ") {";
            pendingInjections.push_back(openNode);
            optionalChainDepth++;
        }
        token nextPart = file.getToken(eTokenType::identifier);
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
        return parsingError(format("Literal value cannot appear as a statement without a method call"));



    //----------------------------------------------------------------------
    //We've encountered an identifier, which could be a variable assignment,
    //  subscript assignment, function call, or value emitter statement.

    // Value emitter as statement: identifier; or dot-path; where it resolves to a value emitter
    if(symbol.is(token::endStatement)){
        string ident = tok.value;
        // Use qualifyIdentifier to resolve dot-paths, aliases, and #using imports
        string qualified = qualifyIdentifier(ident, func, body);
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
            return false;
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
                string bodyText = processBglConditionals(blk->i6Body);
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
            return false;
        }
    }

    // Subscript: name[i] = v  (assignment) or  name[i].member (dot-chain on result)
    if(symbol.is(token::bracketOpen)) {
        string arrPath = (string)tok;  // e.g. "scores" or "player.inventory"
        expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);

        // Peek after ']': if '.', this is a dot-chain on the subscript result (e.g. arr[0].method()).
        // Build the subscript-read text, then dispatch the continuation as a method call or property access.
        token afterBracket = file.peekToken();
        if(afterBracket.is(token::period)){
            file.getToken(); // consume '.'
            // Build subscript-read I6 text (same emitter expansion as expression-level path)
            string arrType = resolvePathType(arrPath, func, body);
            classDef* arrCls = dynamic_cast<classDef*>(&languageService.getType(arrType));
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
                    string b = processBglConditionals(blk->i6Body);
                    string pv = (arrType == "array" || arrType == "bytearray") ? "0" : "<$prop undefined>";
                    string selfValue = arrPath;
                    size_t innerDot = arrPath.rfind('.');
                    if(innerDot != string::npos){ selfValue = arrPath.substr(0, innerDot); pv = arrPath.substr(innerDot + 1); }
                    b = replaceWord(b, "$self", selfValue);
                    b = replaceWord(b, "$val",  arrPath);
                    b = replaceWord(b, "$prop", pv);
                    if(!getMethod->params.empty())
                        b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                    subscriptText = b;
                }
            // Read member name and dispatch
            token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            string memberName = memberTok.value;
            token afterMember = file.getToken();
            if(afterMember.is(token::parenOpen)){
                // Method call: arr[i].method(args)
                classDef* elemCls = dynamic_cast<classDef*>(&languageService.getType(elemType));
                ParsedArgList pal = parseCallArgList(func, body);
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
                        string b = processBglConditionals(blk->i6Body);
                        for(size_t i = 0; i < method->params.size() && i < pal.args.size(); i++)
                            b = replaceWord(b, "$" + method->params[i]->name, pal.args[i]->text());
                        b = replaceWord(b, "$self", subscriptText);
                        b = replaceWord(b, "$val",  subscriptText);
                        callStmt.emitterBody = b;
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
        }

        file.getToken(token::assignment);
        expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);

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

        functionDef* setMethod = nullptr;
        if(!elemType.empty())
            setMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/true);
        if(setMethod == nullptr){
            if(elemType.empty())
                parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrPath));
            parsingError(format("No operator[]= for element type '{0}' on type '{1}'. Add an overload or use a supported element type.",
                typeDisplayName(elemType), typeDisplayName(arrType)));
        }
        // Validate value type against element type
        string valType = valExpr ? valExpr->resolvedType : "";
        if(!valType.empty() && !isTypeCompatible(valType, elemType))
            parsingError(format("Cannot assign value of type '{0}' to element of array<{1}>",
                typeDisplayName(valType), typeDisplayName(elemType)));

        // Compute $self and $prop
        size_t innerDot = arrPath.rfind('.');
        string selfValue = (innerDot == string::npos) ? arrPath : arrPath.substr(0, innerDot);
        string propValue = (innerDot == string::npos)
            ? (arrType == "array" ? "0" : "<$prop undefined>")
            : arrPath.substr(innerDot + 1);

        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        callStmt.functionName = arrPath + ".set";
        callStmt.args.push_back(indexExpr);
        callStmt.args.push_back(valExpr);

        if(setMethod->isEmitter)
            if(auto* blk = dynamic_cast<i6Block*>(setMethod->body)) {
                string b = processBglConditionals(blk->i6Body);
                size_t pos = 0;
                for(size_t i = 0; i < setMethod->params.size() && i < callStmt.args.size(); i++)
                    b = replaceWord(b, "$" + setMethod->params[i]->name, callStmt.args[i]->text());
                b = replaceWord(b, "$self", selfValue);
                b = replaceWord(b, "$val",  arrPath);
                b = replaceWord(b, "$prop", propValue);
                callStmt.emitterBody = b;
            }
        if(body != nullptr) body->statements.push_back(&callStmt);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }

    if(symbol.is(token::assignment))  {
        assignmentStatement& assignExpr=*(new assignmentStatement());
        assignExpr.src = stmtLoc;
        string lhsOriginal = (string)tok;
        if(isConstVariable(lhsOriginal, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", lhsOriginal));
        if(func != nullptr){
            string qualified = qualifyIdentifier(lhsOriginal, func, body);
            if(qualified.empty())
                parsingError(format("Undeclared variable '{0}'", lhsOriginal));
            assignExpr.variableLeft = qualified;
        } else {
            assignExpr.variableLeft = lhsOriginal;
        }

        // look up the left-hand variable's type using original (unqualified) name
        typeDef* leftType = nullptr;
        bool lhsIsRefLocal = false;     // set true if the bare LHS resolves to a `ref` local
        string emitterSelfForLhs = lhsOriginal;  // $self value for emitter substitution

        size_t lhsDot = lhsOriginal.rfind('.');
        if(lhsDot != string::npos){
            // dot-path LHS: resolve owner type, then find property type in its class
            string ownerPath = lhsOriginal.substr(0, lhsDot);
            string propName  = lhsOriginal.substr(lhsDot + 1);
            // Check for static member assignment: ClassName.staticMember
            classDef* ownerAsCls = dynamic_cast<classDef*>(&languageService.getType(ownerPath));
            if(ownerAsCls != nullptr){
                for(typeMember* m : ownerAsCls->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->isStatic && vd->name == propName){
                            if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                            string mangledName = "_bgl_" + ownerAsCls->name + "_" + propName;
                            assignExpr.variableLeft = mangledName;
                            emitterSelfForLhs = mangledName;  // $self should be the mangled global, not the owner
                            leftType = &vd->type;
                            break;
                        }
            }
            string ownerType = leftType != nullptr ? "" : resolvePathType(ownerPath, func, body);
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
                                    leftType = &vd->type; break;
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
                    }
                }
            }
            if(!emitterSelfForLhs.starts_with("_bgl_"))  // don't override if already set by static member resolution
                emitterSelfForLhs = ownerPath;  // $self = the owner object, not the full obj.prop path
        } else {
            if(func != nullptr){
                for(paramDef* p : func->params)
                    if(p->name == lhsOriginal){ leftType = &p->type; break; }
                if(leftType == nullptr && body != nullptr)
                    for(statement* s : body->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(vd->name == lhsOriginal){
                                leftType = &vd->type;
                                if(vd->isRefLocal) lhsIsRefLocal = true;
                                break;
                            }
                if(leftType == nullptr && currentObject != nullptr)
                    for(typeMember* m : currentObject->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == lhsOriginal){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", lhsOriginal));
                                leftType = &vd->type; break;
                            }
                if(leftType == nullptr && currentClass != nullptr)
                    for(typeMember* m : currentClass->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == lhsOriginal){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", lhsOriginal));
                                leftType = &vd->type; break;
                            }
            }
            if(leftType == nullptr)
                for(typeDef* g : languageService.globals)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == lhsOriginal){ leftType = &vd->type; break; }
        }

        classDef* classType = leftType != nullptr ? dynamic_cast<classDef*>(&languageService.getType(leftType->name)) : nullptr;

        // helper: apply operator= emitter lookup to an assignment node for a given rhs expression
        auto resolveEmitter = [&](assignmentStatement& a, expression* val){
            a.emitterSelf = emitterSelfForLhs;  // always record $self for this assignment
            // `ref` locals opt out of operator= dispatch entirely: every assignment is
            // plain pointer-alias. Skip the dispatch lookups + the silent-emission error,
            // and skip the type-compatibility fallback so the plain assignment emits as-is.
            if(classType != nullptr && val != nullptr && !lhsIsRefLocal){
                string valueTypeName = val->resolvedType;
                if(!valueTypeName.empty()){
                    // Two-pass emitter lookup first — explicit operator= emitters always beat raw type compatibility
                    bool found = false;
                    {
                        typeMember* m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1
                                   && opFunc->params[0]->type.name==valueTypeName && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                        });
                        if(!m) m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1
                                   && opFunc->params[0]->type.name=="var" && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                        });
                        if(m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                            a.emitterBody = processBglConditionals(blk->i6Body);
                            a.emitterParam = opFunc->params[0]->name;
                            // Pre-substitute $class with the LHS's declared type. $self / $param /
                            // $target are substituted later at emit time (i6Emitter), but $class
                            // resolves at parse time because it depends on the static type known here.
                            if(classType != nullptr)
                                a.emitterBody = i6Emitter::replaceWord(a.emitterBody, "$class", classType->i6Name());
                            found = true;
                        }
                    }
                    // Non-emitter operator=: dispatch via a mangled method call so the routine runs
                    // exactly once and the RHS is evaluated exactly once. We synthesize a one-line
                    // emitter body using $target (the full LHS path, e.g. retval.parentWin) so that
                    // member-access assignments dispatch on the property, not its owner.
                    if(!found){
                        typeMember* m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && !opFunc->isEmitter && !opFunc->isPrePassStub
                                   && opFunc->params.size()==1 && opFunc->params[0]->type.name==valueTypeName;
                        });
                        if(!m) m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && !opFunc->isEmitter && !opFunc->isPrePassStub
                                   && opFunc->params.size()==1 && opFunc->params[0]->type.name=="var";
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
                    bool foundViaOperatorEq = found;
                    if(!found) found = isTypeCompatible(valueTypeName, leftType->name);
                    // Silent value-semantics gap: TypeCompatible let it through but no
                    // operator= matched, AND the LHS class carries its own stored fields,
                    // AND it isn't a world-tree citizen (object-derived classes use
                    // reference semantics by convention). Force the user to declare
                    // operator= so copy semantics aren't a surprise.
                    if(found && !foundViaOperatorEq && classHasStoredFields(classType) && !inheritsFromObject(classType))
                        parsingError(format("Type '{0}' has no operator=. Declare 'operator =' on the class to define copy semantics for its fields, mark the local as 'ref' to opt into pointer-reference semantics, or inherit from 'object' for tree-citizen reference semantics.",
                            typeDisplayName(leftType->name)));
                    if(!found){
                        // Fallback: check if RHS type has emitter LhsType operator(){}
                        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(valueTypeName));
                        if(rhsCls != nullptr)
                            if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                                       && opFn->returnType.name==leftType->name && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                            })){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                auto* blk = dynamic_cast<i6Block*>(opFn->body);
                                string b = processBglConditionals(blk->i6Body);
                                string argText = val->text();
                                size_t pos = 0;
                                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
                                val->tokens.clear();
                                val->tokens.push_back(b);
                                val->resolvedType = leftType->name;
                                found = true;
                            }
                    }
                    if(!found)
                        parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName(leftType->name)));
                }
            }
        };

        // Interpolated string literal on RHS: var = $"..."
        if(file.peekToken(1).is("$") && file.peekToken(2).is(eTokenType::quote)){
            file.getToken();  // consume '$'
            assignExpr.interpSegments = parseInterpolatedSegments(func, body);
            file.getToken(token::endStatement);  // consume ';'
            // Create a dummy RHS expression typed as interpolatedstringliteral for emitter resolution
            expression* rhs = new expression();
            rhs->resolvedType = "interpolatedstringliteral";
            assignExpr.assignedExpression = rhs;
            resolveEmitter(assignExpr, rhs);
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
                resolveEmitter(*a, val);
                return a;
            };

            expression* trueVal  = parseExpression(file.getToken(), {":"}, func, body);
            expression* falseVal = parseExpression(file.getToken(), {token::endStatement}, func, body);

            ifStmt.thenBlock = new statementBlock();
            ifStmt.thenBlock->statements.push_back(makeAssign(trueVal));
            ifStmt.elseBlock = new statementBlock();
            ifStmt.elseBlock->statements.push_back(makeAssign(falseVal));

            if(body != nullptr) body->statements.push_back(&ifStmt);
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
            return false;
        }

        assignExpr.assignedExpression = rhs;
        // Skip operator= emitter if RHS contains $target — the opcode handles its own store
        if(rhs->text().find("$target") == string::npos)
            resolveEmitter(assignExpr, rhs);

        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&assignExpr);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    // Compound assignment: +=, -=, *=, /=, %=, |=, &=, ^=, <<=, >>=
    static const vector<string> compoundOps = {"+=","-=","*=","/=","%=","|=","&=","^=","<<=",">>="};
    if(symbol.is(eTokenType::oper) && find(compoundOps.begin(), compoundOps.end(), symbol.value) != compoundOps.end()){
        string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
        if(isConstVariable(tok.value, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", tok.value));
        expression* rhs = parseExpression(file.getToken(), {token::endStatement}, func, body);

        // Try emitter lookup for this compound operator on the LHS type
        string lhsTypeName = resolveIdentifierType(tok.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
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
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhsType));
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
                a.emitterBody = processBglConditionals(blk->i6Body);
                a.emitterParam = opFunc->params[0]->name;
                a.emitterSelf = lhs;
                if(body != nullptr) body->statements.push_back(&a);
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
    if(symbol.is(eTokenType::oper) && (symbol.value == "++" || symbol.value == "--")){
        file.getToken(token::endStatement);
        string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
        if(isConstVariable(tok.value, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", tok.value));
        // Try emitter lookup for this operator on the LHS type
        string lhsTypeName = resolveIdentifierType(tok.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
        bool emitterFound = false;
        if(lhsClass != nullptr){
            if(typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                       && opFunc->params.empty() && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
            })){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                string b = processBglConditionals(blk->i6Body);
                b = replaceWord(b, "$self", lhs);
                b = replaceWord(b, "$val",  lhs);
                i6RawNode& node = *(new i6RawNode());
                node.text = b + ";";
                node.src = stmtLoc;
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
            if(body != nullptr) body->statements.push_back(&node);
        }
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    if(symbol.is(token::parenOpen))  { //then this is a function call.

        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        // Qualify bare function name: if inside an instance and the name matches an instance
        // member method, prepend "self." so it routes to the method call path below.
        {
            string rawName = (string)tok;
            // replace chaining: replaced() resolves to the predecessor's mangled name
            if(rawName == "replaced" && currentFunc && !currentFunc->replacedTarget.empty()){
                rawName = currentFunc->replacedTarget;
                currentFunc->replacedWasCalled = true;
            }
            if(func != nullptr && rawName.find('.') == string::npos){
                string qualified = qualifyIdentifier(rawName, func, body);
                // qualifyIdentifier walks inherited VARIABLES but not functions.
                // For call-form resolution, also check the class hierarchy for inherited methods.
                // Skip if the name also exists as a global function (global arity matching wins).
                if((qualified.empty() || qualified == rawName) && currentClass != nullptr){
                    bool isGlobalFunc = false;
                    for(typeDef* g : languageService.globals)
                        if(auto* fd = dynamic_cast<functionDef*>(g))
                            if(fd->name == rawName){ isGlobalFunc = true; break; }
                    if(!isGlobalFunc){
                        function<bool(classDef*)> searchHierarchy = [&](classDef* c) -> bool {
                            for(typeMember* m : c->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == rawName) return true;
                            for(classDef* base : c->baseClasses)
                                if(searchHierarchy(base)) return true;
                            return false;
                        };
                        if(searchHierarchy(currentClass)) qualified = "self." + rawName;
                    }
                }
                callStmt.functionName = qualified.empty() ? rawName : qualified;
            } else {
                callStmt.functionName = rawName;
            }
        }

        // parse argument list
        {
            ParsedArgList pal = parseCallArgList(func, body);
            callStmt.args = pal.args;
            callStmt.namedArgNames = pal.namedArgNames;
            callStmt.interpSegmentsPerArg = pal.interpSegmentsPerArg;
        }

        string chainReturnType;
        size_t dotPos = callStmt.functionName.rfind('.');  // use LAST dot for method name
        if(dotPos != string::npos){
            // method call: validate and resolve emitter
            string objectPath = callStmt.functionName.substr(0, dotPos);  // may be "obj" or "obj.prop"
            string methodName = callStmt.functionName.substr(dotPos + 1);
            string objectName = objectPath;  // kept for backward compat in non-emitter emit path
            // Pass memberHint=methodName so the resolver disambiguates a name collision in favor
            // of whichever candidate's type actually exposes the method.
            string objectType = !stmtCastType.empty() ? stmtCastType
                              : !literalTypeName.empty() ? literalTypeName
                              : resolvePathType(objectPath, func, body, methodName);
            stmtCastType = "";  // consume the cast
            if(objectType.empty())
                parsingError(format("Unknown variable '{0}'", objectPath));
            // Compute $self and $prop for emitter substitution.
            // For literals, $self is the raw literal text (e.g. "hello", 42, 'x'), not the path.
            size_t innerDot = objectPath.rfind('.');
            string selfValue = (!literalSelfText.empty() && innerDot == string::npos)
                              ? literalSelfText
                              : (innerDot == string::npos) ? objectPath : objectPath.substr(0, innerDot);
            string propValue = (innerDot == string::npos)
                ? (objectType == "array" ? "0" : "<$prop undefined>")
                : objectPath.substr(innerDot + 1);
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
                // Element-type binding for generic receivers (array<T>, etc.).
                string recvElemType = resolveArrayElementType(selfValue, currentFunc, currentFunc ? dynamic_cast<statementBlock*>(currentFunc->body) : nullptr);
                functionDef* method = bindMethodCall(objectType, objectPath, methodName,
                                                       callStmt.args, callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                                       recvElemType);
                cls = dynamic_cast<classDef*>(&languageService.getType(objectType));
                // Non-emitter overload sets carry a mangled i6name (assigned by
                // mangleOverloadSetForReceiver inside bindMethodCall). Rewrite the
                // call statement's functionName so emission targets the mangled property.
                if(!method->isEmitter && !method->i6name.empty())
                    callStmt.functionName = objectPath + "." + method->i6name;
                // if emitter, pre-substitute $self, $prop, and $class
                if(method->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = replaceWord(b, "$self", selfValue);
                        b = replaceWord(b, "$val",  objectPath);
                        // $class — declared receiver type (ignores multiple inheritance).
                        // Resolves to the variable's static type, not the type that owns the
                        // inherited emitter. Powers class-message I6 emission from mixins.
                        if(cls != nullptr)
                            b = replaceWord(b, "$class", cls->i6Name());
                        // $prop fallback — done before staging callStmt.emitterBody so that
                        // resolveEmitterText's later param substitution can still substitute
                        // a `prop`-named parameter when present (e.g. `provides(property prop)`).
                        // Skip if any parameter is named `prop` so the param sub wins.
                        bool hasPropParam = false;
                        for(paramDef* p : method->params) if(p->name == "prop"){ hasPropParam = true; break; }
                        if(!hasPropParam)
                            b = replaceWord(b, "$prop", propValue);
                        callStmt.emitterBody = b;
                        for(paramDef* p : method->params)
                            callStmt.emitterParams.push_back(p->name);
                    }
                chainReturnType = method->returnType.name;
            }
        } else {
            // global function call: bind (resolve + validate + finalize) then stage emitter body
            GlobalCallBinding gcb = bindGlobalCall(callStmt.functionName, callStmt.args,
                                                    callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                                    func, body);
            if(!gcb.funcVarReturnType.empty())  chainReturnType = gcb.funcVarReturnType;
            else if(gcb.method != nullptr)      chainReturnType = gcb.method->returnType.name;
            else                                chainReturnType = "var"; // loose mode: unresolved → opaque
            if(gcb.method && gcb.method->isEmitter)
                if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
                    callStmt.emitterBody = processBglConditionals(blk->i6Body);
                    for(paramDef* p : gcb.method->params) callStmt.emitterParams.push_back(p->name);
                }
            // Loose-mode unresolved global call: carry original case via displayName so
            // the emitter can prefer it over the lowercased functionName.
            if(gcb.method == nullptr && gcb.funcVarReturnType.empty() && looseIdentifierMode)
                callStmt.displayName = tok.originalValue;
        }

        // method chaining: handle optional ".method()" suffixes before the final ";"
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
                chainMember = file.getToken(eTokenType::identifier);
            else
                chainMember = chainTok;  // dictionaryWord already holds the method name
            file.getToken(token::parenOpen);
            vector<expression*> chainArgs;
            token chainArgTok = file.getToken();
            while(chainArgTok.isNot(token::parenClose)){
                expression* arg = parseExpression(chainArgTok, {token::comma, token::parenClose}, func, body);
                chainArgs.push_back(arg);
                if(arg->terminator == token::parenClose) break;
                chainArgTok = file.getToken();
            }
            classDef* chainCls = dynamic_cast<classDef*>(&languageService.getType(chainReturnType));
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
                        classDef* convCls = dynamic_cast<classDef*>(&languageService.getType(convertedType));
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
            if(!chainMethod->isEmitter || !dynamic_cast<i6Block*>(chainMethod->body))
                parsingError(format("Chained method '{0}' on type '{1}' is not an emitter", chainMethodName, chainReturnType));
            string selfText = resolveEmitterText(callStmt);
            i6Block* chainBlk = dynamic_cast<i6Block*>(chainMethod->body);
            string b = processBglConditionals(chainBlk->i6Body);
            b = replaceWord(b, "$self", selfText);
            b = replaceWord(b, "$val",  selfText);
            for(size_t i=0; i<chainMethod->params.size() && i<chainArgs.size(); i++)
                b = replaceWord(b, "$" + chainMethod->params[i]->name, chainArgs[i]->text());
            callStmt.emitterBody = b;
            callStmt.emitterParams.clear();
            callStmt.args.clear();
            chainReturnType = chainMethod->returnType.name;
            chainTok = file.getToken();
        }
        chainTok.assert(token::endStatement);

        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&callStmt);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    return parsingError(format("Unhandled token '{0}'",tok.value));
}

/*bool bglParser::processDataType(token dataType, bool isExternal){
    token name;
    token symbol;

    // if(dataType.is(token::constantDeclararion)){
    //     dataType = file.getToken(eTokenType::dataType);
    //     name = file.getToken(eTokenType::identifier);
    //     symbol = file.getToken(token::assignment);
    //     processConstantDeclaration(dataType, name, symbol);        
    //     file.getToken(token::endStatement);
    //     return false;
    // }
    
    name = file.getToken(eTokenType::identifier);
    if(name.is("operator")){
        //set is operator value here.
        name = file.getToken(eTokenType::oper);    
    }
    
    symbol = file.getToken({eTokenType::symbol, eTokenType::oper}); 

    //--a variable declaration, with optional assignment:
    //      int myVar;
    //      int myVar=99; 
    if(symbol.isOneOf({token::endStatement, token::assignment})) {
        processVariableDeclaration(dataType, name, symbol);
        if(symbol.value==token::assignment) file.getToken(token::endStatement);
        return false;
    }
  
    //--a function declaration:
    if(symbol.is(token::parenOpen)) return processRoutineDeclaration(dataType, name, isExternal);

    //--an object instance declaration:
    if(symbol.is(token::braceOpen)) return processObjectDeclaration(dataType, name, isExternal);

    return parsingError("Unexpected value '"+symbol.value+"'.");
   
}*/
// bool parser::processAttribute(token tok){

// }
// Resolves the Beguile type of a potentially dotted path (e.g. "player.inventory").
// Single segment: delegates to resolveIdentifierType.
// Two segments: resolves head type, then looks up tail member on that type.



#pragma endregion

//-------------------------------------------------------------------------------------------------------------------------------
// Throw an error, formatting the output to point to the current line
