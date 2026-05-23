// ===============================================================================
// bglParserDirectives.cpp - directive + #beguilerSettings processing.
//
// Extracted from bglParser.cpp (Phase 1 of the refactor). Holds all `#`-prefixed
// directive handling plus the compile-time #if/#elif/#else condition evaluator.
//
// Public-facing entry points (all members of bglParser):
//   processDirective          - main dispatch for #include/#i6/#bgl/#if/#define/etc.
//   processBeguilerSettings   - parses #beguilerSettings { ... } block
//   applySchemaDefaults       - fills unset settings with defaults from beguilerSettingsType
//   processBglConditionals    - in-string ##ifdef/##ifndef inside emitter bodies
//   evaluateCondition         - compile-time condition expression evaluator
//   skipBglConditionalBlock   - skip body inside a false #if from #bgl scope
//   skipConditionalBlock      - skip body inside a false #if from top-level scope
// ===============================================================================
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype>
#include <optional>

#include "helpers.h"
#include "settings.h"
#include "typeDef.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"
#include "bglParserHelpers.h"

using namespace std;

// Forward declarations for free helpers defined in bglParser.cpp.
// Matches the pattern used in bglPreScanner.cpp.
string resolveIncludePath(const string& name, const string& ext, const filesystem::path& baseDir, const vector<string>& includePaths);
string rewritePathSeps(const string& path);
filesystem::path findCaseInsensitive(const filesystem::path& dir, const string& target);


