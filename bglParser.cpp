// ═══════════════════════════════════════════════════════════════════════════════
// bglParser.cpp — Beguile language parser
//
// Two-pass compilation:
//   Pass 1 (preScanFile): registers type/class/object/function stubs so that
//     forward references resolve. Stubs have isPrePassStub=true and are replaced
//     during pass 2 via replaceStubMember().
//   Pass 2 (parseFile → processNextStatement): full parse, type checking, AST
//     construction. Statements are dispatched through processStatement() which
//     handles keywords (if/for/while/switch/return), assignments, function calls,
//     and compound operators.
//
// Key entry points:
//   preScanFile()       — pass 1 stub registration
//   parseFile()         — pass 2 main loop
//   parseExpression()   — expression parser (900+ lines, section-commented)
//   processStatement()  — statement-level dispatch
//
// Shared helpers (extracted to reduce duplication):
//   resolveMethod()         — unified method lookup across class hierarchy + objectDef members
//   replaceStubMember()     — replace pre-scan stub with real definition
//   applyBinaryOperator()   — binary operator emitter resolution + RHS parsing
//   findMemberInHierarchy() — recursive class member search
//   isTypeCompatible()      — type compatibility check with operator/conversion fallbacks
//   evaluateCondition()     — #if/#elif compile-time condition evaluator
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

using namespace std;

// Normalize path separators to the OS path separator unless rewritePaths is explicitly false.
// Treats unset (nullopt) as true — rewriting is the default.
// Resolve an include file path by searching source directory then a list of search paths.
// Tries each root + filename, optionally with an added extension.
// Returns the resolved absolute path, or empty string if not found.
string resolveIncludePath(const string& filename, const string& extension,
                                  const filesystem::path& sourceDir,
                                  const vector<string>& searchPaths){
    string normalized = filename;
    for(char& c : normalized)
        if(c == '/' || c == '\\') c = filesystem::path::preferred_separator;
    // Try source directory first
    filesystem::path candidate = sourceDir / (normalized + extension);
    if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
    if(!extension.empty()){
        candidate = sourceDir / normalized;
        if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
    }
    // Try each search path
    for(const string& sp : searchPaths){
        candidate = filesystem::path(sp) / (normalized + extension);
        if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
        if(!extension.empty()){
            candidate = filesystem::path(sp) / normalized;
            if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
        }
    }
    return "";
}

string rewritePathSeps(const string& path){
    if(beguilerSettings.rewritePaths.has_value() && !beguilerSettings.rewritePaths.value())
        return path;
    string result = path;
    for(char& c : result)
        if(c == '/' || c == '\\') c = settings.pathSep;
    return result;
}

// Case-insensitive file lookup: returns the first directory entry whose
// lowercased filename equals the lowercased form of `target`. Falls back
// to `target` itself (which will produce a normal "file not found" error)
// if nothing matches. If multiple entries match, one is chosen arbitrarily.
filesystem::path findCaseInsensitive(const filesystem::path& dir, const string& target){
    string lower = target;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if(filesystem::exists(dir)){
        for(const auto& entry : filesystem::directory_iterator(dir)){
            string fname = entry.path().filename().string();
            string flwr = fname;
            transform(flwr.begin(), flwr.end(), flwr.begin(), ::tolower);
            if(flwr == lower) return entry.path();
        }
    }
    return dir / target; // not found; return as-is so the error surfaces normally
}

bglParser::bglParser(){
    openCompileContext(eCompileContext::global);
    // Pre-defined compiler symbols (read-only; calculated from BEGUILER_VERSION in settings.h)
    // beguiler encodes major*1000 + minor*10 + patch (e.g. 1.0.0 = 1000, 1.1.0 = 1010)
    definedSymbols["beguiler"] = to_string(BEGUILER_VERSION);
    definedSymbols["beguilermajor"]   = to_string(BEGUILER_VERSION / 1000);
    definedSymbols["beguilerminor"]   = to_string((BEGUILER_VERSION % 1000) / 10);
    definedSymbols["beguilerpatch"]   = to_string(BEGUILER_VERSION % 10);
    //emit.to(cout);  //write the result to the terminal window for now
}
void bglParser::reset(){
    // Clear all accumulated state so the parser can re-parse from scratch (for LSP re-parse)
    compileContextStack.clear();
    openCompileContext(eCompileContext::global);
    onceFiles.clear();
    startupFiles.clear();
    emitFirstFiles.clear();
    emitLastFiles.clear();
    usingImports.clear();
    includeDepth = 0;
    forInCounter = 0;
    lambdaCounter = 0;
    loopDepth = 0;
    ternaryDepth = 0;
    preScanOnceFiles.clear();
    preScanDepth = 0;
    pendingInjections.clear();
    postInjections.clear();
    currentObject = nullptr;
    currentClass = nullptr;
    currentFunc = nullptr;
    // Reset definedSymbols to only the built-in version symbols
    definedSymbols.clear();
    definedSymbols["beguiler"] = to_string(BEGUILER_VERSION);
    definedSymbols["beguilermajor"]   = to_string(BEGUILER_VERSION / 1000);
    definedSymbols["beguilerminor"]   = to_string((BEGUILER_VERSION % 1000) / 10);
    definedSymbols["beguilerpatch"]   = to_string(BEGUILER_VERSION % 10);
    lspErrors.clear();
}

// Open an input file, process each statement.  This is the entry point to this whole parsing process
bool bglParser::parseFile(string filename){
    string absPath = filesystem::canonical(filesystem::absolute(filename)).string();
    // If this file declared #once, silently skip subsequent inclusions.
    if(onceFiles.count(absPath)) return false;
    // Guard against runaway or circular includes.
    if(includeDepth > maxIncludeDepth)
        return parsingError(format("Maximum include nesting depth ({0}) exceeded while including '{1}'", maxIncludeDepth, filename));
    includeDepth++;
    // Save file-scoped #using imports — each file has its own imports
    vector<classDef*> savedUsingImports = usingImports;
    usingImports.clear();

    try{
        file.open(absPath);
    }
    catch(runtime_error& e){
        usingImports = savedUsingImports;
        includeDepth--;
        return parsingError(e.what());
    }

    if(file.getNumberOfOpenFiles()==1){
        cout<<format("Beguiling file \"{0}\"...\n",filename);

        // Always load the system library first so built-in types (eBool, eTarget, etc.) are available
        filesystem::path systemPath = filesystem::path(settings.libPath) / "core" / "_beguileCore.bgl";
        parseFile(systemPath.string());
        // Load built-in I6 templates (for-in loop etc.) from beguilib/core/__builtins.i6b
        filesystem::path builtinsPath = filesystem::path(settings.libPath) / "core" / "__builtins.i6b";
        emitter.loadBuiltinTemplates(builtinsPath.string());
    }

    //process all statements in the file one by one.  This may include recursive calls for included files.
    try{
        while(processNextStatementV2()==false){
            //cout<<"Block processed."<<endl;
        }
    } catch(lspRecoverySignal&){
        // LSP error recovery: skip to next statement boundary, then continue parsing
        // Loop handles multiple successive errors — each recovery catches the next signal
        while(lspMode && file.getNumberOfOpenFiles() > 0) {
            // Skip tokens until we find a safe resume point: ';' or '}' at brace depth 0, or EOF
            int braceDepth = 0;
            bool hitEof = false;
            while(true) {
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { hitEof = true; break; }
                if(t.is(token::braceOpen)) { braceDepth++; continue; }
                if(t.is(token::braceClose)) {
                    if(braceDepth > 0) { braceDepth--; continue; }
                    break;  // closing brace at depth 0 — end of enclosing block
                }
                if(t.is(token::endStatement) && braceDepth == 0) break;
            }
            if(hitEof) break;
            // Reset compile context to global (error may have left us nested)
            while(compileContextStack.size() > 1) compileContextStack.pop_back();
            currentObject = nullptr;
            currentClass = nullptr;
            currentFunc = nullptr;
            // Continue parsing — if another error occurs, the loop catches it
            try {
                while(processNextStatementV2()==false){}
                break;  // parsing finished normally
            } catch(lspRecoverySignal&) {
                continue;  // another error — loop and recover again
            } catch(exitFileSignal&) {
                break;
            }
        }
    } catch(exitFileSignal&){
        // #exit directive: treat as end-of-file, discarding any open directive nesting
    }
    //emit.out<<endl;
    file.close();
    usingImports = savedUsingImports;  // restore parent file's imports
    includeDepth--;
    return false;
}

//==================================================================
// Pre-scanner methods (preScanFile, preScanDirective, preScanSkip*)
// have been moved to bglPreScanner.cpp for code organization.
//==================================================================


//===============================================================================================================================
// Grammar-driven pattern matching (V2 parser dispatcher)
//===============================================================================================================================

// initGrammarTable() is in grammarTable.cpp

bool bglParser::matchElement(const PatternElement& elem, token& tok) {
    switch(elem.kind) {
        case PatternElement::Kind::Literal:
            return tok.value == elem.literal || tok.is(elem.literal);
        case PatternElement::Kind::TokenType:
            return tok.is(elem.tokenType);
        case PatternElement::Kind::AnyOf:
            for(const string& s : elem.anyOfValues)
                if(tok.value == s || tok.is(s)) return true;
            return false;
        case PatternElement::Kind::Semantic:
            return elem.predicate && elem.predicate(tok);
        case PatternElement::Kind::Wildcard:
            return true;
    }
    return false;
}

string bglParser::describeExpected(const PatternElement& elem) {
    if(!elem.tag.empty()) return elem.tag;
    switch(elem.kind) {
        case PatternElement::Kind::Literal:   return "'" + elem.literal + "'";
        case PatternElement::Kind::TokenType:
            switch(elem.tokenType) {
                case eTokenType::identifier:    return "an identifier";
                case eTokenType::dataType:      return "a type name";
                case eTokenType::integer:       return "a number";
                case eTokenType::quote:         return "a string";
                case eTokenType::directive:     return "a directive";
                case eTokenType::symbol:        return "a symbol";
                case eTokenType::eof:           return "end of file";
                default:                        return "a token";
            }
        case PatternElement::Kind::AnyOf: {
            string s;
            for(size_t i = 0; i < elem.anyOfValues.size(); i++) {
                if(i > 0) s += (i == elem.anyOfValues.size()-1) ? " or " : ", ";
                s += "'" + elem.anyOfValues[i] + "'";
            }
            return s;
        }
        case PatternElement::Kind::Semantic:  return "a valid token";
        case PatternElement::Kind::Wildcard:  return "any token";
    }
    return "?";
}

GrammarMatch bglParser::matchGrammar(token& firstToken) {
    GrammarMatch result;

    // Phase 1: filter to candidates whose first element matches firstToken
    struct Candidate {
        int ruleIndex;
        int patternPos;  // how far into the pattern we've matched
    };
    vector<Candidate> alive;
    for(int i = 0; i < (int)grammarRules.size(); i++) {
        if(!grammarRules[i].pattern.empty() && matchElement(grammarRules[i].pattern[0], firstToken))
            alive.push_back({i, 1});
    }

    if(alive.empty()) return result;

    // Helper: populate result from a winning rule and consume tokens
    auto setWinner = [&](GrammarRule& winner, int tokensToConsume) {
        result.success = true;
        result.ruleName = winner.name;
        result.handler = winner.handler;
        result.matchedTokens.push_back(firstToken);
        for(int i = 0; i < tokensToConsume; i++)
            result.matchedTokens.push_back(file.getToken());
    };

    // Separate complete vs incomplete candidates
    // `bestComplete` tracks the best rule that has completed its pattern at some depth.
    // We preserve it across peek iterations so a shorter rule that completed early isn't lost
    // when a longer rule (still incomplete) gets eliminated in a later phase.
    struct BestComplete { int ruleIndex = -1; int depthAtComplete = 0; };
    BestComplete bestComplete;
    vector<Candidate> incomplete;
    for(auto& c : alive) {
        if(c.patternPos >= (int)grammarRules[c.ruleIndex].pattern.size()) {
            if(bestComplete.ruleIndex < 0) bestComplete = {c.ruleIndex, 0};
        } else {
            incomplete.push_back(c);
        }
    }

    // All complete, none incomplete — first rule wins (table order = priority)
    if(incomplete.empty() && bestComplete.ruleIndex >= 0) {
        setWinner(grammarRules[bestComplete.ruleIndex], 0);
        return result;
    }

    // Phase 2: peek ahead to disambiguate
    for(int peekDepth = 1; peekDepth <= 10; peekDepth++) {
        token peek = file.peekToken(peekDepth);

        vector<Candidate> stillAlive;
        for(auto& c : incomplete) {
            auto& rule = grammarRules[c.ruleIndex];
            if(c.patternPos < (int)rule.pattern.size() && matchElement(rule.pattern[c.patternPos], peek))
                stillAlive.push_back({c.ruleIndex, c.patternPos + 1});
            else
                result.failedCandidates.push_back({rule.name, c.patternPos});
        }

        incomplete.clear();
        for(auto& c : stillAlive) {
            if(c.patternPos >= (int)grammarRules[c.ruleIndex].pattern.size()) {
                // This candidate just completed — longer matches beat shorter ones, so it wins
                // over any earlier bestComplete (we've matched more tokens).
                bestComplete = {c.ruleIndex, peekDepth};
            } else {
                incomplete.push_back(c);
            }
        }

        // Single candidate remains — verify any remaining elements then consume
        if(stillAlive.size() == 1 && incomplete.size() == 1) {
            auto& cand = incomplete[0];
            auto& winner = grammarRules[cand.ruleIndex];
            int totalPeek = peekDepth;
            bool verified = true;
            while(cand.patternPos < (int)winner.pattern.size()) {
                totalPeek++;
                token p = file.peekToken(totalPeek);
                if(!matchElement(winner.pattern[cand.patternPos], p)) {
                    verified = false;
                    result.failedCandidates.push_back({winner.name, cand.patternPos});
                    break;
                }
                cand.patternPos++;
            }
            if(verified) { setWinner(winner, totalPeek); return result; }
            // Verification failed — fall through: bestComplete (if any) still wins
            break;
        }

        // No incomplete candidates left — bestComplete wins (if any)
        if(incomplete.empty()) break;
    }

    // Final: bestComplete wins if any rule completed during the walk
    if(bestComplete.ruleIndex >= 0) {
        setWinner(grammarRules[bestComplete.ruleIndex], bestComplete.depthAtComplete);
        return result;
    }

    return result;
}

// Grammar handler methods — standard GrammarHandler signature
bool bglParser::processEnum(vector<token>& t, Qualifiers& q, abstractObject&)
    { return processEnumDeclaration(t[0], q.isExtern, t[1]); }
bool bglParser::processClass(vector<token>& t, Qualifiers& q, abstractObject&)
    { return processClassDeclaration(t[0], q.isExtern, q.isExtend, q.isEmitter, q.isAlias, t[1]); }
bool bglParser::processGrammar(vector<token>& t, Qualifiers&, abstractObject&)
    { return processGrammarDeclaration(t[1]); }
bool bglParser::processArray(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processArrayDeclarationFromGeneric(t[0], q, c); }
bool bglParser::processRoutine(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processRoutineDeclaration(t[0], t[1], c, q.isExtern, q.isEmitter, q.isReplace); }
bool bglParser::processObject(vector<token>& t, Qualifiers& q, abstractObject&)
    { return processObjectDeclaration(t[0], t[1], q.isExtern, "", "", true, q.isEmitter); }
bool bglParser::processVariable(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processVariableDeclaration(t[0], t[1], t[2], c, q.isExtern, q.isConst); }
bool bglParser::processTypedObject(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processTypedObjectDeclaration(t[0], t[1], t[3], q, c); }
bool bglParser::processAliased(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processAliasedDeclaration(t[0], t[1], t[3], q, c); }

// ── Statement handlers (code-block scope) ───────────────────────────────────
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
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot return a value from void routine '{0}'", func->name));
    // Read the return expression — caller has consumed only the "return" keyword
    token first = file.getToken();
    expression* retExpr = parseExpression(first, {token::endStatement}, func, body);
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    rs.returnExpression = retExpr ? retExpr->text() : "";
    if(body != nullptr) body->statements.push_back(&rs);
    return false;
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
    ifStmt.thenBlock = new statementBlock();
    functionDef thenCtx;
    if(func != nullptr){ thenCtx.returnType = func->returnType; thenCtx.params = func->params; }
    thenCtx.body = ifStmt.thenBlock;
    token next = file.getToken();
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(thenCtx) == false){}
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
            openCompileContext(eCompileContext::codeBlock);
            while(processNextStatementV2(elseCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatement(elseNext, elseCtx);
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
    whileStmt.body = new statementBlock();
    functionDef whileCtx;
    if(func != nullptr){ whileCtx.returnType = func->returnType; whileCtx.params = func->params; }
    whileCtx.body = whileStmt.body;
    token next = file.getToken();
    loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(whileCtx) == false){}
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
        token nameTok = file.getToken(eTokenType::identifier);
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
            string incrText;
            tt = file.getToken();
            while(tt.isNot(token::parenClose)){
                if(!incrText.empty()) incrText += " ";
                incrText += tt.value;
                tt = file.getToken();
            }
            forStmt.incrementText = incrText;
            forStmt.body = new statementBlock();
            functionDef forCtx;
            if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
            forCtx.body = forStmt.body;
            token next = file.getToken();
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock);
                while(processNextStatementV2(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatementDispatch(next, forCtx);
            }
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
            string incrText;
            tt = file.getToken();
            while(tt.isNot(token::parenClose)){
                if(!incrText.empty()) incrText += " ";
                incrText += tt.value;
                tt = file.getToken();
            }
            forStmt.incrementText = incrText;
            forStmt.body = new statementBlock();
            functionDef forCtx;
            if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
            forCtx.body = forStmt.body;
            token next = file.getToken();
            loopDepth++;
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock);
                while(processNextStatementV2(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatementDispatch(next, forCtx);
            }
            loopDepth--;
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
        string incrText;
        tt = file.getToken();
        while(tt.isNot(token::parenClose)){
            if(!incrText.empty()) incrText += " ";
            incrText += tt.value;
            tt = file.getToken();
        }
        forStmt.incrementText = incrText;
        forStmt.body = new statementBlock();
        functionDef forCtx;
        if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
        forCtx.body = forStmt.body;
        token next = file.getToken();
        loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock);
            while(processNextStatementV2(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
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
        fi.isByteArray = (elemVarType == "char");
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
        loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock);
            while(processNextStatementV2(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        loopDepth--;
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
        loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock);
            while(processNextStatementV2(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        loopDepth--;
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
    fi.elementVar = elemVarName;
    fi.arrayVar   = arrName;
    fi.counterVar = counterName;
    fi.isByteArray = (arrElemType == "char");
    fi.body = new statementBlock();
    functionDef forCtx;
    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
    paramDef& elemParam = *(new paramDef());
    elemParam.name = elemVarName;
    elemParam.type = languageService.getType(elemVarType);
    forCtx.params.push_back(&elemParam);
    forCtx.body = fi.body;
    token next = file.getToken();
    loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processStatementDispatch(next, forCtx);
    }
    loopDepth--;
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
    openCompileContext(eCompileContext::codeBlock);
    while(processNextStatementV2(doCtx) == false){}
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
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(tryCtx) == false){}
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
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(catchCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    }
    if(body != nullptr) body->statements.push_back(&tcStmt);
    return false;
}

// Single handler for all directive rules — delegates to the existing processDirective switch.
bool bglParser::processDirectiveDispatch(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    return processDirective(t[0], ctx);
}

bool bglParser::processFunc(vector<token>& t, Qualifiers& q, abstractObject& ctx) {
    // Caller already consumed "func" "<" — parse the remaining <..., ...> body
    token typeTok = t[0];  // the "func" keyword
    string result = "func<";
    bool first = true;
    while(true){
        token tt = file.getToken({eTokenType::dataType, eTokenType::identifier});
        string typeName = tt.value;
        if(typeName == "func") typeName = parseFuncType();  // nested func<>
        if(!first) result += ",";
        result += typeName;
        first = false;
        token sep = file.getToken();
        if(sep.is(">")) break;
    }
    result += ">";
    typeTok.value = result;

    // Now continue like the dataType branch — read name and dispatch
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    string objectClassName;
    if(file.peekToken().is(":")) {
        file.getToken();
        objectClassName = file.getToken(eTokenType::dataType).value;
    }
    string i6alias;
    if(file.peekToken().is("as")) {
        file.getToken();
        token aliasTok = file.getToken(eTokenType::identifier);
        i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    }
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, name, ctx, q.isExtern, q.isEmitter, q.isReplace);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, name, q.isExtern, objectClassName, i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, name, symbol, ctx, q.isExtern, q.isConst, i6alias);
}

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

bool bglParser::processNextStatementV2(abstractObject& contextObject) {
    if(!grammarInitialized) initGrammarTable();
    token tok = file.getToken();
    return processStatementDispatch(tok, contextObject);
}

// Dispatches a single statement given a pre-read token. Used by processNextStatementV2
// for the main loop, and by statement handlers (if/while/for/etc.) for unbraced single-
// statement bodies like `if(x) doSomething();`.
bool bglParser::processStatementDispatch(token tok, abstractObject& contextObject) {
    if(!grammarInitialized) initGrammarTable();
    sourceLocation stmtLoc = file.currentLocation();

    // Early exits
    if(tok.is(token::braceClose)) return true;
    if(tok.is(eTokenType::eof)) {
        if(getCurrentCompileContext() == eCompileContext::codeBlock)
            parsingError("Unexpected end of file — missing closing '}'");
        return true;
    }
    // Directives flow through the grammar table (each #xxx is a rule).

    // Parse qualifiers
    Qualifiers q = parseQualifiers(tok);
    if(q.isExplicit && getCurrentCompileContext() == eCompileContext::global)
        parsingError("'explicit' is only valid inside a class or object body");
    if(q.isStatic && getCurrentCompileContext() == eCompileContext::global)
        parsingError("'static' is only valid inside a class body");

    // Static member access: ClassName.member — route to processStatement
    // Object instances no longer need special handling here because they're lexed as identifiers.
    if(!q.isExtern && getCurrentCompileContext() != eCompileContext::global && tok.isDataType()) {
        if(file.peekToken(1).is(token::period) || file.peekToken(1).is("?.")) {
            processStatement(tok, contextObject);
            return false;
        }
    }

    // Qualifier-dependent patterns — must be checked before the grammar match
    // because the grammar matcher consumes tokens and can't put them back
    if(q.isEmitter && (tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)) && file.peekToken().is(token::braceOpen))
        return processClassDeclaration(tok, false, q.isExtend, true, false, tok);

    if(q.isExtend && !q.isExtern && (tok.is(eTokenType::identifier) || tok.isDataType()) && !tok.is(token::classDeclaration))
        return processObjectExtension(tok);

    if(!q.isExtern && tok.is("grammar")) return processGrammarDeclaration();

    // Try grammar-driven matching
    GrammarMatch match = matchGrammar(tok);

    if(match.success && match.handler) {
        return (this->*match.handler)(match.matchedTokens, q, contextObject);
    }

    // ── FALLTHROUGH: grammar match failed ──
    // All declaration, statement, and directive patterns are in the grammar table.
    // Anything reaching here is either an expression statement (code block) or an error (global).

    if(getCurrentCompileContext() == eCompileContext::global) {
        // Grammar-improved error: report what patterns came closest
        if(!match.failedCandidates.empty()) {
            auto best = max_element(match.failedCandidates.begin(), match.failedCandidates.end(),
                [](const GrammarMatch::FailedCandidate& a, const GrammarMatch::FailedCandidate& b) {
                    return a.matchedUpTo < b.matchedUpTo;
                });
            if(best->matchedUpTo > 0) {
                for(auto& rule : grammarRules) {
                    if(rule.name == best->ruleName && best->matchedUpTo < (int)rule.pattern.size()) {
                        string expected = describeExpected(rule.pattern[best->matchedUpTo]);
                        parsingError(format("Near '{0}': expected {1} (in {2})", (string)tok, expected, best->ruleName));
                    }
                }
            }
        }
        parsingError(format("Illegal global identifier:'{0}'", (string)tok));
    }

    if(q.isExtern) parsingError(format("Extern declaration only valid for global variables, classes, routines, and enums:'{0}'", (string)tok));

    processStatement(tok, contextObject);
    return false;
}

//===============================================================================================================================
// Routines to parser larger blocks of the source code
//-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
#pragma region Parsing functions
// Returns true if the block contains a returnStatement on any path (used for lambda return-type inference).
static bool hasReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* is = dynamic_cast<ifStatement*>(s))
            if(hasReturn(is->thenBlock) || hasReturn(is->elseBlock)) return true;
        if(auto* ws = dynamic_cast<whileStatement*>(s))  if(hasReturn(ws->body))  return true;
        if(auto* ds = dynamic_cast<doStatement*>(s))     if(hasReturn(ds->body))  return true;
        if(auto* fs = dynamic_cast<forStatement*>(s))    if(hasReturn(fs->body))  return true;
        if(auto* sw = dynamic_cast<switchStatement*>(s))
            for(switchCase* c : sw->cases) if(hasReturn(c->body)) return true;
    }
    return false;
}

// Returns true if ALL execution paths through the block are guaranteed to return.
// Criteria:
//   - An unconditional returnStatement at the top level of the block
//   - An if-else where both the then-block and else-block all-paths-return
//   - A switch that has a default case and every case body all-paths-returns
// Loops (for/while/do) are NOT treated as guaranteed — the body may not execute.
static bool allPathsReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* is = dynamic_cast<ifStatement*>(s)){
            if(is->elseBlock != nullptr &&
               allPathsReturn(is->thenBlock) && allPathsReturn(is->elseBlock))
                return true;
        }
        if(auto* sw = dynamic_cast<switchStatement*>(s)){
            bool hasDefault = false;
            bool allReturn = true;
            for(switchCase* c : sw->cases){
                if(c->entries.empty()) hasDefault = true;  // default: case
                if(!allPathsReturn(c->body)) allReturn = false;
            }
            if(hasDefault && allReturn) return true;
        }
        // Loops do not guarantee return — body may not execute
    }
    return false;
}

bool bglParser::processEnumDeclaration(token tok, bool isExternal, token nameOverride){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration)) isBnum=true;
    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType}); //enum name
    tok=file.getToken(token::braceOpen);
    enumDef& newEnum=languageService.registerEnum((string)name, isExternal, name.originalValue);
    // If the pre-scanner already populated values, consume the body and return
    bool alreadyPopulated = !newEnum.namedValues.empty();
    int val=1;
    while(tok.isNot(token::braceClose)){
        tok=file.getToken();
        if(tok.is(token::braceClose)) break;
        if(alreadyPopulated){
            // Just drain remaining tokens until closing brace
            while(tok.isNot(token::braceClose)) tok=file.getToken();
            break;
        }
        enumValueDef& newVal=*new enumValueDef();
        newVal.name=tok.value;
        tok=file.getToken({token::braceClose, token::comma, token::assignment});
        if(tok.is(token::assignment)){
            token numTok=file.getToken(eTokenType::integer);
            val=stoi(numTok.value);
            if(isBnum && val != 0 && (val & (val - 1)) != 0)
                parsingError(format("bnum '{0}': explicit value {1} is not a power of 2", newEnum.dName(), val));
            tok=file.getToken({token::braceClose, token::comma});
        }
        newVal.value=val;
        if(isBnum)
            val<<=1;
        else
            val++;
        newEnum.namedValues.push_back(&newVal);
    }
    return false;
}

bool bglParser::processClassDeclaration(token tok, bool isExternal, bool isExtend, bool isEmitterClass, bool isAlias, token nameOverride){
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

    if(!isExtend){
        if(isEmitterClass) newClass.isEmitterClass = true;
        if(isAlias)        newClass.isAlias = true;
    }

    classDef* savedClass = currentClass;
    currentClass = &newClass;
    openCompileContext(eCompileContext::objectDef);

    // Inheritance clause:
    //   alias class requires 'for Parent'
    //   other classes use optional ': Parent [, Parent2 ...]'
    tok = file.getToken();
    if(isAlias && !tok.is("for"))
        parsingError(format("'alias {0}' requires a parent class: use 'alias {0} for ParentClass'", (string)nameTok));
    if(tok.is("for")){
        // alias class single-parent clause
        token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
        classDef* parent = dynamic_cast<classDef*>(&languageService.getType(parentTok.value));
        if(!parent) parsingError(format("Unknown base class '{0}'", parentTok.value));
        else newClass.baseClasses.push_back(parent);
        tok = file.getToken();
    } else if(tok.is(":")){
        do {
            token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            classDef* parent = dynamic_cast<classDef*>(&languageService.getType(parentTok.value));
            if(!parent) parsingError(format("Unknown base class '{0}'", parentTok.value));
            else newClass.baseClasses.push_back(parent);
            tok = file.getToken();
        } while(tok.is(","));
    }
    tok.assert(token::braceOpen);
    tok=file.getToken();
    while(tok.isNot(token::braceClose)){
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
        // Check for inherited member type inference: if the token is an identifier (not a data type)
        // followed by '=' or ';', look up the member name in base classes to infer the type.
        if(tok.is(eTokenType::identifier) && !tok.isDataType()){
            token peek = file.peekToken(1);
            if(peek.is(token::assignment) || peek.is(token::endStatement)){
                string memberName = tok.value;
                string inferredType;
                // Search base classes for a member with this name
                std::function<void(classDef*)> searchBases = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == memberName){ inferredType = vd->type.name; return; }
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
            funcDef.returnType=languageService.getType((string) returnType);
            funcDef.isEmitter=isEmitter;
            funcDef.isExplicit=isExplicitConversion;
            funcDef.isDefault=q.isDefault;
            if(isExplicitConversion && funcDef.name != "operator()")
                parsingError("'explicit' is only valid on conversion operators (operator())");
            processParameterList(funcDef);
            // operator auto: no params, no body, max one per class
            if(funcDef.name == "auto"){
                if(!funcDef.params.empty())
                    parsingError("operator auto() cannot have parameters");
                // Check for duplicate
                for(typeMember* m : newClass.members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == "auto")
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
                    rawblock.i6Body=file.getRawTextThroughClosingBrace();
                    funcDef.body=&rawblock;
                } else {
                    funcDef.body = new statementBlock();
                    functionDef* savedFunc = currentFunc;
                    currentFunc = &funcDef;
                    openCompileContext(eCompileContext::codeBlock);
                    while(processNextStatementV2(funcDef) == false){}
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
                    cerr << format("warning: replace '{0}' in class '{1}': no existing member with that signature\n", funcDef.name, newClass.name);
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
                    bool replacedExisting = false;
                    for(size_t i = 0; i < newClass.members.size(); i++){
                        functionDef* fd = dynamic_cast<functionDef*>(newClass.members[i]);
                        if(fd && fd->name == funcDef.name && fd->params.size() == funcDef.params.size()){
                            bool match = true;
                            for(size_t j=0; j<funcDef.params.size(); j++)
                                if(fd->params[j]->type.name != funcDef.params[j]->type.name){ match=false; break; }
                            // operator() can have multiple overloads with different return types (conversion operators)
                            if(match && funcDef.name == "operator()" && fd->returnType.name != funcDef.returnType.name)
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
                    // Check base class hierarchy for shadowed methods — warn if 'replace' not specified
                    if(!isReplace && !funcDef.name.empty()){
                        string shadowedFrom;
                        function<void(classDef*)> searchBases = [&](classDef* c){
                            if(!shadowedFrom.empty()) return;
                            for(typeMember* m : c->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == funcDef.name){ shadowedFrom = c->dName(); return; }
                            for(classDef* base : c->baseClasses) searchBases(base);
                        };
                        for(classDef* base : newClass.baseClasses) searchBases(base);
                        if(!shadowedFrom.empty())
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
            funcDef.returnType = languageService.getType((string)returnType);
            funcDef.isEmitter = true;
            funcDef.isValueEmitter = true;
            funcDef.isDefault = q.isDefault;
            funcDef.src = file.currentLocation();
            i6Block& rawblock = *(new i6Block());
            rawblock.i6Body = file.getRawTextThroughClosingBrace();
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
            if(newClass.isAlias && tok.is(token::assignment))
                parsingError(format("Member '{0}' in an alias class may not have a definition (value); declarations only", (string)name));
            variableDeclaration& varDef=*(new variableDeclaration());
            varDef.name=(string) name;
            varDef.type=languageService.getType((string) returnType);
            if(isMemberConst) varDef.isConst = true;
            varDef.isStatic = isMemberStatic;
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
                    cerr << format("warning: replace '{0}' in class '{1}': no existing member with that name\n", varDef.name, newClass.name);
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
    currentClass = savedClass;
    closeCompileContext(eCompileContext::objectDef);
    return false;
}
// Reads <ReturnType, ParamType, ...> from stream and returns the full "func<...>" type string.
// Called after the "func" token has been consumed.
string bglParser::parseFuncType(){
    file.getToken("<");
    string result = "func<";
    bool first = true;
    while(true){
        token t = file.getToken({eTokenType::dataType, eTokenType::identifier});
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        if(!first) result += ",";
        result += typeName;
        first = false;
        token sep = file.getToken();
        if(sep.is(">")) break;
        // else sep is "," — continue
    }
    result += ">";
    return result;
}

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

    // Set up outer scope context for capture detection
    functionDef* savedOuterFunc = lambdaOuterFunc;
    statementBlock* savedOuterBody = lambdaOuterBody;
    lambdaOuterFunc = outerFunc;
    lambdaOuterBody = outerBody;

    token bodyStart = file.getToken();
    if(bodyStart.is(token::braceOpen)){
        // Block body
        functionDef* savedFunc = currentFunc;
        currentFunc = &fd;
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(fd) == false){}
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        fd.returnType.name = hasReturn(lambdaBody) ? "var" : "void";
    } else {
        // Single-expression body (terminates at ';').
        // Only works cleanly when ';' is the terminator (e.g. assignment RHS).
        // For use as a function argument, use block syntax: () => { return expr; }
        expression* retExpr = parseExpression(bodyStart, {token::endStatement}, &fd, lambdaBody);
        returnStatement& ret = *(new returnStatement());
        ret.src = fd.src;
        ret.returnExpression = retExpr->text();
        lambdaBody->statements.push_back(&ret);
        fd.returnType.name = retExpr->resolvedType.empty() ? "var" : retExpr->resolvedType;
    }

    // Restore outer scope context
    lambdaOuterFunc = savedOuterFunc;
    lambdaOuterBody = savedOuterBody;

    // Emit capture globals — insert at front so they appear before functions in the I6 output
    for(auto& cap : fd.captures){
        variableDeclaration& capGlobal = *(new variableDeclaration());
        capGlobal.name = cap.globalName;
        capGlobal.type.name = cap.typeName;
        languageService.globals.insert(languageService.globals.begin(), &capGlobal);
    }

    // Lift: append to globals — I6 doesn't require routines to precede call sites,
    // and inserting at front would place lambdas before _bgl_temp (I6 compile error)
    languageService.globals.push_back(&fd);

    // If there are captures, emit copy assignments before the lambda name is used.
    // These are injected as pending injections so they execute before the enclosing statement.
    for(auto& cap : fd.captures){
        i6RawNode& assign = *(new i6RawNode());
        assign.text = cap.globalName + " = " + cap.outerName + ";";
        pendingInjections.push_back(&assign);
    }

    return fd.name;
}

bool bglParser::processParameterList(functionDef& funcDef){
    token tok=file.getToken(); // first type, or ")" for empty list
    while(tok.isNot(token::parenClose)){
        paramDef& param=*new paramDef();
        tok.assertDataType();
        string paramTypeName = tok.value;
        if(paramTypeName == "func") paramTypeName = parseFuncType();
        else if(paramTypeName == "array") {
            // consume <ElementType> and store full "array<ElementType>" for for-in/type-compat resolution
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            // array<char> maps to bytearray (byte-access operators); others keep array<T> format
            if(elemType == "char" || elemType == "charliteral")
                paramTypeName = "bytearray";
            else
                paramTypeName = format("array<{0}>", elemType);
        }
        param.type=languageService.getType(paramTypeName);
        if(param.type.name.empty()) param.type.name = paramTypeName; // for func<...> types
        tok=file.getToken(); // name, "=", ",", or ")"
        // Accept both identifier and dataType tokens for the parameter name. A dataType here
        // means the name collides with a registered class (e.g., parameter 'b' when 'class B'
        // exists). The parameter declaration is the authoritative use of the name in the
        // function's scope, so we allow the shadow.
        if(tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)){
            param.name=(string) tok;
            param.displayName=tok.originalValue;
            // Disallow parameter names that shadow a global, a class member, or an object member.
            // Emitters are skipped: their parameter names are template substitution keys, not I6 locals.
            if(!funcDef.isEmitter){
                for(typeDef* g : languageService.globals)
                    if(g->name == param.name)
                        parsingError("Parameter '" + param.name + "' shadows global variable of the same name. Rename the parameter.");
                if(currentClass != nullptr)
                    for(typeMember* m : currentClass->members)
                        if(m->name == param.name)
                            parsingError("Parameter '" + param.name + "' shadows a member of class '" + currentClass->name + "'. Rename the parameter.");
                if(currentObject != nullptr)
                    for(typeMember* m : currentObject->members)
                        if(m->name == param.name)
                            parsingError("Parameter '" + param.name + "' shadows a member of object '" + currentObject->name + "'. Rename the parameter.");
            }
            tok=file.getToken(); // "=", ",", or ")"
        }
        if(tok.is(token::assignment)){
            expression* defExpr = parseExpression(file.getToken(), {token::comma, token::parenClose}, nullptr, nullptr);
            param.defaultValue = defExpr->text();
            funcDef.params.push_back(&param);
            if(defExpr->terminator == token::parenClose) break; // ")" consumed by parseExpression
            tok=file.getToken(); // terminator was ","; read next param's type
        } else {
            tok.assertOneOf({token::comma, token::parenClose});
            funcDef.params.push_back(&param);
            if(tok.is(token::parenClose)) break;
            tok=file.getToken(); // tok was ","; read next param's type
        }
    }
    return false;
}

// Depth-first search through a class and its base classes (first-listed wins).
// Returns the first typeMember* for which pred returns true, or nullptr.
typeMember* bglParser::findMemberInHierarchy(classDef* cls, std::function<bool(typeMember*)> pred){
    for(typeMember* m : cls->members)
        if(pred(m)) return m;
    for(classDef* base : cls->baseClasses){
        typeMember* found = findMemberInHierarchy(base, pred);
        if(found) return found;
    }
    return nullptr;
}

// Unified method resolution. Searches:
//   1. Class hierarchy (via typeName → classDef)
//   2. ObjectDef instance members (via objPath in globals, or currentObject if objPath is "self")
// Handles: default parameters, var-fallback, pre-scan stub name-only matching.
bglParser::MethodMatch bglParser::resolveMethod(const string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args){
    MethodMatch result;
    functionDef* varFallback = nullptr;

    // Helper: check one function definition against the method name and args
    auto checkMethod = [&](functionDef* fd) {
        if(fd->name != methodName) return;
        if(!result.nameMatch) result.nameMatch = fd;
        result.nameFound = true;

        // Pre-scan stubs have no params — match by name only
        if(fd->isPrePassStub){ result.method = fd; return; }

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
    classDef* cls = dynamic_cast<classDef*>(&languageService.getType(typeName));
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

    // Step 2: fallback — search objectDef's own members
    if(!result.method){
        string lowerPath = objPath;
        transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        objectDef* targetObj = nullptr;
        if(lowerPath == "self" && currentObject != nullptr)
            targetObj = currentObject;
        // Also check when typeName is "object" and we're inside an object body
        else if(typeName == "object" && currentObject != nullptr)
            targetObj = currentObject;
        else
            for(typeDef* g : languageService.globals)
                if(g->name == lowerPath)
                    if(auto* od = dynamic_cast<objectDef*>(g)){ targetObj = od; break; }
        if(targetObj != nullptr){
            varFallback = nullptr;
            for(typeMember* m : targetObj->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    checkMethod(fd);
            if(!result.method) result.method = varFallback;
        }
    }

    return result;
}

// Replace a pre-scan stub in a member list with the real definition.
bool bglParser::replaceStubMember(vector<typeMember*>& members, functionDef& newDef){
    for(auto it = members.begin(); it != members.end(); ++it)
        if(auto* fd = dynamic_cast<functionDef*>(*it))
            if(fd->name == newDef.name && fd->isPrePassStub){
                *it = &newDef;
                return true;
            }
    return false;
}

std::string bglParser::resolveIdentifierType(std::string name, functionDef* func, statementBlock* body){
    if(name == "null") return "object";
    if(name == "self"){
        if(currentObject != nullptr)
            return currentObject->objectClass ? currentObject->objectClass->name : "object";
        if(currentClass != nullptr)
            return currentClass->name;
        return "object";
    }
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return p->type.name;
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->type.name;
    // Tier 1c: locals in the enclosing function's outer body (mirrors qualifyIdentifier)
    if(currentFunc != nullptr && currentFunc->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(currentFunc->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
    }
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
    if(currentClass != nullptr)
        for(typeMember* m : currentClass->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
    string enumType = languageService.getEnumType(name);
    if(!enumType.empty()) return enumType;
    for(typeDef* g : languageService.globals){
        if(g->name == name){
            if(auto* vd = dynamic_cast<variableDeclaration*>(g)) return vd->type.name;
            if(auto* fd = dynamic_cast<functionDef*>(g)) return fd->returnType.name;
            if(auto* od = dynamic_cast<objectDef*>(g))
                return od->objectClass ? od->objectClass->name : "object"; // use declared class for method dispatch
        }
    }
    // Action constants (extern verb Take etc.) → type "verb"
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name) return "verb";
    }
    // Global emitter objects (emitter bglStyle { }) → resolve to their own class name
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject || cd->isEmitterClass)
                return name;
    }
    // Compile-time symbols (#define): numeric values resolve as intliteral, others as stringliteral
    {
        auto it = definedSymbols.find(name);
        if(it != definedSymbols.end() && !it->second.empty()){
            bool isNumeric = !it->second.empty() && (isdigit(it->second[0]) || (it->second[0] == '-' && it->second.size() > 1));
            return isNumeric ? "intliteral" : "stringliteral";
        }
    }
    // #using imports: search imported scopes' members
    for(classDef* imp : usingImports)
        for(typeMember* m : imp->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == name) return fd->returnType.name;
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
        }

    // Closure capture: check outer function's scope for type
    if(lambdaOuterFunc != nullptr){
        for(paramDef* p : lambdaOuterFunc->params)
            if(p->name == name) return p->type.name;
        statementBlock* outerBody = lambdaOuterBody ? lambdaOuterBody
            : dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
        if(outerBody != nullptr)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
    }
    return "";
}

// Returns the I6-qualified form of an identifier based on scope:
//   - found in local params/vars → name (unqualified)
//   - found in current object's members → "self.name"
//   - found in enum values or globals → name (unqualified)
//   - not found → "" (caller should report an error)
// Whole-word string replacement: replaces occurrences of 'from' in 'str' with 'to',
// only when bounded by non-identifier characters (respects word boundaries).
// Used throughout emitter body substitution ($self, $prop, parameter names).
static string replaceWord(string str, const string& from, const string& to){
    size_t pos = 0;
    while((pos = str.find(from, pos)) != string::npos){
        bool leftOk  = pos == 0 || !(isalnum(str[pos-1]) || str[pos-1] == '_' || str[pos-1] == '$');
        bool rightOk = pos + from.size() >= str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()] == '_');
        if(leftOk && rightOk){ str.replace(pos, from.size(), to); pos += to.size(); }
        else pos += from.size();
    }
    return str;
}