// ===============================================================================
// processBglConditionals + evaluateCondition + skipBglConditionalBlock + skipConditionalBlock + processDirective
// ===============================================================================
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

            if(dir == "ifdef" || dir == "ifndef"){
                // Only `##if` is valid inside emitter bodies per spec §7.2. The single-hash
                // forms `#ifdef` / `#ifndef` exist as top-level directives, but their
                // double-hash counterparts were never part of the emitter-body grammar.
                parsingError(format("'##{0}' is not valid in an emitter body — use '##if <expr>' instead", dir));
            } else if(dir == "if"){
                string condText;
                while(pos < text.size() && text[pos] != '\n' && !(text[pos] == '#' && pos+1 < text.size() && text[pos+1] == '#'))
                    condText += text[pos++];
                while(!condText.empty() && (condText.back() == ' ' || condText.back() == ';')) condText.pop_back();
                bool condTrue = evaluateCondition(condText);
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

// Skip tokens in a false #if/#elif branch until #elif (true) / #else / #endif at the current
// nesting depth. On #elif-true or #else, resume normal parsing. On #endif, done. Handles nested
// #if correctly via depth counting. Records the skipped source-line range via
// recordInactiveRange() for LSP inactive-region reporting.
void bglParser::skipConditionalBlock(abstractObject& ctx){
    int depth = 1;
    int startLine1 = file.currentLocation().line;
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) {
            recordInactiveRange(startLine1, file.currentLocation().line);
            parsingError("Unexpected end of file inside #if block.");
            return;
        }
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("#if"))    { depth++; continue; }
        if(t.is("#endif")) {
            depth--;
            if(depth == 0) { recordInactiveRange(startLine1, t.src.line - 1); return; }
            continue;
        }
        if(depth == 1 && t.is("#elif")){
            // Evaluate this #elif's condition; on true, record and resume. On false, continue
            // skipping — extend the current dead range through the next terminator.
            int elifLine = t.src.line;
            string condText;
            token ct = file.getBasicToken(true);
            while(ct.isNot("\n") && ct.isNot(eTokenType::eof)){ condText += ct.value; ct = file.getBasicToken(true); }
            if(evaluateCondition(condText)) {
                recordInactiveRange(startLine1, elifLine - 1);
                return;
            }
            // continue skipping — the next terminator determines the range end
        }
        if(depth == 1 && t.is("#else")){
            recordInactiveRange(startLine1, t.src.line - 1);
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
                if(filesystem::exists(libPath)){
                    parseFile(libPath.string());
                    // Case-fold for these flag triggers — Beguile is case-insensitive but
                    // includeName accumulates t.originalValue (preserves user casing), so
                    // `<bglWorld>` and `<bglworld>` should both fire the same setup.
                    string ciInclude = includeName;
                    transform(ciInclude.begin(), ciInclude.end(), ciInclude.begin(), ::tolower);
                    if(ciInclude == "array"){
                        languageService.arrayInUse = true;
                        languageService.linqInUse  = true;
                    }
                    else if(ciInclude == "bglworld"){
                        languageService.worldInUse = true;
                    }
                    else if(ciInclude == "buf"){
                        languageService.bufInUse = true;
                    }
                }
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
            string body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
            if(!startupFiles.count(curFile)){
                startupFiles.insert(curFile);
                languageService.startupBlocks.push_back(body);
            }
            return false;
            break;
        }
        case chk("#using"):{
            // Import a class or object's members into the current file's scope.
            //   #using Class        — classes' members become visible
            //   #using object       — object's own members become visible (may include aliases)
            //   #using a.b.c        — walk the dot-path, resolving alias members along the way,
            //                         import the final class or object's members
            token first = file.getToken();
            string displayPath = first.originalValue.empty() ? first.value : first.originalValue;
            string curName = first.value;  // lowercased
            classDef*  curCls = dynamic_cast<classDef*>(&languageService.getType(curName));
            objectDef* curObj = nullptr;
            if(!curCls) {
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == curName){ curObj = od; break; }
            }
            if(!curCls && !curObj){
                parsingWarning(format("#using '{0}': not a declared class or object; directive ignored", displayPath));
                return false;
            }
            // Walk dotted path: at each `.member`, look up member on current scope and redirect
            while(file.peekToken().is(token::period)){
                file.getToken(); // consume '.'
                token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
                string memberName = memberTok.value;
                displayPath += "." + (memberTok.originalValue.empty() ? memberTok.value : memberTok.originalValue);
                // Find the member in the current scope
                typeMember* found = nullptr;
                if(curCls) for(typeMember* m : curCls->members) if(m->name == memberName){ found = m; break; }
                if(!found && curObj) for(typeMember* m : curObj->members) if(m->name == memberName){ found = m; break; }
                if(!found){
                    parsingWarning(format("#using '{0}': member not found; directive ignored", displayPath));
                    return false;
                }
                // Only variable declarations (type references, including alias members) can be traversed
                auto* vd = dynamic_cast<variableDeclaration*>(found);
                if(!vd){
                    parsingWarning(format("#using '{0}': '{1}' is not a type reference; directive ignored", displayPath, memberName));
                    return false;
                }
                // Follow the member — could resolve to a class (alias target) or an object.
                // For auto members on non-emitter objects, the declared type may be "object"
                // (base class) rather than the specific target. Use the initializer expression
                // name to find the actual target first, then fall back to the type name.
                string nextType = vd->type.name;
                string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                classDef*  nextCls = nullptr;
                objectDef* nextObj = nullptr;
                // Try initializer name first (most specific)
                if(!initName.empty()){
                    nextCls = dynamic_cast<classDef*>(&languageService.getType(initName));
                    if(!nextCls)
                        for(typeDef* g : languageService.globals)
                            if(auto* od = dynamic_cast<objectDef*>(g))
                                if(od->name == initName){ nextObj = od; break; }
                }
                // Fall back to declared type name
                if(!nextCls && !nextObj){
                    nextCls = dynamic_cast<classDef*>(&languageService.getType(nextType));
                    if(!nextCls)
                        for(typeDef* g : languageService.globals)
                            if(auto* od = dynamic_cast<objectDef*>(g))
                                if(od->name == nextType){ nextObj = od; break; }
                }
                if(!nextCls && !nextObj){
                    parsingWarning(format("#using '{0}': '{1}' has type '{2}' which is not importable; directive ignored",
                        displayPath, memberName, nextType));
                    return false;
                }
                curCls = nextCls;
                curObj = nextObj;
            }
            // Import the final scope
            if(curCls) usingImports.push_back(curCls);
            else if(curObj) usingObjectImports.push_back(curObj);
            return false;
        }
        case chk("#emitfirst"):{
            // Additive: every #emitfirst block contributes. Re-include cycles are protected
            // by `#once` (each BLR file that uses #emitfirst declares #once at the top), so
            // we don't dedup at this level. Multiple #emitfirst blocks in the same file —
            // common for .inf-mode with many #bgl islands — all register.
            file.getToken(token::braceOpen);
            languageService.emitFirstBlocks.push_back(file.getRawTextThroughClosingBrace(/*isI6Content=*/true));
            return false;
            break;
        }
        case chk("#emitlast"):{
            // Same additive policy as #emitfirst.
            file.getToken(token::braceOpen);
            languageService.emitLastBlocks.push_back(file.getRawTextThroughClosingBrace(/*isI6Content=*/true));
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
            functionDef* func = dynamic_cast<functionDef*>(&contextObj);
            statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;
            // Helper: install an i6RawNode at the current context — into the function body
            // when one exists, otherwise into globals (claiming the pre-scan placeholder so
            // source order is preserved).
            auto installI6Node = [&](i6RawNode* node){
                if(body != nullptr){
                    body->statements.push_back(node);
                } else {
                    // Replace the pre-scan placeholder slot with the new node so all fields
                    // (including composite `parts`) transfer naturally.
                    bool claimed = false;
                    for(size_t i = 0; i < languageService.globals.size(); i++){
                        if(auto* raw = dynamic_cast<i6RawNode*>(languageService.globals[i])){
                            if(raw->isPrePassStub && raw->text == "#i6_placeholder"){
                                languageService.globals[i] = node;
                                claimed = true;
                                break;
                            }
                        }
                    }
                    if(!claimed) languageService.globals.push_back(node);
                }
            };

            token t = file.getToken();
            if(!t.is(token::braceOpen)){
                // Single-line variant: read raw chars to end of line. No #bgl support here —
                // a single-line I6 statement is too small to need it.
                i6RawNode* node = new i6RawNode();
                node->src = file.currentLocation();
                node->text = t.value;
                char c = file.readChar();
                while(c != '\n' && c != EOF){ node->text += c; c = file.readChar(); }
                installI6Node(node);
                return false;
            }

            // Multi-line block: alternate between raw-I6 chunks and embedded #bgl{} regions.
            // Each #bgl{} is parsed as Beguile statements (code-block mode — declarations
            // disallowed) and appended in source order, so the emission preserves the natural
            // interleaving between raw I6 and Beguile.
            //
            // Inside a function body, raw chunks become i6RawNodes and bgl statements push
            // directly into body->statements. At global scope, both are accumulated into a
            // single composite i6RawNode whose `parts` vector preserves the interleaving for
            // emit-time rendering — Beguile statements assume a code-block context, so we
            // synthesize one (lambdaOuterFunc / activeBlockStack) for the duration of the parse.
            i6RawNode* compositeNode = nullptr;
            functionDef* synthFunc = nullptr;
            statementBlock* synthBody = nullptr;
            if(body == nullptr){
                compositeNode = new i6RawNode();
                synthFunc = new functionDef();
                synthFunc->name = "__bgl_inline_block";
                synthBody = new statementBlock();
                synthFunc->body = synthBody;
                openCompileContext(eCompileContext::codeBlock, synthBody);
            }
            string accumulatedRaw;  // accumulating raw text between/around #bgl statements (global scope)
            sourceLocation accumulatedRawSrc;  // src of the FIRST char of accumulatedRaw (preserved across appends)
            int depth = 1;
            while(depth > 0){
                eBglDirective directive = eBglDirective::NotFound;
                // Capture source position before reading the raw segment so the resulting
                // i6RawNode's `src` reflects where this chunk begins in the .bgl file. Used
                // by the emitter to anchor per-source-line entries in the source map, so I6
                // diagnostics inside the raw block remap accurately to the .bgl line.
                sourceLocation segStart = file.currentLocation();
                string segment = file.getRawTextUntilCloseOrBgl(directive, depth, depth);
                if(body != nullptr){
                    if(!segment.empty()){
                        i6RawNode* node = new i6RawNode();
                        node->src = segStart;
                        node->text = segment;
                        installI6Node(node);
                    }
                } else {
                    if(accumulatedRaw.empty()) accumulatedRawSrc = segStart;
                    accumulatedRaw += segment;
                }
                if(directive != eBglDirective::NotFound){
                    // Slice 1: all three directives (#bgl / #bglDecl / #bglStmt) route to the
                    // existing loose-statement parser inside `#i6{}` regions of `.bgl` files.
                    // TODO (later slice): decide whether `#bglDecl` is even valid here, since
                    // file-scope declarations belong outside the surrounding `#i6{}` block.
                    (void)directive;
                    statementBlock* targetBody = body != nullptr ? body : synthBody;
                    // Two forms (matching #i6):
                    //   #bgl{ stmts… }  — multi-line, terminated by matching `}`
                    //   #bgl stmt;…     — single-line, terminated by newline
                    // The single-line form must have its `{` (if any) on the same source line
                    // as `#bgl`; a newline before any non-whitespace puts us into single-line.
                    string bglContent;
                    sourceLocation hereLoc = file.currentLocation();
                    bool isMultiLine = false;
                    {
                        // Skip space/tab on the same line, looking for `{`. Don't cross newlines.
                        while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
                        if(file.peekChar() == '{'){
                            file.readChar();
                            isMultiLine = true;
                        }
                    }
                    if(isMultiLine){
                        // We consumed the `{` ourselves, so balance braceDepth for the
                        // decrement that getRawTextThroughClosingBrace will perform.
                        file.braceDepth++;
                        bglContent = file.getRawTextThroughClosingBrace();
                    } else {
                        // Single-line: read raw chars to end of line. The newline itself is
                        // part of the surrounding I6 stream, so don't consume it here.
                        char c = file.peekChar();
                        while(c != '\n' && c != (char)EOF){
                            file.readChar();
                            bglContent += c;
                            c = file.peekChar();
                        }
                    }
                    // Sub-parse via an in-memory stream so getToken() drives Beguile parsing
                    // over the bgl content with normal line tracking. Skip whitespace and
                    // check for stream EOF before calling into getToken — the lexer treats a
                    // raw EOF inside a non-global compile context as an error, but in our
                    // case end-of-bgl-content is a normal terminator.
                    abstractObject& subContext = (compositeNode != nullptr) ? *(abstractObject*)synthFunc : contextObj;
                    size_t stmtCountBefore = targetBody->statements.size();
                    // Use the original file path verbatim so editor click-to-navigate works
                    // on errors fired during the sub-parse. The virtual stream's startLine
                    // is the source line of `#bgl`, so reported line numbers map back correctly.
                    file.openText(bglContent, hereLoc.file, hereLoc.line);
                    bool savedLoose = looseIdentifierMode;
                    looseIdentifierMode = true;
                    try {
                        while(true){
                            file.bleedSpaces();
                            if(file.peekChar() == (char)EOF) break;
                            token nt = file.getToken();
                            if(nt.is(eTokenType::eof)) break;
                            // processStatementDispatch returns true when it consumes a stray
                            // `}` — shouldn't happen in well-formed bgl content, but guard anyway.
                            if(processStatementDispatch(nt, subContext)) break;
                        }
                    } catch(...) { looseIdentifierMode = savedLoose; file.close(); throw; }
                    looseIdentifierMode = savedLoose;
                    file.close();
                    // Global-scope: move newly-parsed statements into the composite node's
                    // parts list, attaching the accumulated raw text in front of the first.
                    if(compositeNode != nullptr){
                        bool firstStatement = true;
                        for(size_t i = stmtCountBefore; i < targetBody->statements.size(); i++){
                            statement* s = targetBody->statements[i];
                            string lead = firstStatement ? accumulatedRaw : "";
                            sourceLocation leadSrc = firstStatement ? accumulatedRawSrc : sourceLocation{};
                            compositeNode->parts.push_back({lead, s, leadSrc});
                            firstStatement = false;
                        }
                        if(firstStatement){
                            // No statements were parsed — nothing to anchor accumulatedRaw to;
                            // it will be picked up by the next iteration's segment handling.
                        } else {
                            accumulatedRaw.clear();
                            accumulatedRawSrc = {};
                        }
                        // Drop them from synthBody so the next sub-parse starts fresh.
                        targetBody->statements.resize(stmtCountBefore);
                    }
                }
            }
            // Global-scope: install the composite node, with any trailing raw text appended.
            if(compositeNode != nullptr){
                closeCompileContext(eCompileContext::codeBlock);
                if(!accumulatedRaw.empty()){
                    if(compositeNode->parts.empty()){
                        compositeNode->text = accumulatedRaw;
                        compositeNode->src = accumulatedRawSrc;
                    } else {
                        compositeNode->parts.push_back({accumulatedRaw, nullptr, accumulatedRawSrc});
                    }
                }
                installI6Node(compositeNode);
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
            // Collect condition text up to end of line, optional `;` no-op, EOF, or the
            // start of the next #-directive. Single-line forms like `#if cond; …; #endif`
            // and `#if cond #includeI6 … #endif` both parse correctly. When stopping on
            // a #-directive, hand it off to processDirective recursively.
            string condText;
            token t = file.getBasicToken(true);
            bool stoppedOnDirective = false;
            while(t.isNot("\n") && t.isNot(";") && t.isNot(eTokenType::eof)){
                if(!t.value.empty() && t.value[0] == '#'){ stoppedOnDirective = true; break; }
                condText += t.value;
                t = file.getBasicToken(true);
            }
            if(!evaluateCondition(condText))
                skipConditionalBlock(contextObj);
            else if(stoppedOnDirective)
                processDirective(t, contextObj);
            return false;
        }
        case chk("#elif"):{
            // reached here only when a prior #if branch was TRUE — skip to #endif
            int startLine1 = directive.src.line + 1;  // first line after the #elif directive
            int depth = 1;
            int endLine1 = startLine1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #elif block."); return false; }
                if(t.is("#if"))    depth++;
                if(t.is("#endif")) { depth--; if(depth == 0) endLine1 = t.src.line - 1; }
            }
            recordInactiveRange(startLine1, endLine1);
            return false;
        }
        case chk("#else"):{
            // reached here only when a prior #if/#elif branch was TRUE — skip to #endif
            int startLine1 = directive.src.line + 1;  // first line after the #else directive
            int depth = 1;
            int endLine1 = startLine1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #else block."); return false; }
                if(t.is("#if"))    depth++;
                if(t.is("#endif")) { depth--; if(depth == 0) endLine1 = t.src.line - 1; }
            }
            recordInactiveRange(startLine1, endLine1);
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