// Check if a variable is declared const (local, global, or class member).
static bool isConstVariable(const string& name, functionDef* func, statementBlock* body){
    // Check locals
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->isConst;
    // Check enclosing function's outer body
    if(func != nullptr && func->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(func->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->isConst;
    }
    // Check globals
    for(typeDef* g : languageService.globals)
        if(auto* vd = dynamic_cast<variableDeclaration*>(g))
            if(vd->name == name) return vd->isConst;
    return false;
}

// Look up a type's display name (original casing) from the language service.
static string typeDisplayName(const string& typeName){
    typeDef& td = languageService.getType(typeName);
    if(&td != &emptyTDef && !td.displayName.empty()) return td.displayName;
    return typeName;
}

// Format a function signature for error messages: "name(type1 p1, type2 p2) → returnType"
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
bglParser::ParsedArgList bglParser::parseCallArgList(functionDef* func, statementBlock* body){
    ParsedArgList result;
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
            if(arg->terminator == token::parenClose) break;
            firstArgTok = file.getToken();
        }
    }
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
    if(gcm.nameMatch == nullptr)
        parsingError(format("Undeclared function '{0}'", funcName));
    string dispName = gcm.nameMatch ? gcm.nameMatch->dName() : funcName;
    if(gcm.arityMatch == nullptr){
        size_t req = 0; for(paramDef* p : gcm.nameMatch->params) if(p->defaultValue.empty()) req++;
        size_t tot = gcm.nameMatch->params.size();
        parsingError(format("Function '{0}' expects {1} argument(s), but {2} were supplied.\n  Expected: {3}",
            dispName,
            (req == tot) ? to_string(tot) : to_string(req) + "-" + to_string(tot),
            argCount, formatSignature(gcm.nameMatch)));
    }
    if(gcm.match == nullptr)
        parsingError(format("No overload of function '{0}' accepts these argument types", dispName));
    return gcm.match->returnType.name;
}

std::string bglParser::qualifyIdentifier(std::string name, functionDef* func, statementBlock* body){
    if(name == "null") return "nothing";
    if(name == "self") return "self";
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
    // Tier 1a: params of current (possibly nested) context
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return name;
    // Tier 1b: locals in current block
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return name;
    // Tier 1c: locals in the enclosing function's body (when inside a nested block like if/while)
    if(currentFunc != nullptr && currentFunc->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(currentFunc->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return name;
    }
    // Tier 2: current object/class members (variables and methods) → qualify with self
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(m->name == name)
                if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                    return "self." + name;
    if(currentClass != nullptr)
        for(typeMember* m : currentClass->members)
            if(m->name == name)
                if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                    return "self." + name;
    // Tier 3: enum values and globals
    if(!languageService.getEnumType(name).empty()){
        string enumTypeName = languageService.getEnumType(name);
        enumDef* ed = dynamic_cast<enumDef*>(&languageService.getType(enumTypeName));
        if(ed && ed->isExternal) return name;   // extern enum values (e.g. true/false) emit as-is
        return "_" + enumTypeName + "_" + name;
    }
    for(typeDef* g : languageService.globals){
        if(g->name == name){
            // Warn if a #using import is shadowed by this global
            for(classDef* imp : usingImports)
                for(typeMember* m : imp->members)
                    if(m->name == name){
                        parsingWarning(format("global '{0}' shadows imported '{1}.{0}'; use '{1}.{0}' to access the import",
                            name, imp->dName()));
                        goto globalResolved;  // warn once, then proceed with global
                    }
            globalResolved:
            // Value emitter: expand body inline instead of emitting identifier
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string body = processBglConditionals(blk->i6Body);
                        size_t s = body.find_first_not_of(" \t\n\r"); if(s != string::npos) body = body.substr(s);
                        size_t e = body.find_last_not_of(" \t\n\r;"); if(e != string::npos) body = body.substr(0, e+1);
                        return body;
                    }
                }
            }
            return g->i6name.empty() ? name : g->i6name;
        }
    }
    // Tier 4: #using imports — search imported scopes' members (with ambiguity check)
    {
        classDef* matchedImport = nullptr;
        typeMember* matchedMember = nullptr;
        for(classDef* imp : usingImports){
            for(typeMember* m : imp->members){
                bool isMatch = false;
                if(auto* fd = dynamic_cast<functionDef*>(m)) isMatch = (fd->name == name);
                else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) isMatch = (vd->name == name);
                if(isMatch){
                    if(matchedImport && matchedImport != imp)
                        parsingError(format("'{0}' is ambiguous — found in both '{1}' and '{2}'; qualify explicitly",
                            name, matchedImport->dName(), imp->dName()));
                    matchedImport = imp;
                    matchedMember = m;
                }
            }
        }
        if(matchedImport && matchedMember){
            if(auto* fd = dynamic_cast<functionDef*>(matchedMember)){
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = i6Emitter::replaceWord(b, "$self", matchedImport->name);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        return b;
                    }
                }
                return matchedImport->name + "." + name;
            }
            return matchedImport->name + "." + name;
        }
    }

    // Tier 5: verb names (action constants and verb variables)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name) return vd->name;
    }
    // Tier 5: global emitter objects (emitter bglStyle { }) — no instance, qualify as-is
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject || cd->isEmitterClass)
                return name;
    }
    // Tier 6: compile-time symbols (#define) — substitute the literal value inline
    {
        auto it = definedSymbols.find(name);
        if(it != definedSymbols.end() && !it->second.empty())
            return it->second;
    }
    // Tier 7: closure capture — inside a lambda, check the outer function's scope
    if(lambdaOuterFunc != nullptr && currentFunc != nullptr){
        // Check outer function's params
        for(paramDef* p : lambdaOuterFunc->params)
            if(p->name == name){
                return addCapture(name, p->type.name);
            }
        // Check outer function's body locals
        statementBlock* outerBody = lambdaOuterBody ? lambdaOuterBody
            : dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
        if(outerBody != nullptr)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name)
                        return addCapture(name, vd->type.name);
    }
    return "";
}

// Register a closure capture: creates a global variable for the captured value.
// Returns the global name. Deduplicates: if the same outer variable is captured
// multiple times in the same lambda, returns the existing global.
string bglParser::addCapture(const string& outerName, const string& typeName){
    // Check if already captured in this lambda
    for(auto& cap : currentFunc->captures)
        if(cap.outerName == outerName) return cap.globalName;
    string globalName = format("_bglCap{0}", languageService.captureCounter++);
    currentFunc->captures.push_back({outerName, globalName, typeName});
    return globalName;
}

bool bglParser::isTypeCompatible(std::string argType, std::string paramType){
    if(paramType == "var") return true;  // var accepts any type without checking
    if(argType == "var") return true;    // var is assignable to any type (untyped source)
    if(argType == paramType) return true;
    // func<...> compatibility: a func value is compatible with any func<...> param type
    if(argType == "func" && paramType.rfind("func<", 0) == 0) return true;
    if(argType.rfind("func<", 0) == 0 && paramType.rfind("func<", 0) == 0) return true;
    // array<T> compatibility: array is compatible with array<T> param
    if(argType == "array" && paramType.rfind("array<", 0) == 0) return true;
    if(argType.rfind("array<", 0) == 0 && paramType.rfind("array<", 0) == 0) return true;
    // Object subtyping is handled by the class hierarchy check below —
    // only classes that actually inherit from 'object' are compatible with it.
    // Class hierarchy: argType is compatible with paramType if argType inherits from paramType
    {
        classDef* argCls2 = dynamic_cast<classDef*>(&languageService.getType(argType));
        classDef* paramCls2 = dynamic_cast<classDef*>(&languageService.getType(paramType));
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
        classDef* argCls3  = dynamic_cast<classDef*>(&languageService.getType(argType));
        classDef* paramCls3 = dynamic_cast<classDef*>(&languageService.getType(paramType));
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
    classDef* cls = dynamic_cast<classDef*>(&languageService.getType(paramType));
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
    classDef* argCls = dynamic_cast<classDef*>(&languageService.getType(argType));
    if(argCls != nullptr && findMemberInHierarchy(argCls, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty() && !fn->isExplicit && fn->returnType.name == paramType;
    })) return true;
    return false;
}

// Reorder named arguments to match parameter positions. Validates that all named args match valid parameter names.
void reorderNamedArgs(functionCallStatement& cs, functionDef* fd, function<bool(string)> errorFn){
    bool hasNamed = false;
    for(auto& n : cs.namedArgNames) if(!n.empty()){ hasNamed = true; break; }
    if(!hasNamed) return;
    // Build reordered args vector aligned to parameter positions
    vector<expression*> reordered(fd->params.size(), nullptr);
    vector<string> reorderedNames(fd->params.size());
    vector<vector<interpolatedSegment>> reorderedInterp(fd->params.size());
    // First pass: place positional args (unnamed) in order
    size_t positionalIdx = 0;
    for(size_t i = 0; i < cs.args.size(); i++){
        if(cs.namedArgNames[i].empty()){
            while(positionalIdx < reordered.size() && reordered[positionalIdx] != nullptr) positionalIdx++;
            if(positionalIdx >= fd->params.size()) { errorFn("Too many positional arguments"); return; }
            reordered[positionalIdx] = cs.args[i];
            if(i < cs.interpSegmentsPerArg.size()) reorderedInterp[positionalIdx] = cs.interpSegmentsPerArg[i];
            positionalIdx++;
        }
    }
    // Second pass: place named args by matching parameter name
    for(size_t i = 0; i < cs.args.size(); i++){
        if(!cs.namedArgNames[i].empty()){
            bool found = false;
            for(size_t p = 0; p < fd->params.size(); p++){
                if(fd->params[p]->name == cs.namedArgNames[i]){
                    if(reordered[p] != nullptr){ errorFn(format("Parameter '{0}' specified more than once", cs.namedArgNames[i])); return; }
                    reordered[p] = cs.args[i];
                    if(i < cs.interpSegmentsPerArg.size()) reorderedInterp[p] = cs.interpSegmentsPerArg[i];
                    found = true; break;
                }
            }
            if(!found){ errorFn(format("No parameter named '{0}'", cs.namedArgNames[i])); return; }
        }
    }
    // Replace args with reordered; leave nullptr slots for defaults to fill in later
    cs.args.clear();
    cs.interpSegmentsPerArg.clear();
    for(size_t i = 0; i < reordered.size(); i++){
        if(reordered[i] != nullptr){
            cs.args.push_back(reordered[i]);
            cs.interpSegmentsPerArg.push_back(reorderedInterp[i]);
        } else if(!fd->params[i]->defaultValue.empty()){
            // Leave a gap — the default-fill logic that follows will handle it
            // Actually, we need to push something to maintain positional alignment
            expression* defExpr = new expression();
            defExpr->tokens.push_back(fd->params[i]->defaultValue);
            cs.args.push_back(defExpr);
            cs.interpSegmentsPerArg.push_back({});
        } else {
            errorFn(format("Required parameter '{0}' not provided", fd->params[i]->name)); return;
        }
    }
    cs.namedArgNames.clear();
}

void bglParser::applyArgConversions(std::vector<expression*>& args, functionDef* fd){
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++){
        string argType = args[i]->resolvedType;
        string paramType = fd->params[i]->type.name;
        if(paramType == "var" || argType == paramType || argType.empty()) continue;
        // Look for conversion operator on arg's class
        classDef* argCls = dynamic_cast<classDef*>(&languageService.getType(argType));
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
                string argText = args[i]->text();
                size_t pos = 0;
                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
                args[i]->tokens.clear();
                args[i]->tokens.push_back(b);
                args[i]->resolvedType = paramType;
            }
        }
    }
}

// Parses the segments of an interpolated string from the live stream.
// Assumes the '$' token has already been consumed; consumes the opening '"', segments, and closing '"'.
vector<interpolatedSegment> bglParser::parseInterpolatedSegments(functionDef* func, statementBlock* body){
    file.readChar();  // consume opening '"'
    vector<interpolatedSegment> segments;
    string currentStr;
    char c = file.readChar();
    while(c != '"' && c != EOF) {
        if(c == '\\') {
            char nc = file.readChar();
            if     (nc == 'n')  currentStr += '^';      // \n  → I6 newline
            else if(nc == '"')  currentStr += '~';      // \"  → I6 double-quote
            else if(nc == '\\') currentStr += "@@92";   // \\  → literal backslash
            else if(nc == '@')  currentStr += "@@64";   // \@  → literal at-sign
            else if(nc == '{')  currentStr += '{';       // \{  → literal brace (not an expression)
            else if(nc == '$') {                          // \$XX → @{XX} (hex character code)
                string hex;
                while(isxdigit(file.peekChar())) { hex += file.readChar(); }
                currentStr += "@{" + hex + "}";
            }
            else if(isdigit(nc)) {                        // \NNN → @{hex} (decimal character code)
                string dec; dec += nc;
                while(isdigit(file.peekChar())) { dec += file.readChar(); }
                char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "%X", stoi(dec));
                currentStr += "@{"; currentStr += hexBuf; currentStr += "}";
            }
            // ── Diacritical accent shorthands (same as fileLexer.cpp) ──
            // \^^ = forced literal caret, \~~ = forced literal tilde.
            else if(nc == '^') {
                char xc = file.peekChar();
                if(xc == '^') { file.readChar(); currentStr += "@@94"; }
                else if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@^"; currentStr += xc;
                } else { currentStr += "@@94"; }
            }
            else if(nc == '~') {
                char xc = file.peekChar();
                if(xc == '~') { file.readChar(); currentStr += "@@126"; }
                else if(string("anoANO").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@~"; currentStr += xc;
                } else { currentStr += "@@126"; }
            }
            else if(nc == '\'') {
                char xc = file.peekChar();
                if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@'"; currentStr += xc;
                } else { currentStr += '\''; }
            }
            else if(nc == '`') {
                char xc = file.peekChar();
                if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@`"; currentStr += xc;
                } else { currentStr += '`'; }
            }
            else if(nc == ':') {
                char xc = file.peekChar();
                if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@:"; currentStr += xc;
                } else { currentStr += ':'; }
            }
            else if(nc == '/') {
                char xc = file.peekChar();
                if(string("oO").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@\\"; currentStr += xc;
                } else { currentStr += '/'; }
            }
            else if(nc == 'c') {
                char xc = file.peekChar();
                if(xc == 'c' || xc == 'C') {
                    file.readChar(); currentStr += "@c"; currentStr += xc;
                } else { currentStr += 'c'; }
            }
            else if(nc == 'o') {
                char xc = file.peekChar();
                if(xc == 'a' || xc == 'A') {
                    file.readChar(); currentStr += "@o"; currentStr += xc;
                } else { currentStr += 'o'; }
            }
            else if(nc == 's' && file.peekChar()=='s') { file.readChar(); currentStr += "@ss"; }
            else if(nc == 'a' && file.peekChar()=='e') { file.readChar(); currentStr += "@ae"; }
            else if(nc == 'A' && file.peekChar()=='E') { file.readChar(); currentStr += "@AE"; }
            else if(nc == 'O' && file.peekChar()=='E') { file.readChar(); currentStr += "@OE"; }
            else if(nc == 't' && file.peekChar()=='h') { file.readChar(); currentStr += "@th"; }
            else if(nc == 'e' && file.peekChar()=='t') { file.readChar(); currentStr += "@et"; }
            else if(nc == 'L' && file.peekChar()=='L') { file.readChar(); currentStr += "@LL"; }
            else if(nc == '!' && file.peekChar()=='!') { file.readChar(); currentStr += "@!!"; }
            else if(nc == '?' && file.peekChar()=='?') { file.readChar(); currentStr += "@??"; }
            else if(nc == '<' && file.peekChar()=='<') { file.readChar(); currentStr += "@<<"; }
            else if(nc == '>' && file.peekChar()=='>') { file.readChar(); currentStr += "@>>"; }
            else { currentStr += '\\'; currentStr += nc; }
        } else if(c == '{') {
            if(!currentStr.empty()) {
                interpolatedSegment seg;
                seg.isExpr = false;
                seg.text = currentStr;
                segments.push_back(seg);
                currentStr = "";
            }
            token exprFirst = file.getToken();
            expression* exprNode = parseExpression(exprFirst, {"}"}, func, body);
            interpolatedSegment seg;
            seg.isExpr = true;
            seg.expr = exprNode;
            seg.injections = pendingInjections;
            pendingInjections.clear();
            segments.push_back(seg);
        } else {
            currentStr += c;
        }
        c = file.readChar();
    }
    if(!currentStr.empty()) {
        interpolatedSegment seg;
        seg.isExpr = false;
        seg.text = currentStr;
        segments.push_back(seg);
    }
    return segments;
}

// Binary operator resolution: read RHS, find matching emitter, inline.
// Returns true if handled; false if the operator should pass through as raw I6.
bool bglParser::applyBinaryOperator(expression* expr, const string& opName, classDef* cls,
    const vector<string>& terminators, int parenDepth,
    function<token()> getNext, optional<token>& prefetched,
    functionDef* func, statementBlock* body)
{

    // Step 1: Read RHS token and determine its type and text
    bool isLowPrecedence = (opName == "&&" || opName == "||");
    token rhs = getNext();
    string rhsType, rhsText;

    if(isLowPrecedence){
        vector<string> rhsTerminators = terminators;
        rhsTerminators.push_back("&&");
        rhsTerminators.push_back("||");
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
        token terminatorTok; terminatorTok.value = rhsExpr->terminator;
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
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
        token terminatorTok; terminatorTok.value = rhsExpr->terminator;
        prefetched = terminatorTok;
        rhsType = rhsExpr->resolvedType;
        rhsText = rhsExpr->text();
    }
    else if(rhs.is(eTokenType::name)) {
        rhsType = resolveIdentifierType(rhs.value, func, body);
        rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value, func, body) : rhs.value;
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
    functionDef* matchedOp = nullptr;
    if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* m){
        auto* opFn = dynamic_cast<functionDef*>(m);
        if(!opFn || opFn->name != opName) return false;
        // Pre-scan stubs have no params — match by name only
        if(opFn->isPrePassStub) return true;
        return !opFn->params.empty() &&
               (rhsType.empty() || opFn->params[0]->type.name==rhsType || opFn->params[0]->type.name=="var");
    })) matchedOp = dynamic_cast<functionDef*>(m);

    // LHS conversion fallback: if LHS has operator() → convertedType, retry operator search on that type
    if(!matchedOp){
        for(typeMember* m : cls->members){
            auto* convFn = dynamic_cast<functionDef*>(m);
            if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
            string convertedType = convFn->returnType.name;
            classDef* convCls = dynamic_cast<classDef*>(&languageService.getType(convertedType));
            if(!convCls) continue;
            if(typeMember* m2 = findMemberInHierarchy(convCls, [&](typeMember* m){
                auto* opFn = dynamic_cast<functionDef*>(m);
                if(!opFn || opFn->name != opName) return false;
                if(opFn->isPrePassStub) return true;
                return !opFn->params.empty() &&
                       (rhsType.empty() || opFn->params[0]->type.name==rhsType || opFn->params[0]->type.name=="var"
                        || opFn->params[0]->type.name==convertedType);
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
        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhsType));
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
                        if(!convBody.empty())
                            rhsText = i6Emitter::replaceWord(convBody, "$self", rhsText);
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
        // For identifier RHS, check if it's a function call and collect full text
        if(rhs.is(eTokenType::name)){
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
        b = replaceWord(b, "$self", lhsText);
        expr->tokens.clear();
        for(auto& p : prefix) expr->tokens.push_back(p);
        expr->tokens.push_back(b);
    } else if(matchedOp && !matchedOp->isEmitter && !matchedOp->isPrePassStub){
        // Non-emitter operator: emit as a method call on the LHS using a mangled property name
        string lhsText = expr->text();
        // Build a safe I6 property name from the operator symbol
        if(matchedOp->i6name.empty()){
            string safe = "_op";
            for(char ch : matchedOp->name){
                if(ch == '=') safe += "eq";
                else if(ch == '~') safe += "tilde";
                else if(ch == '<') safe += "lt";
                else if(ch == '>') safe += "gt";
                else if(ch == '!') safe += "ne";
                else if(ch == '+') safe += "add";
                else if(ch == '-') safe += "sub";
                else if(ch == '*') safe += "mul";
                else if(ch == '/') safe += "div";
                else if(ch == '%') safe += "mod";
                else if(ch == '&') safe += "and";
                else if(ch == '|') safe += "or";
                else if(ch == '^') safe += "xor";
                else if(ch == '?') safe += "qry";
                else if(ch != ' ') safe += ch;
            }
            matchedOp->i6name = safe;
        }
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

// ── Shared: parse qualifier keywords in any order ────────────────────────────
// Consumes qualifier tokens from the stream; leaves tok at the first non-qualifier.
// Validates nonsensical combinations after all qualifiers are collected.
Qualifiers bglParser::parseQualifiers(token& tok){
    Qualifiers q;
    while(true){
        if     (tok.is(token::replace))            { q.isReplace  = true; tok = file.getToken(); }
        else if(tok.is("explicit"))                { q.isExplicit = true; tok = file.getToken(); }
        else if(tok.is(token::external))           { q.isExtern   = true; tok = file.getToken(); }
        else if(tok.is("emitter"))                 { q.isEmitter  = true; tok = file.getToken(); }
        else if(tok.is(token::constantDeclararion)){ q.isConst    = true; tok = file.getToken(); }
        else if(tok.is("static"))                  { q.isStatic   = true; tok = file.getToken(); }
        else if(tok.is(token::extend))             { q.isExtend   = true; tok = file.getToken(); }
        else if(tok.is("alias"))                   { q.isAlias    = true; tok = file.getToken(); }
        else if(tok.is("default"))                 { q.isDefault  = true; tok = file.getToken(); }
        else break;
    }
    // Validate nonsensical combinations
    if(q.isConst && q.isStatic)
        parsingError("A member cannot be both 'const' and 'static'");
    if(q.isStatic && q.isEmitter)
        parsingError("A static member cannot be an emitter");
    if(q.isExplicit && (q.isConst || q.isStatic))
        parsingError("'explicit' cannot be combined with 'const' or 'static'");
    if(q.isExtern && q.isEmitter && q.isExtend)
        parsingError("'extern', 'emitter', and 'extend' cannot all be combined");
    if(q.isAlias && q.isExtern)
        parsingError("'alias' and 'extern' are mutually exclusive");
    if(q.isAlias && q.isEmitter)
        parsingError("'alias' and 'emitter' are mutually exclusive");
    return q;
}

// ── parseExpression sub-functions ─────────────────────────────────────────────

// Ternary operator: condition ? trueExpr : falseExpr
// Lowers to if/else injection using _bgl_temp. Replaces expr contents and sets terminator.
void bglParser::parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body){
    if(ternaryDepth > 0)
        parsingError("Nested ternary expressions are not supported. Use an if/else statement instead.");
    ternaryDepth++;
    string condText = expr->text();
    expression* trueExpr  = parseExpression(file.getToken(), {":"}, func, body);
    expression* falseExpr = parseExpression(file.getToken(), terminators, func, body);
    string injText = "if (" + condText + ") _bgl_temp = " + trueExpr->text()
                   + "; else _bgl_temp = " + falseExpr->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    pendingInjections.push_back(inj);
    languageService.ternaryTempNeeded = true;
    expr->tokens.clear();
    expr->tokens.push_back("_bgl_temp");
    expr->resolvedType = !trueExpr->resolvedType.empty() ? trueExpr->resolvedType : falseExpr->resolvedType;
    expr->terminator = falseExpr->terminator;
    ternaryDepth--;
}

// Null coalescing: lhs ?? fallback
// Lowers to if injection using _bgl_temp and operator?(). Replaces expr contents and sets terminator.
void bglParser::parseExprNullCoalescing(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body){
    string lhsText = expr->text();
    string lhsType = expr->resolvedType;
    classDef* lhsCls = !lhsType.empty() ? dynamic_cast<classDef*>(&languageService.getType(lhsType)) : nullptr;
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
    nullTest = replaceWord(nullTest, "$self", "_bgl_temp");
    { size_t s=nullTest.find_first_not_of(" \t\n\r"); if(s!=string::npos) nullTest=nullTest.substr(s);
      size_t e=nullTest.find_last_not_of(" \t\n\r;"); if(e!=string::npos) nullTest=nullTest.substr(0,e+1); }
    expression* fallback = parseExpression(file.getToken(), terminators, func, body);
    string injText = "_bgl_temp = " + lhsText + "; if (~~(" + nullTest + ")) _bgl_temp = " + fallback->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    pendingInjections.push_back(inj);
    languageService.ternaryTempNeeded = true;
    expr->tokens.clear();
    expr->tokens.push_back("_bgl_temp");
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
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == callName){ retType = fd->returnType.name; break; }
    } else {
        GlobalCallMatch gcm = resolveGlobalCall(callName, pal.args, func, body);
        retType = validateGlobalCall(gcm, callName, pal.args.size());
        // Emitter inlining: substitute params and push as single token
        if(gcm.match && gcm.match->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(gcm.match->body)){
                string b = processBglConditionals(blk->i6Body);
                for(size_t i = 0; i < gcm.match->params.size() && i < pal.args.size(); i++)
                    b = replaceWord(b, "$" + gcm.match->params[i]->name, pal.args[i]->text());
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.push_back(b);
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                return true; // emitter inlined — caller should continue
            }
        }
    }
    if(retType == "void")
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
            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(opType));
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
                { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                  size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1); }
                expr->tokens.push_back("~~(" + b + ")");
                if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
                return true;
            }
        }
        // Try operator! emitter on the type
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(opType));
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