// ===============================================================================
// processBeguilerSettings + applySchemaDefaults
// ===============================================================================
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
            // Comma-separated list, matching I6 ICL `+include_path=a,b,c` convention.
            // Whitespace around each entry is trimmed; empty entries are skipped; existing
            // entries are not duplicated.
            size_t i = 0;
            while(i < strVal.size()){
                size_t comma = strVal.find(',', i);
                string entry = strVal.substr(i, comma == string::npos ? string::npos : comma - i);
                size_t a = entry.find_first_not_of(" \t");
                size_t b = entry.find_last_not_of(" \t");
                if(a != string::npos){
                    string path = rewritePathSeps(entry.substr(a, b - a + 1));
                    if(find(cfg.includePaths.begin(), cfg.includePaths.end(), path) == cfg.includePaths.end())
                        cfg.includePaths.push_back(path);
                }
                if(comma == string::npos) break;
                i = comma + 1;
            }
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
        else if(key == "linqscratchsize"){
            int sz = stoi(strVal);
            if(sz < 1) parsingError("beguilerSettings property 'linqScratchSize' must be at least 1");
            if(cfg.linqScratchSize == -1) cfg.linqScratchSize = sz;  // -1 = unset sentinel
        }
        else if(key == "worldbufsize"){
            int sz = stoi(strVal);
            if(sz < 1) parsingError("beguilerSettings property 'worldBufSize' must be at least 1");
            if(cfg.worldBufSize == -1) cfg.worldBufSize = sz;  // -1 = unset sentinel
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
        else if(key == "linqscratchsize" && cfg.linqScratchSize == -1) cfg.linqScratchSize = stoi(defVal);
        else if(key == "worldbufsize" && cfg.worldBufSize == -1) cfg.worldBufSize = stoi(defVal);
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