expression* bglParser::parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body){
    expression* expr = new expression();
    int parenDepth = 0;
    token cur = firstToken;
    optional<token> prefetched = nullopt;
    string castType;  // set when a (TypeName) cast prefix is detected

    auto isTerminator = [&](const token& t) -> bool {
        if(parenDepth != 0) return false;
        for(const string& term : terminators)
            if(t.value == term) return true;
        return false;
    };
    auto getNext = [&]() -> token {
        if(prefetched.has_value()){ token t = *prefetched; prefetched = nullopt; return t; }
        return file.getToken();
    };

    // ═══════════════════════════════════════════════════════════════════════
    // MAIN EXPRESSION LOOP — processes one token per iteration
    // Branches by token type: parens, literals, identifiers, operators, etc.
    // ═══════════════════════════════════════════════════════════════════════
    while(true){
        if(isTerminator(cur)){ expr->terminator = cur.value; break; }

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
            parenDepth++;
            expr->tokens.push_back(cur.value);
        }
        // ─── CLOSE PAREN ──────────────────────────────────────────────────
        else if(cur.is(token::parenClose)){
            if(parenDepth > 0) parenDepth--;
            expr->tokens.push_back(cur.value);
        }
        // ─── LITERALS: integer, string, char, dictionary word ─────────────
        else if(cur.is(eTokenType::integer)){
            if(expr->resolvedType.empty()) expr->resolvedType = "intliteral";
            expr->tokens.push_back(cur.value);
        }
        else if(cur.isString()){
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
            expr->tokens.push_back(cur.value);
        }
        // ═── IDENTIFIER: the largest branch ═════════════════════════════
        // Handles: subscript name[i], function call name(args), optional
        // chain name?., dot-access name.member/name.method(), postfix
        // query name?, and plain identifier fallback.
        // ═════════════════════════════════════════════════════════════════
        else if(cur.is(eTokenType::name)){
            token next = getNext();
            // ── name[i]: subscript access ──
            if(next.is(token::bracketOpen)){
                string arrName = cur.value;
                string arrType = resolveIdentifierType(arrName, func, body);
                classDef* arrCls = dynamic_cast<classDef*>(&languageService.getType(arrType));
                expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                functionDef* getMethod = nullptr;
                if(arrCls != nullptr)
                    getMethod = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [](typeMember* m){
                        auto* fd = dynamic_cast<functionDef*>(m);
                        return fd && fd->name == "[]" && fd->params.size() == 1;
                    }));
                if(getMethod == nullptr) parsingError(format("Type '{0}' does not support subscript read (no operator[])", arrType));
                if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                if(getMethod->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                        string b = processBglConditionals(blk->i6Body);
                        string pv = (arrType == "array") ? "0" : "<$prop undefined>";
                        b = replaceWord(b, "$self", arrName);
                        b = replaceWord(b, "$prop", pv);
                        if(!getMethod->params.empty())
                            b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                        expr->tokens.push_back(b);
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
                // Self-call detection for object member methods
                bool isSelfCall = false;
                if(func != nullptr && callName.find('.') == string::npos && currentObject != nullptr)
                    for(typeMember* m : currentObject->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->name == callName){ isSelfCall = true; break; }
                // Note: '(' was already consumed by getNext() above
                if(parseExprFunctionCall(expr, callName, isSelfCall, func, body))
                    continue; // emitter was inlined — skip getNext at loop bottom
            }
            // ── name?.: optional chaining (expression-level) ──
            else if(parenDepth == 0 && next.is("?.")){
                // Optional chaining: obj?.member or obj?.method(args)
                // Lowers to: _bgl_temp = obj; if(nullTest) _bgl_temp = _bgl_temp.member;
                string lhsName = cur.value;
                string lhsType = !castType.empty() ? castType : resolveIdentifierType(lhsName, func, body);
                castType = "";
                if(lhsType.empty()) parsingError(format("Unknown variable '{0}' in optional chain", lhsName));
                // Look up operator?() on LHS type for the null test
                classDef* lhsCls = dynamic_cast<classDef*>(&languageService.getType(lhsType));
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
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                    return b;
                };
                // Build the guarded chain
                string injText = "_bgl_temp = " + lhsName + ";";
                string currentType = lhsType;
                // Process chain steps: each is ?.member, ?.method(), or a trailing .member/.method()
                while(true){
                    string nullTest = getNullTest(nullTestFn, "_bgl_temp");
                    injText += " if (" + nullTest + ") {";
                    token member = file.getToken(eTokenType::identifier);
                    token afterMember = getNext();
                    if(afterMember.is(token::parenOpen)){
                        // ?.method(args) — find method, inline emitter or call
                        string methName = member.value;
                        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(currentType));
                        vector<expression*> callArgs;
                        token firstArg = file.getToken();
                        while(firstArg.isNot(token::parenClose)){
                            expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                            callArgs.push_back(arg);
                            if(arg->terminator == token::parenClose) break;
                            firstArg = file.getToken();
                        }
                        MethodMatch mm = resolveMethod(currentType, "_bgl_temp", methName, callArgs);
                        functionDef* method = mm.method;
                        if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                        if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                b = replaceWord(b, "$self", "_bgl_temp");
                                for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                    b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                injText += " _bgl_temp = " + b + ";";
                            }
                        } else {
                            string call = "_bgl_temp." + methName + "(";
                            for(size_t i = 0; i < callArgs.size(); i++){
                                if(i > 0) call += ", ";
                                call += callArgs[i]->text();
                            }
                            call += ")";
                            injText += " _bgl_temp = " + call + ";";
                        }
                        currentType = method->returnType.name;
                        afterMember = getNext();
                    } else {
                        // ?.property — simple property access
                        string propType = resolvePathType("_x_." + member.value, func, body);
                        // We can't resolve the runtime path, so check the class for a member
                        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(currentType));
                        if(cls != nullptr)
                            for(typeMember* m : cls->members)
                                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                    if(vd->name == member.value){ propType = vd->type.name; break; }
                        injText += " _bgl_temp = _bgl_temp." + member.value + ";";
                        currentType = propType;
                    }
                    // Check for continuation: another ?. or regular .
                    if(afterMember.is("?.")){
                        // Another optional step: look up operator?() on the current type
                        classDef* nextCls = dynamic_cast<classDef*>(&languageService.getType(currentType));
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
                            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(currentType));
                            vector<expression*> callArgs;
                            token firstArg = file.getToken();
                            while(firstArg.isNot(token::parenClose)){
                                expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                                callArgs.push_back(arg);
                                if(arg->terminator == token::parenClose) break;
                                firstArg = file.getToken();
                            }
                            MethodMatch mm2 = resolveMethod(currentType, "_bgl_temp", methName, callArgs);
                            functionDef* method = mm2.method;
                            if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                            if(method->isEmitter){
                                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                    string b = processBglConditionals(blk->i6Body);
                                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                    b = replaceWord(b, "$self", "_bgl_temp");
                                    for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                        b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                    injText += " _bgl_temp = " + b + ";";
                                }
                            } else {
                                string call = "_bgl_temp." + methName + "(";
                                for(size_t i = 0; i < callArgs.size(); i++){ if(i > 0) call += ", "; call += callArgs[i]->text(); }
                                call += ")";
                                injText += " _bgl_temp = " + call + ";";
                            }
                            currentType = method->returnType.name;
                            afterMember = getNext();
                            // Could chain further — check again
                            if(afterMember.is("?.") || afterMember.is(token::period)) { prefetched = afterMember; /* TODO: loop */ }
                            else prefetched = afterMember;
                        } else {
                            // .property
                            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(currentType));
                            string propType;
                            if(cls != nullptr)
                                for(typeMember* m : cls->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->name == nextMember.value){ propType = vd->type.name; break; }
                            injText += " _bgl_temp = _bgl_temp." + nextMember.value + ";";
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
                languageService.ternaryTempNeeded = true;
                expr->tokens.clear();
                expr->tokens.push_back("_bgl_temp");
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
                    if(expr->resolvedType.empty() && !memberType.empty()) expr->resolvedType = memberType;
                    expr->tokens.push_back("self." + member.value);
                } else {
                    // Non-self identifier, or self.method(args).
                    // afterMember was already read above; resolveIdentifierType("self",...)
                    // now returns the current object/class type, so self.method() works here too.
                    if(afterMember.is(token::parenOpen)){
                        // method call in expression context: obj.method(args)
                        string objName = cur.value;
                        string methName = member.value;
                        string objType = !castType.empty() ? castType : resolveIdentifierType(objName, func, body);
                        castType = "";  // consume the cast
                        if(objType.empty()) parsingError(format("Unknown variable '{0}'", objName));
                        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(objType));
                        if(cls == nullptr) parsingError(format("Type '{0}' has no methods", objType));

                        // parse argument list
                        vector<expression*> callArgs;
                        token firstArg = file.getToken();
                        while(firstArg.isNot(token::parenClose)){
                            expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                            callArgs.push_back(arg);
                            if(arg->terminator == token::parenClose) break;
                            firstArg = file.getToken();
                        }

                        MethodMatch mm = resolveMethod(objType, objName, methName, callArgs);
                        functionDef* method = mm.method;
                        // If not found, try conversion operator: if the type has operator() → targetType,
                        // retry method resolution on the target type
                        if(method == nullptr){
                            classDef* srcCls = dynamic_cast<classDef*>(&languageService.getType(objType));
                            if(srcCls){
                                for(typeMember* m : srcCls->members){
                                    auto* convOp = dynamic_cast<functionDef*>(m);
                                    if(convOp && convOp->name == "operator()" && convOp->isEmitter && !convOp->isExplicit){
                                        string convertedType = convOp->returnType.name;
                                        MethodMatch mm2 = resolveMethod(convertedType, objName, methName, callArgs);
                                        if(mm2.method){
                                            mm = mm2;
                                            method = mm2.method;
                                            objType = convertedType;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        if(method == nullptr){
                            if(!mm.nameFound)
                                parsingError(format("No method '{0}' defined on type '{1}'", methName, typeDisplayName(objType)));
                            else
                                parsingError(format("No method '{0}' on type '{1}' matches arguments", methName, typeDisplayName(objType)));
                        }

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
                        string exprPropValue = (objType == "array") ? "0" : "<$prop undefined>";

                        if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r"); if(e != string::npos) b = b.substr(0, e+1);
                                b = replaceWord(b, "$self", objName);
                                b = replaceWord(b, "$prop", exprPropValue);
                                for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                    b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                expr->tokens.push_back(b);
                            }
                        } else {
                            // non-emitter: emit verbatim as obj.method(args)
                            string call = objName + "." + methName + "(";
                            for(size_t i = 0; i < callArgs.size(); i++){
                                if(i > 0) call += ", ";
                                call += callArgs[i]->text();
                            }
                            call += ")";
                            expr->tokens.push_back(call);
                        }
                    } else if(afterMember.is(token::bracketOpen)) {
                        // Property array subscript in expression: obj.prop[i]
                        string objName = cur.value;
                        string propName = member.value;
                        string propType = resolvePathType(objName + "." + propName, func, body);
                        classDef* arrCls = dynamic_cast<classDef*>(&languageService.getType(propType));
                        expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                        functionDef* getMethod = nullptr;
                        if(arrCls != nullptr)
                            getMethod = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [](typeMember* m){
                                auto* fd = dynamic_cast<functionDef*>(m);
                                return fd && fd->name == "[]" && fd->params.size() == 1;
                            }));
                        if(getMethod == nullptr) parsingError(format("Type '{0}' does not support subscript read (no operator[])", propType));
                        if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                        if(getMethod->isEmitter)
                            if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                                string b = processBglConditionals(blk->i6Body);
                                b = replaceWord(b, "$self", objName);
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
                        classDef* maybeCls = dynamic_cast<classDef*>(&languageService.getType(cur.value));
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
                        // Value emitter member on emitter class: expand body inline
                        bool isValueEmitterAccess = false;
                        if(!isStaticAccess && maybeCls != nullptr){
                            for(typeMember* m : maybeCls->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == member.value && fd->isValueEmitter && fd->isEmitter){
                                        if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                                            string b = processBglConditionals(blk->i6Body);
                                            b = i6Emitter::replaceWord(b, "$self", cur.value);
                                            size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                            size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                            expr->tokens.push_back(b);
                                            if(expr->resolvedType.empty()) expr->resolvedType = fd->returnType.name;
                                            isValueEmitterAccess = true;
                                        }
                                        break;
                                    }
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
                        if(isEnum){
                            if(expr->resolvedType.empty()) expr->resolvedType = cur.value;
                            expr->tokens.push_back("_" + cur.value + "_" + member.value);
                        } else if(isAliasMember) {
                            continue;  // re-enter loop with cur set to alias type
                        } else if(!isStaticAccess && !isValueEmitterAccess) {
                            // Object property access: emit as obj.prop
                            string propType = resolvePathType(cur.value + "." + member.value, func, body);
                            if(expr->resolvedType.empty() && !propType.empty()) expr->resolvedType = propType;
                            expr->tokens.push_back(cur.value + "." + member.value);
                        }
                    }
                }
            }
            // ── name?: postfix query operator ──
            // Only treat ? as postfix if the identifier's type has operator?().
            // Otherwise fall through so ? is put back and handled as ternary.
            else if(next.is("?")){
                string varName = cur.value;
                string varType = resolveIdentifierType(varName, func, body);
                classDef* cls = !varType.empty() ? dynamic_cast<classDef*>(&languageService.getType(varType)) : nullptr;
                functionDef* queryFn = nullptr;
                if(cls != nullptr)
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
                prefetched = next;
                // Resolve identifier: variables/params/globals take priority over verb action constants.
                // Only emit ##VerbName if the identifier doesn't resolve as a declared variable.
                // qualifyIdentifier handles: params/locals → name, object members → self.name,
                // globals → name, action constants (verbDefs) → ##VerbName.
                // It works correctly with func==nullptr (skips param/local tiers gracefully).
                string qualified = qualifyIdentifier(cur.value, func, body);
                if(!qualified.empty()){
                    if(!castType.empty()){ expr->resolvedType = castType; castType = ""; }
                    else if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body);
                    expr->tokens.push_back(qualified);
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
                if(cls != nullptr){
                    string opName = cur.value;
                    // Peek at RHS
                    applyBinaryOperator(expr, opName, cls, terminators, parenDepth, getNext, prefetched, func, body);
                } else {
                    expr->tokens.push_back(cur.value);
                }
            } else if(cur.value == "!"){
                parseExprPrefixNot(expr, getNext(), prefetched, func, body);
            } else {
                expr->tokens.push_back(cur.value);
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

            string chainTypeName = expr->resolvedType;
            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(chainTypeName));
            if(cls == nullptr)
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

            MethodMatch mm = resolveMethod(chainTypeName, selfText, methName, callArgs);
            functionDef* method = mm.method;
            // If not found, try conversion operator fallback
            if(method == nullptr){
                classDef* srcCls = dynamic_cast<classDef*>(&languageService.getType(chainTypeName));
                if(srcCls){
                    for(typeMember* m : srcCls->members){
                        auto* convOp = dynamic_cast<functionDef*>(m);
                        if(convOp && convOp->name == "operator()" && convOp->isEmitter && !convOp->isExplicit){
                            string convertedType = convOp->returnType.name;
                            MethodMatch mm2 = resolveMethod(convertedType, selfText, methName, callArgs);
                            if(mm2.method){
                                mm = mm2;
                                method = mm2.method;
                                chainTypeName = convertedType;
                                break;
                            }
                        }
                    }
                }
            }
            if(method == nullptr){
                if(!mm.nameFound)
                    parsingError(format("No method '{0}' defined on type '{1}'", methName, typeDisplayName(chainTypeName)));
                else
                    parsingError(format("No method '{0}' on type '{1}' matches arguments", methName, typeDisplayName(chainTypeName)));
            }

            expr->tokens.clear();
            expr->resolvedType = method->returnType.name;

            if(method->isEmitter){
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    string b = processBglConditionals(blk->i6Body);
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r"); if(e != string::npos) b = b.substr(0, e+1);
                    b = replaceWord(b, "$self", selfText);
                    b = replaceWord(b, "$prop", "<$prop undefined>");
                    for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                        b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                    expr->tokens.push_back(b);
                }
            } else {
                string call = selfText + "." + methName + "(";
                for(size_t i = 0; i < callArgs.size(); i++){
                    if(i > 0) call += ", ";
                    call += callArgs[i]->text();
                }
                call += ")";
                expr->tokens.push_back(call);
            }
        }
        // ─── TERNARY OPERATOR: condition ? trueExpr : falseExpr ──────────
        else if(cur.value == "?" && parenDepth == 0) {
            parseExprTernary(expr, terminators, func, body);
            break;
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
                else if(key == "framepoolsize"){ isInt = true; intVal = beguilerSettings.framePoolSize < 0 ? 0 : beguilerSettings.framePoolSize; }
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
    return expr;
}

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
        // Try emitter lookup for "prefix++" / "prefix--" on the LHS type
        string lhsTypeName = resolveIdentifierType(varName.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
        bool emitterFound = false;
        string prefixOpName = "prefix" + tok.value;  // e.g. "prefix++"
        if(lhsClass != nullptr){
            if(typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                return opFunc && opFunc->name==prefixOpName && opFunc->isEmitter
                       && opFunc->params.empty() && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
            })){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                string b = processBglConditionals(blk->i6Body);
                size_t pos = 0;
                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, lhs); pos += lhs.size(); }
                i6RawNode& node = *(new i6RawNode());
                node.text = b + ";";
                node.src = stmtLoc;
                if(body != nullptr) body->statements.push_back(&node);
                emitterFound = true;
            }
        }
        if(!emitterFound){
            if(!lhsTypeName.empty() && lhsTypeName != "var")
                parsingError(format("No operator '{0}' defined on type '{1}'", prefixOpName, typeDisplayName(lhsTypeName)));
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
            { size_t s=guard.find_first_not_of(" \t\n\r"); if(s!=string::npos) guard=guard.substr(s);
              size_t e=guard.find_last_not_of(" \t\n\r;"); if(e!=string::npos) guard=guard.substr(0,e+1); }
            i6RawNode* openNode = new i6RawNode();
            openNode->text = "if (" + guard + ") {";
            pendingInjections.push_back(openNode);
            optionalChainDepth++;
        }
        tok.value += "." + file.getToken(eTokenType::identifier).value;
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

    // Subscript assignment: name[i] = v  or  obj.prop[i] = v
    if(symbol.is(token::bracketOpen)) {
        string arrPath = (string)tok;  // e.g. "scores" or "player.inventory"
        expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        file.getToken(token::assignment);
        expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);

        // Resolve array type and compute $self/$prop
        string arrType = resolvePathType(arrPath, func, body);
        classDef* arrCls = dynamic_cast<classDef*>(&languageService.getType(arrType));
        if(arrCls == nullptr) parsingError(format("Type '{0}' does not support subscript access", arrType));

        // Find operator[]= method — two-pass: exact value type match, then var wildcard
        string valType = valExpr ? valExpr->resolvedType : "";
        functionDef* setMethod = nullptr;
        if(!valType.empty())
            setMethod = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [&](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                return fd && fd->name == "[]=" && fd->params.size() == 2 && fd->params[1]->type.name == valType;
            }));
        if(setMethod == nullptr)
            setMethod = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                return fd && fd->name == "[]=" && fd->params.size() == 2 && fd->params[1]->type.name == "var";
            }));
        if(setMethod == nullptr){
            if(valType.empty())
                parsingError(format("Type '{0}' does not support subscript write (no operator[]=)", arrType));
            else
                parsingError(format("Type '{0}' does not support subscript write with value type '{1}' (no compatible operator[]=)", arrType, valType));
        }

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
                classDef* ownerCls = dynamic_cast<classDef*>(&languageService.getType(ownerType));
                if(ownerCls != nullptr)
                    for(typeMember* m : ownerCls->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == propName){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                                leftType = &vd->type; break;
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
                            if(vd->name == lhsOriginal){ leftType = &vd->type; break; }
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
            if(classType != nullptr && val != nullptr){
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
                            found = true;
                        }
                    }
                    if(!found) found = isTypeCompatible(valueTypeName, leftType->name);
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

        expression* rhs = parseExpression(file.getToken(), {token::endStatement, "?"}, func, body);

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
    // Compound assignment: +=, -=, *=, /=, %=, |=, &=
    static const vector<string> compoundOps = {"+=","-=","*=","/=","%=","|=","&="};
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
                size_t pos = 0;
                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, lhs); pos += lhs.size(); }
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
            string objectType = !stmtCastType.empty() ? stmtCastType
                              : !literalTypeName.empty() ? literalTypeName
                              : resolvePathType(objectPath, func, body);
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
            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(objectType));
            if(cls == nullptr)
                parsingError(format("Type '{0}' is not a class", objectType));
            MethodMatch mm = resolveMethod(objectType, objectPath, methodName, callStmt.args);
            functionDef* method = mm.method;
            // Conversion operator fallback
            if(method == nullptr && cls){
                for(typeMember* m : cls->members){
                    auto* convOp = dynamic_cast<functionDef*>(m);
                    if(convOp && convOp->name == "operator()" && convOp->isEmitter && !convOp->isExplicit){
                        string convertedType = convOp->returnType.name;
                        MethodMatch mm2 = resolveMethod(convertedType, objectPath, methodName, callStmt.args);
                        if(mm2.method){
                            mm = mm2;
                            method = mm2.method;
                            objectType = convertedType;
                            cls = dynamic_cast<classDef*>(&languageService.getType(objectType));
                            break;
                        }
                    }
                }
            }
            if(!mm.nameFound)
                parsingError(format("No method '{0}' on type '{1}'", methodName, typeDisplayName(objectType)));
            if(mm.nameMatch && !mm.arityMatch){
                size_t req = 0; for(paramDef* p : mm.nameMatch->params) if(p->defaultValue.empty()) req++;
                size_t tot = mm.nameMatch->params.size();
                parsingError(format("Method '{0}' on type '{1}' expects {2} argument(s), but {3} were supplied.\n  Expected: {4}",
                    methodName, typeDisplayName(objectType),
                    (req == tot) ? to_string(tot) : to_string(req) + "-" + to_string(tot),
                    callStmt.args.size(), formatSignature(mm.nameMatch)));
            }
            if(method == nullptr)
                parsingError(format("No overload of method '{0}' on type '{1}' accepts these argument types",
                    methodName, typeDisplayName(objectType)));
            // reorder named arguments to match parameter positions
            reorderNamedArgs(callStmt, method, [&](string msg){ parsingError(msg); return false; });
            // fill in defaults for unspecified trailing arguments
            for(size_t i = callStmt.args.size(); i < method->params.size(); i++){
                expression* defExpr = new expression();
                defExpr->tokens.push_back(method->params[i]->defaultValue);
                callStmt.args.push_back(defExpr);
            }
            // apply source-type conversion operators for mismatched arg types
            applyArgConversions(callStmt.args, method);
            // if emitter, pre-substitute $self and $prop
            if(method->isEmitter)
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    string b = processBglConditionals(blk->i6Body);
                    size_t pos = 0;
                    while((pos = b.find("$self", pos)) != string::npos){
                        b.replace(pos, 5, selfValue);
                        pos += selfValue.size();
                    }
                    pos = 0;
                    while((pos = b.find("$prop", pos)) != string::npos){
                        b.replace(pos, 5, propValue);
                        pos += propValue.size();
                    }
                    callStmt.emitterBody = b;
                    for(paramDef* p : method->params)
                        callStmt.emitterParams.push_back(p->name);
                }
            chainReturnType = method->returnType.name;
        } else {
            // global function call: resolve, validate, apply defaults/conversions/emitter
            GlobalCallMatch gcm = resolveGlobalCall(callStmt.functionName, callStmt.args, func, body);
            chainReturnType = validateGlobalCall(gcm, callStmt.functionName, callStmt.args.size());
            if(gcm.funcVarReturnType.empty() && gcm.match){
                // reorder named arguments to match parameter positions
                reorderNamedArgs(callStmt, gcm.match, [&](string msg){ parsingError(msg); return false; });
                // fill in defaults for unspecified trailing arguments
                for(size_t i = callStmt.args.size(); i < gcm.match->params.size(); i++){
                    expression* defExpr = new expression();
                    defExpr->tokens.push_back(gcm.match->params[i]->defaultValue);
                    callStmt.args.push_back(defExpr);
                }
                // apply source-type conversion operators for mismatched arg types
                applyArgConversions(callStmt.args, gcm.match);
                if(gcm.match->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(gcm.match->body)){
                        callStmt.emitterBody = processBglConditionals(blk->i6Body);
                        for(paramDef* p : gcm.match->params) callStmt.emitterParams.push_back(p->name);
                    }
            }
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
std::string bglParser::resolvePathType(std::string path, functionDef* func, statementBlock* body) {
    size_t dot = path.find('.');
    if(dot == string::npos) return resolveIdentifierType(path, func, body);
    string head = path.substr(0, dot);
    string tail = path.substr(dot + 1);
    string headType = resolveIdentifierType(head, func, body);

    // Check the specific object instance's members first (for instance-level properties)
    string lowerHead = head;
    transform(lowerHead.begin(), lowerHead.end(), lowerHead.begin(), ::tolower);
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            if(od->name == lowerHead)
                for(typeMember* m : od->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == tail) return vd->type.name;

    // Then check the type's class hierarchy
    typeDef& td = languageService.getType(headType);
    if(auto* od = dynamic_cast<objectDef*>(&td))
        for(typeMember* m : od->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == tail) return vd->type.name;
    if(auto* cd = dynamic_cast<classDef*>(&td)){
        typeMember* found = findMemberInHierarchy(cd, [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd != nullptr && vd->name == tail;
        });
        if(found) return dynamic_cast<variableDeclaration*>(found)->type.name;
    }
    return "";
}

// Process an array<T> declaration: array<T> name[N]; or array<T> name = { ... };
bool bglParser::processArrayDeclaration(token dataType, token name, string elementType, token symbol, abstractObject& contextObj, bool isExternal) {
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    arrDecl.src = file.currentLocation();
    arrDecl.name = (string)name;
    arrDecl.type = languageService.getType(elementType == "char" ? "bytearray" : "array");
    arrDecl.elementType = elementType;
    arrDecl.isExternal = isExternal;
    if(elementType == "char") arrDecl.isByteArray = true;

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    if(symbol.is(token::bracketOpen)) {
        // array<T> name[N];
        token sizeTok = file.getToken(eTokenType::integer);
        arrDecl.arraySize = stoi(sizeTok.value);
        file.getToken(token::bracketClose);
        file.getToken(token::endStatement);
    } else if(symbol.is(token::assignment)) {
        // Check for string initializer: array<char> name = "text";
        token firstVal = file.peekToken(1);
        if(firstVal.is(eTokenType::quote) || firstVal.is(eTokenType::rawQuote)){
            if(elementType != "char" && elementType != "charliteral")
                parsingError("String initializer is only valid for array<char>");
            token strTok = file.getToken();
            arrDecl.stringInitializer = strTok.value;
            file.getToken(token::endStatement);
        } else {
        // array<T> name = { v1, v2, ... };
        file.getToken(token::braceOpen);
        initializerList* list = new initializerList();
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)) {
            expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
            list->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        file.getToken(token::endStatement);
        // type-check each element against the declared element type
        if(!elementType.empty() && elementType != "var"){
            for(size_t i = 0; i < list->elements.size(); i++){
                expression* elem = list->elements[i];
                if(!elem->resolvedType.empty() && !isTypeCompatible(elem->resolvedType, elementType))
                    parsingError(format("Array element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, elementType));
            }
        }
        arrDecl.declaredExpressionValue = list;
        } // end brace initializer else
    }
    // else symbol is endStatement: extern/forward declaration — no size or initializer

    if(body != nullptr)
        body->statements.push_back(&arrDecl);
    else
        languageService.registerInstance(arrDecl);
    return false;
}

//--process a variable declaration statement and add it to the parse tree
//-- handles both simple declarations and declarations with assignment
//-- also handles parameter declarations within function definitions
//-- e.g.:
//--      int myVar;
//--      int myVar=99;
//--      int myParam)
//--      int myParam=5) 
//--      int myParam=5, ...
bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol, abstractObject& contextObj, bool isExternal, bool isConst, string i6alias){
    // Extern Type name ; where Type is verb-derived → route to object declaration (verb instance)
    if(isExternal && symbol.is(token::endStatement)){
        bool verbDerived = false;
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(dataType.value))){
            function<bool(classDef*)> checkVerb = [&](classDef* c) -> bool {
                if(!c) return false;
                if(c->name == "verb") return true;
                for(classDef* b : c->baseClasses) if(checkVerb(b)) return true;
                return false;
            };
            verbDerived = checkVerb(cls);
        }
        if(verbDerived)
            return processObjectDeclaration(dataType, variableName, true, "", "", false);
    }
    variableDeclaration& varDecl = *new variableDeclaration();
    varDecl.src = file.currentLocation();
    varDecl.name=(string) variableName;
    varDecl.type=languageService.getType((string) dataType);
    if(!i6alias.empty()) varDecl.i6name = i6alias;
    varDecl.isExternal=isExternal;
    varDecl.isConst=isConst;
    // For func<...> types, getType returns the base "func" type. Set the full parameterized name.
    {
        string dtLower = (string)dataType;
        transform(dtLower.begin(), dtLower.end(), dtLower.begin(), ::tolower);
        if(dtLower.rfind("func<", 0) == 0) varDecl.type.name = dtLower;
    }

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    // Disallow local variable names that shadow a global, a class member, or an object member;
    // also disallow duplicate declarations within the same scope.
    if(func != nullptr){
        // Duplicate in same scope
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == varDecl.name)
                        parsingError(format("Variable '{0}' is already declared in this scope", varDecl.name));
        for(typeDef* g : languageService.globals)
            if(g->name == varDecl.name)
                parsingError("Local variable '" + varDecl.name + "' shadows global variable of the same name. Rename the local.");
        if(currentClass != nullptr)
            for(typeMember* m : currentClass->members)
                if(m->name == varDecl.name)
                    parsingError("Local variable '" + varDecl.name + "' shadows a member of class '" + currentClass->name + "'. Rename the local.");
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(m->name == varDecl.name)
                    parsingError("Local variable '" + varDecl.name + "' shadows a member of object '" + currentObject->name + "'. Rename the local.");
    }

    bool isAuto = (dataType.value == "auto");
    if(isAuto && symbol.value != token::assignment)
        parsingError("'auto' requires an initializer to infer the type");

    if(symbol.value==token::assignment){
        token first = file.getToken();
        if(first.is(token::braceOpen)){
            // initializer list: { expr, expr, ... }
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            // consume the trailing semicolon
            file.getToken(token::endStatement);

            // type-check each element against the declared element type
            string dtStr = (string)dataType;
            string expectedElemType;
            // For array<T>, extract T as the expected element type
            if(dtStr.size() > 6 && dtStr.substr(0, 6) == "array<" && dtStr.back() == '>')
                expectedElemType = dtStr.substr(6, dtStr.size() - 7);
            // Fallback: infer from the class's single-param method
            if(expectedElemType.empty()){
                classDef* listClass = dynamic_cast<classDef*>(&languageService.getType(dtStr));
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

            varDecl.declaredExpressionValue = list;
        } else if(first.is("$") && file.peekToken(1).is(eTokenType::quote)){
            // Interpolated string literal initializer: string s = $"..."
            varDecl.interpSegments = parseInterpolatedSegments(func, body);
            file.getToken(token::endStatement);  // consume ';'
            expression* rhs = new expression();
            rhs->resolvedType = "interpolatedstringliteral";
            varDecl.declaredExpressionValue = rhs;
            // Emitter lookup for operator=(interpolatedstringliteral)
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr){
                functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="interpolatedstringliteral";
                }));
                if(assignOp && assignOp->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                        varDecl.initEmitterBody  = processBglConditionals(blk->i6Body);
                        varDecl.initEmitterParam = assignOp->params[0]->name;
                    }
                }
                if(!assignOp)
                    parsingError(format("Type '{0}' has no operator=(interpolatedStringLiteral) emitter", (string)dataType));
            }
        } else {
            expression* rhs = parseExpression(first, {token::endStatement}, func, body);
            varDecl.declaredExpressionValue = rhs;

            // auto type inference: resolve type from RHS, checking operator auto() for type promotion
            if(isAuto && rhs != nullptr && !rhs->resolvedType.empty()){
                string inferredType = rhs->resolvedType;
                // Check if the RHS type has operator auto() — use its return type instead
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(inferredType));
                if(rhsCls){
                    for(typeMember* m : rhsCls->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->name == "auto"){ inferredType = fd->returnType.name; break; }
                }
                varDecl.type = languageService.getType(inferredType);
                if(varDecl.type.name.empty()) varDecl.type.name = inferredType;
                dataType.value = inferredType;  // update for downstream type checks
            } else if(isAuto){
                parsingError("Cannot infer type from initializer expression");
            }

            //type check: if the declared type is a class, verify the assigned value is accepted by one of its operator= signatures
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr && rhs != nullptr){
                string valueTypeName = rhs->resolvedType;
                if(!valueTypeName.empty()){
                    // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all.
                    // Always run findMemberInHierarchy so we can capture the emitter body if found.
                    functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name==valueTypeName;
                    }));
                    if(!assignOp) assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="var";
                    }));
                    bool found = assignOp != nullptr || isTypeCompatible(valueTypeName, (string)dataType);
                    // If operator= is an emitter, capture its body so the emitter uses it instead of plain assignment
                    // Skip if RHS contains $target — the opcode handles its own store
                    if(assignOp && assignOp->isEmitter && rhs->text().find("$target") == string::npos){
                        if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                            varDecl.initEmitterBody  = processBglConditionals(blk->i6Body);
                            varDecl.initEmitterParam = assignOp->params[0]->name;
                        }
                    }
                    if(!found){
                        // Fallback: check if RHS type has emitter DeclaredType operator(){}
                        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(valueTypeName));
                        if(rhsCls != nullptr)
                            if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                                       && opFn->returnType.name==(string)dataType && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                            })){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                auto* blk = dynamic_cast<i6Block*>(opFn->body);
                                string b = processBglConditionals(blk->i6Body);
                                string argText = rhs->text();
                                size_t pos = 0;
                                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
                                rhs->tokens.clear();
                                rhs->tokens.push_back(b);
                                rhs->resolvedType = (string)dataType;
                                found = true;
                            }
                    }
                    if(!found) parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName((string)dataType)));
                }
            }
        }
    }

    // Flush ternary-lowering injections before the variable declaration that uses _bgl_temp
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();

    if(body != nullptr)
        body->statements.push_back(&varDecl);
    else
        languageService.registerInstance(varDecl);

    // init / deinit: if the variable's type is a class with init/deinit emitters, inject them
    // For local variables: inject init as an i6RawNode and register deinit in func->cleanups.
    // For global variables: record init in languageService.globalInits for the synthesised bglInit routine.
    if(!isConst && body != nullptr && func != nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0) continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = processBglConditionals(blk->i6Body);
                // trim
                size_t s=bodyText.find_first_not_of(" \t\n\r"); if(s!=string::npos) bodyText=bodyText.substr(s);
                size_t e=bodyText.find_last_not_of(" \t\n\r");  if(e!=string::npos) bodyText=bodyText.substr(0,e+1);
                if(bodyText.empty()) continue;
                // substitute $self with variable name
                string substituted = replaceWord(bodyText, "$self", varDecl.name);
                if(fn->name == "init"){
                    i6RawNode& initNode = *(new i6RawNode());
                    initNode.text = substituted;
                    // insert before varDecl (which is currently the last element)
                    body->statements.insert(body->statements.end()-1, &initNode);
                } else if(fn->name == "deinit"){
                    func->cleanups.push_back({varDecl.name, substituted});
                }
            }
        }
    }

    // Global scope: record init body in globalInits for bglInit
    if(!isConst && body == nullptr && func == nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0 || fn->name != "init") continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = processBglConditionals(blk->i6Body);
                size_t s=bodyText.find_first_not_of(" \t\n\r"); if(s!=string::npos) bodyText=bodyText.substr(s);
                size_t e=bodyText.find_last_not_of(" \t\n\r");  if(e!=string::npos) bodyText=bodyText.substr(0,e+1);
                if(bodyText.empty()) continue;
                languageService.globalInits.push_back({varDecl.name, replaceWord(bodyText, "$self", varDecl.name)});
            }
        }
    }

    return false;
}
// Evaluate ##ifdef/##ifndef/##else/##endif directives in a raw emitter body string.
// Sections guarded by a false condition are removed from the output entirely.
// Nesting is supported. The ## directives are never included in the output.
string bglParser::processBglConditionals(const string& text){
    string result;
    size_t pos = 0;

    struct Frame { bool shouldEmit; bool ifCondTrue; bool parentEmit; };
    vector<Frame> stk = {{true, true, true}};

    auto readIdent = [&]() -> string {
        string id;
        while(pos < text.size() && (isalnum((unsigned char)text[pos]) || text[pos]=='_'))
            id += (char)tolower(text[pos++]);
        return id;
    };
    auto skipSpaces = [&](){
        while(pos < text.size() && text[pos]==' ') pos++;
    };

    while(pos < text.size()){
        if(pos+1 < text.size() && text[pos]=='#' && text[pos+1]=='#'){
            pos += 2;
            string dir = readIdent();
            skipSpaces();

            if(dir == "if" || dir == "ifdef" || dir == "ifndef"){
                string condText;
                while(pos < text.size() && text[pos] != '\n' && !(text[pos] == '#' && pos+1 < text.size() && text[pos+1] == '#'))
                    condText += text[pos++];
                while(!condText.empty() && (condText.back() == ' ' || condText.back() == ';')) condText.pop_back();
                bool condTrue;
                if(dir == "ifdef") condTrue = definedSymbols.count(condText) > 0;
                else if(dir == "ifndef") condTrue = definedSymbols.count(condText) == 0;
                else condTrue = evaluateCondition(condText);
                bool parent = stk.back().shouldEmit;
                stk.push_back({parent && condTrue, condTrue, parent});
            } else if(dir == "else"){
                if(stk.size() > 1){
                    bool ifWasTrue = stk.back().ifCondTrue;
                    bool parent    = stk.back().parentEmit;
                    stk.pop_back();
                    stk.push_back({parent && !ifWasTrue, !ifWasTrue, parent});
                }
            } else if(dir == "endif"){
                if(stk.size() > 1) stk.pop_back();
            } else {
                // Not a known directive — pass ##identifier through verbatim (e.g. ##VerbName in emitter bodies)
                if(stk.back().shouldEmit) result += "##" + dir;
            }
            // known directive text is never forwarded to output
        } else {
            if(stk.back().shouldEmit) result += text[pos];
            pos++;
        }
    }
    return result;
}

// Evaluate a #if / #elif boolean expression against definedSymbols.
// Supports: symbol, integer literal, !expr, expr&&expr, expr||expr, (expr),
//           and comparison operators: == != < > <= >=
// Precedence (low→high): || → && → ! → comparison → atom
// (macro system removed — emitters with ##if cover the use case)

bool bglParser::evaluateCondition(const string& expr){
    struct Eval {
        const string& s;
        size_t pos = 0;
        map<string,string>& syms;

        void skipWs(){ while(pos<s.size() && isspace(s[pos])) pos++; }

        // Read a value atom — returns its string representation.
        // Symbols resolve to their defined value ("" → "1"), undefined → "0".
        // Numeric literals return the literal text.
        string readAtom(){
            skipWs();
            if(pos<s.size() && (isdigit(s[pos]) || (s[pos]=='-' && pos+1<s.size() && isdigit(s[pos+1])))){
                size_t start=pos; if(s[pos]=='-') pos++;
                while(pos<s.size() && isdigit(s[pos])) pos++;
                return s.substr(start,pos-start);
            }
            if(pos<s.size() && (isalpha(s[pos]) || s[pos]=='_')){
                size_t start=pos;
                while(pos<s.size() && (isalnum(s[pos])||s[pos]=='_')) pos++;
                string name=s.substr(start,pos-start);
                // normalize to lowercase to match stored keys
                transform(name.begin(),name.end(),name.begin(),::tolower);
                auto it=syms.find(name);
                if(it==syms.end()) return "0";               // undefined → 0
                return it->second.empty() ? "1" : it->second; // boolean flag → 1
            }
            return "0";
        }

        bool parseOr(){
            bool lhs=parseAnd();
            while(true){ skipWs();
                if(pos+1<s.size() && s[pos]=='|' && s[pos+1]=='|'){ pos+=2; lhs=lhs||parseAnd(); }
                else break; }
            return lhs;
        }
        bool parseAnd(){
            bool lhs=parseNot();
            while(true){ skipWs();
                if(pos+1<s.size() && s[pos]=='&' && s[pos+1]=='&'){ pos+=2; lhs=lhs&&parseNot(); }
                else break; }
            return lhs;
        }
        bool parseNot(){
            skipWs();
            if(pos<s.size() && s[pos]=='!'){ pos++; return !parseNot(); }
            return parseComparison();
        }
        bool parseComparison(){
            skipWs();
            // parenthesised sub-expression
            if(pos<s.size() && s[pos]=='('){
                pos++;
                bool v=parseOr();
                skipWs();
                if(pos<s.size() && s[pos]==')') pos++;
                return v;
            }
            string lhsVal=readAtom();
            skipWs();
            // look for a comparison operator (two-char first, then one-char)
            string op;
            if(pos+1<s.size()){
                string two=s.substr(pos,2);
                if(two=="==" || two=="!=" || two=="<=" || two==">="){ op=two; pos+=2; }
            }
            if(op.empty() && pos<s.size() && (s[pos]=='<' || s[pos]=='>')){ op=string(1,s[pos]); pos++; }
            if(op.empty()){
                // no comparison — treat atom as boolean (non-empty and non-"0")
                return !lhsVal.empty() && lhsVal!="0";
            }
            skipWs();
            string rhsVal=readAtom();
            // try numeric comparison
            try{
                int lhs=stoi(lhsVal), rhs=stoi(rhsVal);
                if(op=="==") return lhs==rhs;
                if(op=="!=") return lhs!=rhs;
                if(op=="<")  return lhs<rhs;
                if(op==">")  return lhs>rhs;
                if(op=="<=") return lhs<=rhs;
                if(op==">=") return lhs>=rhs;
            } catch(...){
                // string fallback for == and !=
                if(op=="==") return lhsVal==rhsVal;
                if(op=="!=") return lhsVal!=rhsVal;
            }
            return false;
        }
        bool parseExpr(){ return parseOr(); }
    };
    Eval e{expr, 0, definedSymbols};
    return e.parseExpr();
}

// Skip tokens until ##else / ##endif at the current nesting depth (for ## compile-time conditionals).
void bglParser::skipBglConditionalBlock(abstractObject& ctx){
    int depth = 1;
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside ##ifdef block."); return; }
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("##ifdef") || t.is("##ifndef")) { depth++; continue; }
        if(t.is("##endif")) { depth--; if(depth==0) return; continue; }
        if(depth==1 && t.is("##else")){
            processDirective(t, ctx);
            return;
        }
    }
}

// Skip tokens until #elif / #else / #endif at the current nesting depth.
// On return, the terminating directive has been consumed and is returned via `found`
// so the caller knows whether it was #elif, #else, or #endif.
// nestd starts at 1 (we are inside one #if).
void bglParser::skipConditionalBlock(abstractObject& ctx){
    int depth = 1;
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #if block."); return; }
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("#if"))    { depth++; continue; }
        if(t.is("#endif")) { depth--; if(depth==0) return; continue; }
        if(depth==1 && (t.is("#elif") || t.is("#else"))){
            // hand back to processDirective by re-processing this directive
            processDirective(t, ctx);
            return;
        }
    }
}

bool bglParser::processDirective(token directive, abstractObject& contextObj){

    token tok;
    switch(directive.chk()){
        case chk("#include"):{
            token next = file.getToken();
            // Optional include: #include ?"file" or #include ?<file> — skip silently if not found
            bool isOptional = false;
            if(next.is("?")){
                isOptional = true;
                next = file.getToken();
            }
            if(next.isString()){
                // Quoted form: search source dir + bglIncludePaths for a .bgl file
                string includeName = next.value;
                if(includeName.size() >= 2 && includeName.front()=='"' && includeName.back()=='"')
                    includeName = includeName.substr(1, includeName.size()-2);
                filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
                string resolved = resolveIncludePath(includeName, ".bgl", curDir, beguilerSettings.includePaths);
                if(!resolved.empty())
                    parseFile(resolved);
                else if(!isOptional)
                    parsingError(format("#include: file '{0}' not found", includeName));
            } else if(next.is("<")){
                // Angle-bracket form: search lib path for sub-paths like <bindings/i6StandardLibrary>
                string includeName;
                token t = file.getToken();
                while(!t.is(">") && !t.is(eTokenType::eof)){
                    includeName += t.originalValue.empty() ? t.value : t.originalValue;
                    t = file.getToken();
                }
                filesystem::path libPath = findCaseInsensitive(settings.libPath, includeName + ".bgl");
                if(filesystem::exists(libPath))
                    parseFile(libPath.string());
                else if(!isOptional)
                    parsingError(format("#include: file '<{0}>' not found", includeName));
            }
            return false;
            break;
        }    
        case chk("#once"):{
            // Register the current file so that any future #include of it is silently skipped.
            string curFile = filesystem::canonical(filesystem::absolute(file.currentLocation().file)).string();
            onceFiles.insert(curFile);
            return false;
            break;
        }
        case chk("#startup"):{
            // Collect the raw I6 body for emission inside bglInit().
            // Deduplicated per source file so re-including a file doesn't register its blocks twice.
            string curFile = filesystem::absolute(file.currentLocation().file).string();
            file.getToken(token::braceOpen);
            string body = file.getRawTextThroughClosingBrace();
            if(!startupFiles.count(curFile)){
                startupFiles.insert(curFile);
                languageService.startupBlocks.push_back(body);
            }
            return false;
            break;
        }
        case chk("#using"):{
            // Import a class or object's members into the current file's scope
            token target = file.getToken();
            string targetName = target.value;
            transform(targetName.begin(), targetName.end(), targetName.begin(), ::tolower);
            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(targetName));
            if(!cls){
                // Try as a global object — import its class's members
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == targetName){ cls = od->objectClass; break; }
            }
            if(!cls){
                parsingWarning(format("#using '{0}': not a declared class or object; directive ignored",
                    target.originalValue.empty() ? target.value : target.originalValue));
                return false;
            }
            usingImports.push_back(cls);
            return false;
        }
        case chk("#emitfirst"):{
            // Collect raw I6 for emission after ICL headers, before bglInit.
            string curFile = filesystem::absolute(file.currentLocation().file).string();
            file.getToken(token::braceOpen);
            string body = file.getRawTextThroughClosingBrace();
            if(!emitFirstFiles.count(curFile)){
                emitFirstFiles.insert(curFile);
                languageService.emitFirstBlocks.push_back(body);
            }
            return false;
            break;
        }
        case chk("#emitlast"):{
            // Collect raw I6 for emission at the end of the I6 output.
            string curFile = filesystem::absolute(file.currentLocation().file).string();
            file.getToken(token::braceOpen);
            string body = file.getRawTextThroughClosingBrace();
            if(!emitLastFiles.count(curFile)){
                emitLastFiles.insert(curFile);
                languageService.emitLastBlocks.push_back(body);
            }
            return false;
            break;
        }
        case chk("#includei6"):{
            // Optional: #includeI6 ?"file" — skip silently if not found
            bool i6Optional = false;
            if(file.peekToken().is("?")){
                file.getToken(); // consume '?'
                i6Optional = true;
            }
            token filename = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string innerPath = filename.value;
            if(innerPath.size() >= 2 && innerPath.front()=='"' && innerPath.back()=='"')
                innerPath = innerPath.substr(1, innerPath.size()-2);
            // Resolve the file: search source dir + bglIncludePaths + i6IncludePaths (fallback)
            filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
            string resolved = resolveIncludePath(innerPath, "", curDir, beguilerSettings.includePaths);
            if(resolved.empty()){
                // Also try with .h extension
                resolved = resolveIncludePath(innerPath, ".h", curDir, beguilerSettings.includePaths);
            }
            if(resolved.empty()){
                if(!i6Optional)
                    parsingError(format("#includeI6: file '{0}' not found", innerPath));
                return false;
            }
            // Emit with the resolved absolute path
            string emitPath = "\"" + rewritePathSeps(resolved) + "\"";
            string nodeText = format("#include {0};", emitPath);
            // Claim the pre-scan stub if present (preserves source ordering in output)
            bool claimed = false;
            for(typeDef* g : languageService.globals){
                if(auto* raw = dynamic_cast<i6RawNode*>(g)){
                    if(raw->isPrePassStub && raw->text.find("#include") == 0){
                        // Match by the original filename since the stub was registered with the unresolved path
                        string stubPath = raw->text;
                        size_t q1 = stubPath.find('"'), q2 = stubPath.rfind('"');
                        if(q1 != string::npos && q2 > q1){
                            string stubFile = stubPath.substr(q1+1, q2-q1-1);
                            // Match if the stub filename ends with the original include name
                            if(stubFile.find(rewritePathSeps(innerPath)) != string::npos){
                                raw->text = nodeText;
                                raw->isPrePassStub = false;
                                claimed = true;
                                break;
                            }
                        }
                    }
                }
            }
            if(!claimed){
                i6RawNode& node = *(new i6RawNode());
                node.text = nodeText;
                languageService.globals.push_back(&node);
            }
            return false;
        }    
        case chk("#i6"):{
            token t=file.getToken();
            i6RawNode& node=*(new i6RawNode());
            if(t.is(token::braceOpen)){ //multi line — read raw chars to preserve I6 syntax
                node.text = file.getRawTextThroughClosingBrace();
            }
            else{ //single line only — read raw chars to end of line
                node.text = t.value;
                char c = file.readChar();
                while(c != '\n' && c != EOF){
                    node.text += c;
                    c = file.readChar();
                }
            }
            functionDef* func = dynamic_cast<functionDef*>(&contextObj);
            statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;
            if(body != nullptr){
                body->statements.push_back(dynamic_cast<statement*>(&node));
            } else {
                // Claim the pre-scan placeholder to preserve source ordering
                bool claimed = false;
                for(typeDef* g : languageService.globals){
                    if(auto* raw = dynamic_cast<i6RawNode*>(g)){
                        if(raw->isPrePassStub && raw->text == "#i6_placeholder"){
                            raw->text = node.text;
                            raw->isPrePassStub = false;
                            claimed = true;
                            break;
                        }
                    }
                }
                if(!claimed) languageService.globals.push_back(&node);
            }
            return false;
            break;
        }
        case chk("#define"):{
            token sym = file.getToken(eTokenType::identifier);
            // optional value on the same line — skip horizontal whitespace only (not newlines)
            while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
            token val = file.getBasicToken(true);
            string valStr;
            if(val.isNot("\n") && val.isNot(eTokenType::eof)){
                valStr = val.value;
                // consume rest of line
                token rest = file.getBasicToken(true);
                while(rest.isNot("\n") && rest.isNot(eTokenType::eof)) rest = file.getBasicToken(true);
            }
            definedSymbols[sym.value] = valStr;
            return false;
        }
        case chk("#undef"):{
            token sym = file.getToken(eTokenType::identifier);
            definedSymbols.erase(sym.value);
            return false;
        }
        case chk("#if"):{
            // collect condition text up to end of line
            string condText;
            token t = file.getBasicToken(true);
            while(t.isNot("\n") && t.isNot(eTokenType::eof)){ condText += t.value; t = file.getBasicToken(true); }
            if(!evaluateCondition(condText))
                skipConditionalBlock(contextObj);
            return false;
        }
        case chk("#elif"):{
            // reached here only when a prior #if branch was TRUE — skip to #endif
            int depth = 1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #elif block."); return false; }
                if(t.is("#if"))    depth++;
                if(t.is("#endif")) depth--;
            }
            return false;
        }
        case chk("#else"):{
            // reached here only when a prior #if/#elif branch was TRUE — skip to #endif
            int depth = 1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #else block."); return false; }
                if(t.is("#if"))    depth++;
                if(t.is("#endif")) depth--;
            }
            return false;
        }
        case chk("#endif"):{
            // consumed as a no-op (skipConditionalBlock handles it, but a stray #endif is harmless)
            return false;
        }

        // ## compile-time conditionals — evaluated by the Beguile transpiler; emit nothing when false
        case chk("##ifdef"):{
            token sym = file.getToken(eTokenType::identifier);
            if(definedSymbols.find(sym.value) == definedSymbols.end())
                skipBglConditionalBlock(contextObj);
            return false;
        }
        case chk("##ifndef"):{
            token sym = file.getToken(eTokenType::identifier);
            if(definedSymbols.find(sym.value) != definedSymbols.end())
                skipBglConditionalBlock(contextObj);
            return false;
        }
        case chk("##else"):{
            // reached only when the preceding ##ifdef/##ifndef branch was TRUE — skip to ##endif
            int depth = 1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside ##else block."); return false; }
                if(t.is("##ifdef") || t.is("##ifndef")) depth++;
                if(t.is("##endif")) depth--;
            }
            return false;
        }
        case chk("##endif"):{
            // consumed as a no-op (skipBglConditionalBlock handles it)
            return false;
        }

        case chk("#beguilersettings"):{
            return processBeguilerSettings();
        }
        case chk("#message"):{
            token msg = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string text = msg.value;
            if(text.size()>=2 && text.front()=='"' && text.back()=='"') text = text.substr(1,text.size()-2);
            cout << text << endl;
            return false;
        }
        case chk("#error"):{
            token msg = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string text = msg.value;
            if(text.size()>=2 && text.front()=='"' && text.back()=='"') text = text.substr(1,text.size()-2);
            return parsingError(text);
        }
        case chk("#warning"):{
            token msg = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string text = msg.value;
            if(text.size()>=2 && text.front()=='"' && text.back()=='"') text = text.substr(1,text.size()-2);
            parsingWarning(text);
            return false;
        }
        case chk("#exit"):{
            throw exitFileSignal{};
        }
    }
    // ##VerbName is an I6 action-constant syntax that Beguile handles automatically.
    if(directive.value.rfind("##", 0) == 0){
        string verbName = directive.value.substr(2);
        return parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly — the '##' prefix is emitted automatically by the verb type's operator ==.", verbName));
    }
    return parsingError("Unrecognized directive '" + directive.value + "'.");
}
bool bglParser::processRoutineDeclaration(token returnType, token name, abstractObject& contextObject, bool isExternal, bool isEmitter, bool isReplace){
    functionDef& funcDef=*(new functionDef());
    funcDef.name=(string) name; funcDef.displayName=name.originalValue;
    funcDef.src = file.currentLocation();
    funcDef.returnType=languageService.getType((string) returnType);
    funcDef.isExternal=isExternal;
    funcDef.isEmitter=isEmitter;

    processParameterList(funcDef);

    // ── replace chaining (non-emitter): rename existing before body parse ──
    // For non-emitter replace, we rename the existing function to a mangled name
    // BEFORE parsing the body so that replaced() calls can resolve during parsing.
    // Emitter replace keeps the simpler body-swap path (no chaining).
    if(isReplace && !isEmitter){
        functionDef* existing = nullptr;
        for(typeDef* g : languageService.globals){
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                if(fd->name != funcDef.name) continue;
                if(fd->isPrePassStub) continue; // pre-scan stub doesn't count as a real definition
                existing = fd;
                break;
            }
        }
        if(existing){
            // Determine N for mangled name: count existing _bgl_replaced_NAME_* in globals
            int n = 0;
            string prefix = "_bgl_replaced_" + funcDef.name + "_";
            for(typeDef* g : languageService.globals)
                if(auto* fd = dynamic_cast<functionDef*>(g))
                    if(fd->name.rfind(prefix, 0) == 0) n++;
            string mangledName = prefix + to_string(n);
            existing->name = mangledName;
            funcDef.replacedTarget = mangledName;
            funcDef.replacedFunc = existing;
        } else {
            parsingWarning(format("replace: no existing global function '{0}' found; treating as new definition", funcDef.name));
        }
    }

    if(isEmitter && file.peekToken().is(token::endStatement)){
        // Semicolon-terminated emitter: pass-through (value unchanged)
        file.getToken(); // consume ';'
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=" $self";
        funcDef.body=&rawblock;
    } else if(isEmitter){
        file.getToken(token::braceOpen);
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=file.getRawTextThroughClosingBrace();
        funcDef.body=&rawblock;
    } else {
        file.getToken(token::braceOpen);
        funcDef.body=new statementBlock();
        functionDef* savedFunc = currentFunc;
        currentFunc = &funcDef;
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatementV2(funcDef)==false){
        }
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
            parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    }

    // ── replace: emitter body-swap (existing behavior) ──
    if(isReplace && isEmitter){
        for(typeDef* g : languageService.globals){
            if(auto* existing = dynamic_cast<functionDef*>(g)){
                if(existing->name != funcDef.name) continue;
                if(existing->params.size() != funcDef.params.size()) continue;
                bool paramsMatch = true;
                for(size_t i = 0; i < funcDef.params.size() && paramsMatch; i++)
                    if(funcDef.params[i]->type.name != existing->params[i]->type.name)
                        paramsMatch = false;
                if(!paramsMatch) continue;
                existing->body = funcDef.body;
                existing->isEmitter = funcDef.isEmitter;
                existing->returnType = funcDef.returnType;
                return false;
            }
        }
        parsingWarning(format("replace: no existing emitter '{0}' found with matching signature; treating as new definition", funcDef.name));
    }

    // ── replace chaining: dead code elimination ──
    // If replaced() was never called in the body, the predecessor (and its entire
    // backward chain) will never execute — mark them dead so the emitter skips them.
    if(isReplace && !isEmitter && funcDef.replacedFunc && !funcDef.replacedWasCalled){
        functionDef* f = funcDef.replacedFunc;
        while(f){
            f->isReplacedDead = true;
            f = f->replacedFunc;
        }
    }

    // If a pre-pass stub exists for this name, replace its pointer in globals in-place
    for(typeDef*& g : languageService.globals){
        if(auto* stub = dynamic_cast<functionDef*>(g)){
            if(stub->name == funcDef.name && stub->isPrePassStub){
                g = &funcDef;  // replace stub pointer with full definition
                return false;
            }
        }
    }
    languageService.globals.push_back(&funcDef);
    return false;
}

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
        if(!typeName.empty() && typeName != "var" && !expr->resolvedType.empty()
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

void bglParser::processArrayMember(objectDef& obj){
    file.getToken("<");
    string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
    file.getToken(">");
    token propName = file.getToken(eTokenType::identifier);

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
            file.getToken(token::braceClose);  // closing } of this rule
            // Add to verb's grammarLines if applicable
            if(auto* vod = dynamic_cast<verbObjectDef*>(&obj)){
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                vod->grammarLines.push_back(gl);
            }
            obj.members.push_back(&rd);
            token sep = file.getToken({token::comma, token::braceClose});
            if(sep.is(token::braceClose)) break;
            inner = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        return;
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
    for(typeMember* m : obj.members)
        if(m->name == arrDecl.name)
            parsingError(format("object '{0}': member '{1}' is already defined", obj.dName(), arrDecl.dName()));
    obj.members.push_back((typeMember*)&arrDecl);
}

void bglParser::processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace){
    functionDef& funcDef = *(new functionDef());
    funcDef.name = (string)name;
    funcDef.returnType = languageService.getType((string)returnType);
    processParameterList(funcDef);
    file.getToken(token::braceOpen);
    funcDef.body = new statementBlock();
    functionDef* savedFunc = currentFunc;
    currentFunc = &funcDef;
    openCompileContext(eCompileContext::codeBlock);
    while(processNextStatementV2(funcDef) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    currentFunc = savedFunc;
    if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    // Check class hierarchy for shadowed methods — warn if 'replace' not specified
    if(!isReplace && !funcDef.name.empty()){
        string shadowedFrom;
        bool shadowedIsDefault = false;
        function<void(classDef*)> searchClass = [&](classDef* c){
            if(!shadowedFrom.empty() || !c) return;
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == funcDef.name){ shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault; return; }
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
        // No stub — check for duplicates
        bool replaced = false;
        if(isReplace){
            for(size_t i = 0; i < obj.members.size(); i++)
                if(auto* fd = dynamic_cast<functionDef*>(obj.members[i]))
                    if(fd->name == funcDef.name && fd->params.size() == funcDef.params.size()){
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
                    if(fd->name == funcDef.name && fd->params.size() == funcDef.params.size())
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
        for(typeMember* m : obj.members)
            if(m->name == prop.name)
                parsingError(format("object '{0}': member '{1}' is already defined", obj.dName(), prop.dName()));
        obj.members.push_back((typeMember*)&prop);
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
    // Search the object's declared class first (if any), then fall back to the base 'object' class
    std::function<void(classDef*)> searchClass = [&](classDef* cls){
        if(!cls || !propTypeName.empty()) return;
        for(typeMember* m : cls->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == nameTok.value){ propTypeName = vd->type.name; return; }
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
    rawblock.i6Body = file.getRawTextThroughClosingBrace();
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
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    token symbol = file.getToken({token::bracketOpen, token::assignment, token::endStatement});
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
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias);
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
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias);
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
                    else { rawblock.i6Body = file.getRawTextThroughClosingBrace(); if(!funcHasParens) funcDef.isValueEmitter = true; }
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
        if(q.isAlias)   parsingError("'alias' is not valid inside an object body");
        if(q.isConst)   parsingError("'const' is not valid inside an object body (use on the property type)");
        if(q.isStatic)  parsingError("'static' is not valid inside an object body");
        if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
        if(q.isEmitter){
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
                rawblock.i6Body = file.getRawTextThroughClosingBrace();
                if(!hasParens) funcDef.isValueEmitter = true;
            }
            funcDef.body = &rawblock;
            if(!replaceStubMember(newObj.members, funcDef))
                newObj.members.push_back((typeMember*)&funcDef);
        } else if(tok.value == "array")
            processArrayMember(newObj);
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

bool bglParser::processBeguilerSettings(){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("beguilerSettings is only allowed in global context");

    // Use the global singleton — CLI-set fields take precedence; source block fills gaps.
    beguilerSettingsDef& cfg = beguilerSettings;

    // Look up the schema class for property name/type validation
    classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilerSettingstype"));

    file.getToken(token::braceOpen);
    token tok = file.getToken();
    while(!tok.is(token::braceClose) && !tok.is(eTokenType::eof)){
        // each entry: [<type>] <name> = <value> ;
        // Type is optional: if the second token is '=', the first is the property name.
        // Otherwise the first is the type and the second is the property name.
        token propName;
        token second = file.getToken();
        if(second.is(token::assignment)){
            propName = tok;
        } else {
            if(!tok.is(eTokenType::identifier) && !tok.isDataType())
                parsingError(format("Expected type or property name in #beguilerSettings, got '{0}'", tok.value));
            propName = second;
            file.getToken(token::assignment);
        }

        string key = propName.value; // already lowercase (lexer normalises identifiers)

        // Validate property name and look up its declared type from the schema class
        string memberType;
        if(schema){
            typeMember* m = findMemberInHierarchy(schema, [&](typeMember* tm){ return tm->name == key; });
            if(!m)
                parsingError(format("Unknown beguilerSettings property '{0}'", key));
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                memberType = vd->type.name;
        }

        token val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::rawQuote, eTokenType::integer});
        // Allow optional qualified enum form: EnumType.Member — consume the dot and member, use member as value
        if(val.is(eTokenType::identifier) && file.peekToken().is(".")){
            file.getToken(".");
            token member = file.getToken(eTokenType::identifier);
            // Validate that qualifier matches the declared member type
            if(!memberType.empty() && val.value != memberType)
                parsingError(format("beguilerSettings property '{0}' expects a {1} value, got '{2}'", key, memberType, val.value));
            val = member;
        }
        file.getToken(token::endStatement);

        // Type-check the value against the schema-declared member type
        if(!memberType.empty()){
            bool expectInt = (memberType == "int");
            bool expectStr = (memberType == "string");
            if(expectInt && !val.is(eTokenType::integer))
                parsingError(format("beguilerSettings property '{0}' expects an int, got '{1}'", key, val.value));
            if(expectStr && !val.isString())
                parsingError(format("beguilerSettings property '{0}' expects a string, got '{1}'", key, val.value));
            if(!expectInt && !expectStr && !val.is(eTokenType::identifier))
                parsingError(format("beguilerSettings property '{0}' expects a {1} value, got '{2}'", key, memberType, val.value));
        }

        string strVal = val.value;
        if(val.isString()){
            strVal = val.unescape(val.value);
            if(strVal.size() >= 2 && strVal.front() == '"' && strVal.back() == '"')
                strVal = strVal.substr(1, strVal.size() - 2);
        }

        // Per-property storage and semantic validation
        if(key == "errorformat"){
            string upper = strVal;
            transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if(upper != "E1" && upper != "E2")
                parsingError(format("beguilerSettings property 'errorformat' must be E1 or E2, got '{0}'", strVal));
            if(cfg.errorFormat.empty()) cfg.errorFormat = upper.substr(1); // store just the digit
        }
        else if(key == "beguilibpath"){ if(cfg.beguiLibPath.empty())    cfg.beguiLibPath = rewritePathSeps(strVal); }
        else if(key == "informpath"){   if(cfg.informBinaryPath.empty()) cfg.informBinaryPath = rewritePathSeps(strVal); }
        else if(key == "informname"){   if(cfg.informName.empty())       cfg.informName = rewritePathSeps(strVal); }
        else if(key == "outputpath"){   if(cfg.outputPath.empty())       cfg.outputPath = rewritePathSeps(strVal); }
        else if(key == "includepaths"){
            string path = rewritePathSeps(strVal);
            if(find(cfg.includePaths.begin(), cfg.includePaths.end(), path) == cfg.includePaths.end())
                cfg.includePaths.push_back(path);
        }
        else if(key == "release"){ if(cfg.release == 0) cfg.release = stoi(strVal); }
        else if(key == "serial"){
            if(cfg.serial.empty()){
                if(strVal.size() != 6 || !all_of(strVal.begin(), strVal.end(), ::isdigit))
                    parsingError("beguilerSettings property 'serial' must be exactly 6 digits (e.g. \"250328\")");
                cfg.serial = strVal;
            }
        }
        else if(key == "framepoolsize"){
            int sz = stoi(strVal);
            if(sz < 1) parsingError("beguilerSettings property 'framePoolSize' must be at least 1");
            if(cfg.framePoolSize == -1) cfg.framePoolSize = sz;  // -1 = unset sentinel
        }
        else if(key == "target"){
            if(languageService.getEnumType(strVal) != "etarget")
                parsingError(format("Invalid target '{0}'. Must be a value of eTarget (Glulx, Z3, Z5, or Z8).", strVal));
            if(cfg.target.empty()) cfg.target = strVal;
        }
        else if(key == "rewritepaths"){
            if(!cfg.rewritePaths.has_value()){
                if(strVal == "true")       cfg.rewritePaths = true;
                else if(strVal == "false") cfg.rewritePaths = false;
                else parsingError(format("beguilerSettings property 'rewritePaths' expects true or false, got '{0}'", strVal));
            }
        }
        else if(key == "generateblorb"){
            if(strVal == "true")       cfg.blorbEnabled = true;
            else if(strVal == "false") cfg.blorbEnabled = false;
            else parsingError(format("beguilerSettings property 'generateBlorb' expects true or false, got '{0}'", strVal));
        }
        else if(key == "blorbassetpath"){ if(cfg.blorbAssetPath.empty()) cfg.blorbAssetPath = rewritePathSeps(strVal); }
        else if(key == "author"){         if(cfg.author.empty())         cfg.author         = strVal; }
        else if(key == "title"){          if(cfg.title.empty())          cfg.title          = strVal; }
        else if(key == "headline"){       if(cfg.headline.empty())       cfg.headline       = strVal; }
        else if(key == "genre"){          if(cfg.genre.empty())          cfg.genre          = strVal; }
        else if(key == "description"){    if(cfg.description.empty())    cfg.description    = strVal; }
        else if(key == "language"){       if(cfg.language.empty())       cfg.language       = strVal; }
        else if(key == "series"){         if(cfg.series.empty())         cfg.series         = strVal; }
        else if(key == "seriesnumber"){   if(cfg.seriesNumber == 0)      cfg.seriesNumber   = stoi(strVal); }
        else if(key == "firstpublished"){ if(cfg.firstPublished.empty()) cfg.firstPublished = strVal; }
        else if(key == "forgiveness"){    if(cfg.forgiveness.empty())    cfg.forgiveness    = strVal; }
        else if(key == "ifid"){           if(cfg.ifid.empty())           cfg.ifid           = strVal; }

        tok = file.getToken();
    }

    // Apply path overrides immediately so subsequent includes use them
    // (informBinaryPath is intentionally excluded here — resolved after all parsing in beguiler.cpp)
    if(!cfg.beguiLibPath.empty()) settings.libPath = cfg.beguiLibPath;
    if(!cfg.outputPath.empty() && settings.outputPath.empty()) settings.outputPath = cfg.outputPath;

    return false;
}

// Apply default values declared on beguilerSettingsType members to any settings fields
// that were never set by a #beguilerSettings block.  Called once after all parsing is done.
void bglParser::applySchemaDefaults(){
    beguilerSettingsDef& cfg = beguilerSettings;
    classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilerSettingstype"));
    if(!schema) return;

    for(typeMember* m : schema->members){
        variableDeclaration* vd = dynamic_cast<variableDeclaration*>(m);
        if(!vd || !vd->declaredExpressionValue) continue;

        string key = vd->name; // already lowercase
        string defVal = vd->declaredExpressionValue->text();
        // Strip surrounding quotes if the default is a string literal
        if(defVal.size() >= 2 && defVal.front()=='"' && defVal.back()=='"')
            defVal = defVal.substr(1, defVal.size()-2);

        if(key == "target"        && cfg.target.empty())       cfg.target = defVal;
        else if(key == "framepoolsize" && cfg.framePoolSize == -1) cfg.framePoolSize = stoi(defVal);
        else if(key == "errorformat"  && cfg.errorFormat.empty()){
            string upper = defVal;
            transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if(upper == "E1" || upper == "E2") cfg.errorFormat = upper.substr(1);
        }
        else if(key == "release"      && cfg.release == 0)         cfg.release = stoi(defVal);
        else if(key == "rewritepaths" && !cfg.rewritePaths.has_value()) cfg.rewritePaths = (defVal == "true");
        else if(key == "blorbassetpath" && cfg.blorbAssetPath.empty()) cfg.blorbAssetPath = defVal;
    }

    // Inject compile-time target symbols if not already set by extractBlorbSettings
    if(definedSymbols.find("target_glulx") == definedSymbols.end()
       && definedSymbols.find("target_zcode") == definedSymbols.end()){
        string t = cfg.target;
        transform(t.begin(), t.end(), t.begin(), ::tolower);
        if(t == "glulx")
            definedSymbols["target_glulx"] = "";
        else if(t.size() == 2 && t[0] == 'z' && isdigit(t[1]))
            definedSymbols["target_zcode"] = string(1, t[1]);
    }
}
#pragma endregion

//-------------------------------------------------------------------------------------------------------------------------------
// Throw an error, formatting the output to point to the current line
bool bglParser::parsingError(string msg){
    string errorMessage;
    if(file.getNumberOfOpenFiles()>0) {
        // Prefer the statement-start location if set; fall back to current stream position.
        string fileName;
        int curLine, curCol;
        if(currentStatementSrc.line > 0){
            fileName = currentStatementSrc.file;
            curLine  = currentStatementSrc.line;
            curCol   = 1;
        } else {
            auto detail = file.getCurrentFileDetail();
            fileName = get<1>(detail);
            curLine  = get<2>(detail);
            curCol   = get<3>(detail);
        }
        errorMessage=format("{0}:{1}:{2}: error: {3}",fileName,curLine,curCol,msg);
    }
    else{
        errorMessage=msg;
    }
    
    if(lspMode) {
        lspErrors.push_back(errorMessage);
        throw lspRecoverySignal(errorMessage);
    }
    throw runtime_error(errorMessage);
    return true; //won't ever actually run
}
void bglParser::parsingWarning(string msg){
    string warningMessage;
    if(file.getNumberOfOpenFiles()>0) {
        string fileName;
        int curLine, curCol;
        if(currentStatementSrc.line > 0){
            fileName = currentStatementSrc.file;
            curLine  = currentStatementSrc.line;
            curCol   = 1;
        } else {
            auto detail = file.getCurrentFileDetail();
            fileName = get<1>(detail);
            curLine  = get<2>(detail);
            curCol   = get<3>(detail);
        }
        warningMessage=format("{0}:{1}:{2}: warning: {3}",fileName,curLine,curCol,msg);
    }
    else{
        warningMessage=msg;
    }
    cerr << warningMessage << endl;
}

#pragma region Compile Context management
//===============================================================================================================================
// Routines to manage the compile context 
//-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
void bglParser::openCompileContext(eCompileContext newScope){
    compileContextStack.push_front(newScope); 
}
string bglParser::contextToString(eCompileContext context){
    switch(context){
        case eCompileContext::global:
            return "global";
            break;
        case eCompileContext::objectDef:
            return "object definition";
            break;
        case eCompileContext::codeBlock:
            return "code block";
            break;
        default:
            return "unknown compile context";
    }  
}
void bglParser::closeCompileContext(eCompileContext expectedScope){
    eCompileContext oldScope=compileContextStack.front();
    if(oldScope!=expectedScope) parsingError(format("Internal Error: Attempting to close compile context '{0}' but current context is '{1}'.", contextToString(expectedScope), contextToString(oldScope)));
    compileContextStack.pop_front();
}
eCompileContext bglParser::getCurrentCompileContext(){ 
    if(compileContextStack.size()==0) return eCompileContext::noContext; //we register base data types before we even begin parsing.  So lets assume global context even before any context is officially opened.
    return compileContextStack.front();
}
#pragma endregion Compile Context management
// classDef& bglParser::registerDefinition(classDef newType){
    
//     // classDef& el =languageService.registerType(newType);
    
//     // if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(&el);

//     // return el; 
// }
// typeDef& bglParser::registerDefinition(objectDef newType){
//     //typeDef el;
//     //typeDef& el =languageService.registerType(newType);
//     //if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(std::make_unique<classDef>(newClass));

//     return emptyTDef;
// }
// typeDef& bglParser::registerDefinition(enumDef newType){
//     typeDef el;
//     //typeDef& el =languageService.registerType(newType);
//     //if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(std::make_unique<classDef>(newClass));

//     return emptyTDef;
// }
// typeDef& bglParser::registerDefinition(string newTypeName){
//     //typeDef el;
//     //typeDef& el =languageService.registerType(newTypeName);
//     //if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(el);
//     return emptyTDef;
// }

//===============================================================================================================================
// Object extension: extend <objectName> { }
//===============================================================================================================================

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

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
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
        if(q.isAlias)   parsingError("'alias' is not valid inside an extend body");
        if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
        if(isExternalObj && !q.isEmitter){
            // For extern objects, only compound assignment on grammar is allowed
            // Check for += which is handled below; everything else is an error
            bool isCompound = false;
            if(tok.isDataType()){
                token peekName = file.peekToken();
                token peekOp = file.peekToken(2);
                if(peekOp.is("+=") || peekOp.is("-=")) isCompound = true;
            } else if(tok.is(eTokenType::identifier)){
                token peekOp = file.peekToken();
                if(peekOp.is("+=") || peekOp.is("-=")) isCompound = true;
            }
            if(!isCompound)
                parsingError(format("Cannot add members to extern object '{0}'; only compound assignment (+=) on collection members is allowed",
                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
        }
        if(q.isEmitter){
            if(isExternalObj)
                parsingError(format("Cannot add emitter methods to extern object '{0}'",
                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
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
            else { rawblock.i6Body = file.getRawTextThroughClosingBrace(); if(!hasParens) funcDef.isValueEmitter = true; }
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
            processArrayMember(*obj);
        else if(tok.isDataType()){
            // Check for += / -= compound assignment on a typed member
            token peekName = file.peekToken();
            token peekOp = file.peekToken(2);
            if((peekOp.is("+=") || peekOp.is("-=")) && peekName.is(eTokenType::identifier)){
                token memberName = file.getToken();
                token op = file.getToken();
                processExtendCompoundAssignment(*obj, memberName, op.value, vod);
            } else {
                processTypedMember(*obj, tok, memberIsReplace);
            }
        }
        else if(tok.is(eTokenType::identifier)){
            // Check for += / -= compound assignment (inferred type)
            token peekOp = file.peekToken();
            if(peekOp.is("+=") || peekOp.is("-=")){
                token op = file.getToken();
                processExtendCompoundAssignment(*obj, tok, op.value, vod);
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
// Supports: grammarRuleList, attributeList, array<T>.
void bglParser::processExtendCompoundAssignment(objectDef& obj, token memberName, const string& op, verbObjectDef* vod){
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
                                [&](const grammarLine& gl){ return gl.verbWord == rem.verbWord && gl.patternTokens == rem.patternTokens; }),
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
    // attributeList += / -=
    else if(memberType == "attributelist"){
        file.getToken(token::braceOpen);
        vector<string> attrs;
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
            attrs.push_back(t.value);
            t = file.getToken({token::comma, token::braceClose});
            if(t.is(token::comma)) t = file.getToken();
        }
        // Find existing attributeList member
        variableDeclaration* attrMember = nullptr;
        for(typeMember* m : obj.members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == memberNameStr){ attrMember = vd; break; }
        if(op == "+="){
            if(!attrMember){
                attrMember = new variableDeclaration();
                attrMember->name = memberNameStr;
                attrMember->type = languageService.getType("attributelist");
                attrMember->declaredExpressionValue = new initializerList();
                obj.members.push_back(attrMember);
            }
            auto* list = dynamic_cast<initializerList*>(attrMember->declaredExpressionValue);
            if(!list){ list = new initializerList(); attrMember->declaredExpressionValue = list; }
            for(const string& a : attrs){
                expression* elem = new expression();
                elem->tokens.push_back(a);
                elem->resolvedType = "attribute";
                list->elements.push_back(elem);
            }
        } else {
            // -= : remove attributes
            if(attrMember){
                if(auto* list = dynamic_cast<initializerList*>(attrMember->declaredExpressionValue)){
                    for(const string& a : attrs){
                        list->elements.erase(
                            remove_if(list->elements.begin(), list->elements.end(),
                                [&](expression* e){ return !e->tokens.empty() && e->tokens[0] == a; }),
                            list->elements.end());
                    }
                }
            }
        }
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

bool bglParser::processGrammarDeclaration(token nameOverride){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'grammar' declarations are only allowed in global context");

    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken(eTokenType::identifier);
    string grammarName = name.originalValue.empty() ? name.value : name.originalValue;

    // Peek inside the body to detect old-style grammar lines vs new-style member declarations.
    // Old-style: grammar VerbName { {.word, TOKEN}, ... } — first body token is '{'
    // New-style: grammar ObjName { grammarRule rule = ...; ... } — first body token is type/identifier
    file.getToken(token::braceOpen);
    token peek = file.peekToken();
    if(peek.is(token::braceOpen)){
        // Old-style: single-verb grammar lines — each becomes a grammarRuleDecl with inferred verb
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = "grammar";
        gtd.type = languageService.getType("grammarrulelist");
        gtd.verbName = grammarName;
        token tok = file.getToken();
        while(tok.isNot(token::braceClose)){
            tok.assert(token::braceOpen, "Expected '{' to start a grammar line");
            grammarLine gl = parseGrammarLineContent();
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = "grammar";
            rd.type = languageService.getType("grammarrule");
            rd.line = gl;
            rd.targetVerb = grammarName;
            gtd.rules.push_back(&rd);
            gl.targetVerb = grammarName;
            gtd.grammarLines.push_back(gl);
            tok = file.getToken({token::comma, token::braceClose});
            if(tok.is(token::comma)) tok = file.getToken();
        }
        languageService.globals.push_back(&gtd);
    } else {
        // New-style: grammar object with grammarRule members
        return processGrammarObjectDeclaration(grammarName);
    }
    return false;
}

// Parse a grammar object body with grammarRule members.
// Assumes outer '{' already consumed. grammarName is the object name.
// Members are: grammarRule name = {VerbRef, {.word, TOKEN, ...}};
// or array<grammarRule> name = {{VerbRef, {.word, ...}}, ...};
// Type can be inferred from the grammarTable class members.
bool bglParser::processGrammarObjectDeclaration(const string& grammarName){
    // Resolve grammarTable class for type inference
    classDef* gtClass = dynamic_cast<classDef*>(&languageService.getType("grammarrulelist"));

    // Create a grammarRuleListDecl to hold all the rules
    grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
    gtd.name = grammarName;
    gtd.type = languageService.getType("grammarrulelist");

    // Helper: resolve a verb reference token, return original-case verb name or error
    auto resolveVerbRef = [this](const token& verbTok) -> string {
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower)
                return verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        return "";
    };

    // Helper: parse a single grammarRule initializer {VerbRef, {.word, TOKEN, ...}}
    // Assumes outer '{' already consumed.
    auto parseGrammarRuleInit = [&](grammarRuleDecl& rd){
        // First element: verb reference
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        rd.targetVerb = resolveVerbRef(verbTok);
        file.getToken(token::comma);
        // Second element: grammar line pattern {.word, TOKEN, ...}
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Closing } of the outer initializer
        file.getToken(token::braceClose);
    };

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        // Determine type — explicit or inferred
        string memberType;
        bool isArray = false;
        if(tok.is("array")){
            // explicit array<grammarRule>
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            memberType = "grammarrule";
            isArray = true;
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.isDataType() && tok.value == "grammarrule"){
            // explicit grammarRule
            memberType = "grammarrule";
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.is(eTokenType::identifier)){
            // inferred type from grammarTable class
            // tok is the member name; infer type from class
            if(gtClass){
                // Check if assignment follows — peek for = or {
                token afterName = file.peekToken();
                if(afterName.is(token::assignment)){
                    // Look at initializer shape to determine type
                    // For now, we'll determine after reading the initializer
                    memberType = "grammarrule"; // default inference
                }
            }
            if(memberType.empty())
                parsingError(format("Cannot infer type for '{0}' in grammar object body", tok.value));
        } else {
            parsingError(format("Unexpected '{0}' in grammar object body", tok.value));
        }

        string memberName = tok.value;
        file.getToken(token::assignment);

        token first = file.getToken(token::braceOpen);

        // Validate shape matches declared type
        token peek2 = file.peekToken();
        if(isArray){
            // array<grammarRule>: must be {{VerbRef, {pattern}}, ...}
            if(!peek2.is(token::braceOpen))
                parsingError(format("array<grammarRule> '{0}' requires nested initializer list of rule pairs", memberName));
            token inner = file.getToken();
            while(inner.is(token::braceOpen)){
                grammarRuleDecl& rd = *(new grammarRuleDecl());
                rd.name = memberName;
                rd.type = languageService.getType("grammarrule");
                parseGrammarRuleInit(rd);
                gtd.rules.push_back(&rd);
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                gtd.grammarLines.push_back(gl);
                token sep = file.getToken({token::comma, token::braceClose});
                if(sep.is(token::braceClose)) break;
                inner = file.getToken();
            }
        } else {
            // grammarRule: must be {VerbRef, {pattern}} — not nested
            if(peek2.is(token::braceOpen))
                parsingError(format("grammarRule '{0}' takes a single rule initializer; use array<grammarRule> for multiple rules", memberName));
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = memberName;
            rd.type = languageService.getType("grammarrule");
            parseGrammarRuleInit(rd);
            gtd.rules.push_back(&rd);
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            gtd.grammarLines.push_back(gl);
        }

        // Consume optional semicolon after member declaration
        if(file.peekToken().is(token::endStatement)) file.getToken();

        tok = file.getToken();
    }

    languageService.globals.push_back(&gtd);
    return false;
}

// Parse a single grammar line's content: trigger word + pattern tokens.
// Assumes the opening '{' has already been consumed.
// Returns a grammarLine with verbWord and I6-ready patternTokens.
grammarLine bglParser::parseGrammarLineContent(){
    auto escDictWord = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch; return e;
    };
    auto makeI6Word = [&escDictWord](const token& t) -> string {
        string e = escDictWord(t.value);
        if(t.isPlural) return "'" + e + "/p'";
        if(e.size() == 1) return "'" + e + "//'";
        return "'" + e + "'";
    };

    grammarLine line;

    // First token: the verb trigger word — must be a singular dict word
    token trigger = file.getToken(eTokenType::dictionaryWord);
    line.verbWord = trigger.value;   // raw word, e.g. "put"

    // Remaining pattern tokens, comma-separated until }
    token tok = file.getToken({token::comma, token::braceClose});
    while(tok.is(token::comma)){
        // Optional opening paren — syntactic sugar for a dict-word alternatives group
        bool hasParens = file.peekToken().is("(");
        if(hasParens) file.getToken();  // consume '('

        token pt = file.getToken({eTokenType::dictionaryWord, eTokenType::identifier, eTokenType::dataType});

        if(pt.is(eTokenType::dictionaryWord)){
            // Dict word — may be chained with | alternatives: .word1 | .word2 | .word3
            string i6tok = makeI6Word(pt);
            while(file.peekToken().is("|")){
                file.getToken();  // consume '|'
                token alt = file.getToken(eTokenType::dictionaryWord);
                i6tok += "/" + makeI6Word(alt);
            }
            if(hasParens) file.getToken(")");  // consume ')'
            line.patternTokens.push_back(i6tok);
        } else {
            if(hasParens)
                parsingError("Parenthesized grammar group must contain dictionary word alternatives (.word | .word2 | ...)");

            // identifier or dataType in grammar pattern
            string tokenStr = pt.value;
            string display = pt.originalValue.empty() ? pt.value : pt.originalValue;

            // resolve declared type; apply i6name alias if present
            string resolvedType;
            for(typeDef* g : languageService.globals){
                if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                    if(vd->name == tokenStr){
                        resolvedType = vd->type.name;
                        if(!vd->i6name.empty()) tokenStr = vd->i6name;
                        break;
                    }
                if(auto* fd = dynamic_cast<functionDef*>(g))
                    if(fd->name == tokenStr){ resolvedType = "function"; break; }
            }

            if(file.peekToken().is("(")){
                // NOUN(Routine) / SCOPE(Routine) — parameterized grammar token
                if(resolvedType != "grammartoken")
                    parsingError(format("'{0}' in grammar pattern: only grammarToken types can be parameterized with (Routine)", display));
                file.getToken();  // consume '('
                token routine = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(")");  // consume ')'
                functionDef* rfd = nullptr;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == routine.value){ rfd = fd; break; }
                if(!rfd)
                    parsingError(format("'{0}' in '{1}({0})' does not name a declared global function", routine.value, display));
                if(rfd->returnType.name != "bool" && rfd->returnType.name != "ebool")
                    parsingError(format("'{0}' in '{1}({0})' must return bool (returns '{2}')", routine.value, display, rfd->returnType.name));
                tokenStr = pt.value + "=" + routine.value;  // I6 form: noun=Routine
            } else {
                // bare token: must be a declared grammarToken, attribute, or global function
                if(resolvedType != "grammartoken" && resolvedType != "attribute" && resolvedType != "function")
                    parsingError(format("'{0}' in grammar pattern is not a declared grammarToken, attribute, or global function", display));
            }
            line.patternTokens.push_back(tokenStr);
        }
        tok = file.getToken({token::comma, token::braceClose});
    }
    // tok is now } (closing brace of this line)
    return line;
}

// Parse a grammar line list: { {.put, held, .on, noun}, {.hang, held, .on, noun} }
// First token of each line is the verb trigger word (must be a dictionaryWord).
// Remaining tokens are pattern tokens: dict words become I6 'word', identifiers stay as-is.
vector<grammarLine> bglParser::parseGrammarLines(){
    vector<grammarLine> result;

    file.getToken(token::braceOpen);   // outer {
    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        tok.assert(token::braceOpen, "Expected '{' to start a grammar line");
        result.push_back(parseGrammarLineContent());

        // After }, expect , (more lines) or } (end of list)
        tok = file.getToken({token::comma, token::braceClose});
        if(tok.is(token::comma))
            tok = file.getToken();   // either { for next line, or } for end
    }
    return result;
}

bglParser parser;
