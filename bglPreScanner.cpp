// bglPreScanner.cpp — Pre-scanner (Pass 1) for the Beguile compiler.
// Registers type/object/function stubs so forward references resolve in Pass 2.
// These are bglParser methods extracted for code organization.

#include "bglParser.h"
#include "bglLanguageService.h"
#include "settings.h"
#include <filesystem>
#include <iostream>

using namespace std;

// Defined in bglParser.cpp
filesystem::path findCaseInsensitive(const filesystem::path& dir, const string& target);
filesystem::path findLibIncludeRecursive(const filesystem::path& root, const string& includeName);
string resolveIncludePath(const string& name, const string& ext, const filesystem::path& baseDir, const vector<string>& includePaths);
string rewritePathSeps(const string& path);

// Record a 1-based inclusive [startLine1, endLine1] range as inactive in the currently-open file.
// Used by #if/#elif/#else skip handlers to tell the LSP which lines are dead branches.
void bglParser::recordInactiveRange(int startLine1, int endLine1Inclusive){
    if(endLine1Inclusive < startLine1) return;
    sourceLocation loc = file.currentLocation();
    if(loc.file.empty()) return;
    // Normalize path so the LSP can match against its canonicalized parse path.
    string path;
    try { path = filesystem::canonical(filesystem::absolute(loc.file)).string(); }
    catch(...) { path = loc.file; }
    InactiveRegion r{ startLine1 - 1, endLine1Inclusive };  // 0-based half-open
    inactiveRegions[path].push_back(r);
}

//==================================================================
// Pre-scanner (Pass 1): registers type/object/function stubs so that
// forward references resolve correctly in the full parse (Pass 2).
//==================================================================

// Body-skipping helpers use getRawTextThroughClosingBrace (char-level) to safely skip
// content containing I6 raw code like ##VerbName without triggering the token classifier.
void bglParser::preScanSkipBodyContents(bool isI6Content){
    // Opening '{' already consumed. Skip raw chars until matching '}'.
    // Caller passes isI6Content=true when skipping a raw-I6 body (#i6, emitter
    // bodies, etc.) so `!` is recognized as a line comment. Default false treats
    // `!` as a Beguile operator (`!=`, `!flag`, etc.) — correct for skipping
    // Beguile bodies during pre-scan.
    file.getRawTextThroughClosingBrace(isI6Content);
}
void bglParser::preScanSkipBody(bool isI6Content){
    // Consume opening '{' then skip to matching '}'. Same isI6Content semantics
    // as preScanSkipBodyContents.
    token t = file.getToken();
    if(!t.is(token::braceOpen)) return;
    file.getRawTextThroughClosingBrace(isI6Content);
}
// Skip tokens in a false #if branch during pre-scan until #elif/#else/#endif at depth 0.
// On #elif: evaluate condition and either continue scanning or skip again.
// On #else: continue scanning (it's the true branch).
// On #endif: done.
// Records the skipped source-line range via recordInactiveRange() for LSP inactive-region reporting.
void bglParser::preScanSkipConditionalBlock(){
    int depth = 1;
    int startLine1 = file.currentLocation().line;  // first dead line (one past the #if)
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) { recordInactiveRange(startLine1, file.currentLocation().line); return; }
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("#if")) { depth++; continue; }
        if(t.is("#endif")) {
            depth--;
            if(depth == 0) { recordInactiveRange(startLine1, t.src.line - 1); return; }
            continue;
        }
        if(depth == 1 && t.is("#elif")){
            int elifLine = t.src.line;
            // Evaluate this branch's condition
            string condText;
            token ct = file.getBasicToken(true);
            while(ct.isNot("\n") && ct.isNot(eTokenType::eof)){ condText += ct.value; ct = file.getBasicToken(true); }
            if(evaluateCondition(condText)) {
                recordInactiveRange(startLine1, elifLine - 1);
                return; // condition true — resume normal pre-scanning
            }
            // else continue skipping — will be rolled into the next recorded range when we hit
            // the next depth-0 terminator
        }
        if(depth == 1 && t.is("#else")){
            recordInactiveRange(startLine1, t.src.line - 1);
            return; // #else is the true branch — resume normal pre-scanning
        }
    }
}
void bglParser::preScanSkipToSemicolon(){
    while(true){
        token t = file.peekToken();
        // Stop at the enclosing scope's closing brace WITHOUT consuming it — the caller's
        // outer loop (e.g. an object-body walk) needs the `}` to terminate naturally.
        // Without this, members like `grammar = {…}` that lack a trailing `;` cause this
        // function to over-run past the enclosing block's `}` into the next declaration.
        if(t.is(token::braceClose) || t.is(eTokenType::eof)) return;
        file.getToken();
        if(t.is(token::endStatement)) return;
        if(t.is(token::braceOpen)) file.getRawTextThroughClosingBrace(/*isI6Content=*/false);
        // After consuming a {…} block, continue reading to find the ';'
    }
}
void bglParser::preScanSkipParens(){
    // Opening '(' already consumed. Skip to matching ')'.
    // Use token-level for params since they don't contain raw I6 content.
    int depth = 1;
    while(depth > 0){
        token t = file.getToken();
        if(t.is(token::parenOpen))  depth++;
        else if(t.is(token::parenClose)) depth--;
        else if(t.is(eTokenType::eof)) return;
    }
}

void bglParser::preScanCaptureParams(vector<paramDef*>& out){
    // Opening '(' already consumed. Capture each `type name [= default]` up to matching ')'.
    // Defaults, nested parens (in default exprs), and generic suffixes on param types are all tolerated.
    while(true){
        token t = file.getToken();
        if(t.is(token::parenClose) || t.is(eTokenType::eof)) return;
        if(t.is(token::comma)) continue;
        if(!t.isDataType() && !t.is(eTokenType::identifier)){
            // Unrecognized token in param position — bail out safely by skipping to ')'
            int depth = 1;
            while(depth > 0){
                if(t.is(token::parenOpen)) depth++;
                else if(t.is(token::parenClose)){ depth--; if(depth == 0) return; }
                else if(t.is(eTokenType::eof)) return;
                t = file.getToken();
            }
            return;
        }
        string typeName = t.value;
        preScanConsumeGenericSuffix(t);
        token nameTok = file.getToken();
        if(nameTok.is(token::parenClose)) return;
        if(!nameTok.is(eTokenType::identifier)) continue;
        paramDef* p = new paramDef();
        p->name = nameTok.value;
        p->type.name = typeName;
        out.push_back(p);
        token s = file.getToken();
        if(s.is(token::parenClose)) return;
        if(s.is(token::comma)) continue;
        if(s.is(token::assignment)){
            // Skip default value until top-level ',' or ')'
            int depth = 0;
            while(true){
                token d = file.getToken();
                if(d.is(eTokenType::eof)) return;
                if(d.is(token::parenOpen)) depth++;
                else if(d.is(token::parenClose)){
                    if(depth == 0) return;
                    depth--;
                } else if(d.is(token::comma) && depth == 0) break;
            }
        }
    }
}

void bglParser::preScanConsumeGenericSuffix(const token& typeTok){
    if(!file.peekToken().is("<")) return;
    if(typeTok.value == "array"){
        file.getToken(); // '<'
        file.getToken(); // element type
        file.getToken(); // '>'
        return;
    }
    if(typeTok.value == "func"){
        file.getToken(); // '<'
        int depth = 1;
        while(depth > 0){
            token t = file.getToken();
            if(t.value == "<") depth++;
            else if(t.value == ">") depth--;
            else if(t.is(eTokenType::eof)) return;
        }
    }
}

void bglParser::preScanDirective(token tok){
    if(tok.is("#once")){
        auto [stream, name, line, col] = file.getCurrentFileDetail();
        preScanOnceFiles.insert(filesystem::canonical(filesystem::absolute(name)).string());
    } else if(tok.is("#include")){
        token next = file.getToken();
        // Skip optional '?' marker
        if(next.is("?")) next = file.getToken();
        if(next.isString()){
            string includeName = next.value;
            if(includeName.size() >= 2 && includeName.front()=='"' && includeName.back()=='"')
                includeName = includeName.substr(1, includeName.size()-2);
            auto [stream, curFile, line, col] = file.getCurrentFileDetail();
            filesystem::path curDir = filesystem::path(curFile).parent_path();
            string resolved = resolveIncludePath(includeName, ".bgl", curDir, beguilerSettings.includePaths);
            if(!resolved.empty()) preScanFile(resolved);
        } else if(next.is("<")){
            string includeName2;
            token t2 = file.getToken();
            while(!t2.is(">") && !t2.is(eTokenType::eof)){
                includeName2 += t2.originalValue.empty() ? t2.value : t2.originalValue;
                t2 = file.getToken();
            }
            filesystem::path libPath = findLibIncludeRecursive(settings.libPath, includeName2);
            if(!libPath.empty()) preScanFile(libPath.string());
        }
    } else if(tok.is("#define")){
        // Store symbol in definedSymbols so #if works during pre-scan
        token sym = file.getToken(eTokenType::identifier);
        while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
        token val = file.getBasicToken(true);
        string valStr;
        if(val.isNot("\n") && val.isNot(eTokenType::eof)){
            valStr = val.value;
            token rest = file.getBasicToken(true);
            while(rest.isNot("\n") && rest.isNot(eTokenType::eof)) rest = file.getBasicToken(true);
        }
        definedSymbols[sym.value] = valStr;
    } else if(tok.is("#undef")){
        token sym = file.getToken(eTokenType::identifier);
        definedSymbols.erase(sym.value);
    } else if(tok.value == "#startup" || tok.value == "#emitfirst" || tok.value == "#emitlast"){
        preScanSkipBody();
    } else if(tok.value == "#storedemitfirst" || tok.value == "#storedemitlast"){
        // Named-block forms — same body shape as #emitfirst/#emitlast, with a name token
        // before the `{`. Consume the name, then skip the body the same way.
        file.getToken(eTokenType::identifier);   // block name
        preScanSkipBody();
    } else if(tok.value == "#beguilersettings"){
        preScanSkipBody(); // skip { target = Glulx; ... } so the closing } doesn't corrupt the token stream
    } else if(tok.value == "#includei6"){
        // Skip optional '?' marker
        if(file.peekToken().is("?")) file.getToken();
        token filename = file.getToken(); // consume the quoted filename
        string innerPath = filename.value;
        if(innerPath.size() >= 2 && innerPath.front()=='"' && innerPath.back()=='"')
            innerPath = innerPath.substr(1, innerPath.size()-2);
        string emitPath;
        if(filename.is(eTokenType::rawQuote)){
            // Raw form `#includeI6 @"..."` — pass through verbatim, no resolution.
            emitPath = "\"" + innerPath + "\"";
        } else {
            // Resolve path for the stub so it matches the full-pass resolution
            filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
            string resolved = resolveIncludePath(innerPath, "", curDir, beguilerSettings.includePaths);
            if(resolved.empty()) resolved = resolveIncludePath(innerPath, ".h", curDir, beguilerSettings.includePaths);
            emitPath = resolved.empty()
                ? "\"" + rewritePathSeps(innerPath) + "\""
                : "\"" + rewritePathSeps(resolved) + "\"";
        }
        i6RawNode& stub = *(new i6RawNode());
        stub.text = format("#include {0};", emitPath);
        stub.isPrePassStub = true;
        languageService.globals.push_back(&stub);
    } else if(tok.value == "#i6"){
        // Reserve position in globals so main-pass content appears in source order
        i6RawNode& stub = *(new i6RawNode());
        stub.text = "#i6_placeholder";
        stub.isPrePassStub = true;
        languageService.globals.push_back(&stub);
        // Skip content: multi-line (braced) or single-line (to newline).
        // #i6{} content is raw I6 — `!` is a line-comment marker, not an operator.
        if(file.peekToken().is(token::braceOpen))
            preScanSkipBody(/*isI6Content=*/true);
        else {
            token t = file.getBasicToken(true);
            while(t.isNot("\n") && t.isNot(eTokenType::eof)) t = file.getBasicToken(true);
        }
    } else if(tok.is("#using")){
        // Consume the full dotted path: name(.name)*
        // Resolution is deferred to the main parse (this is pre-scan).
        file.getToken();
        while(file.peekToken().is(token::period)){
            file.getToken(); // consume '.'
            file.getToken(); // consume member name
        }
    } else if(tok.is("#if")){
        // Evaluate condition and skip false branch. The condition ends at the first
        // newline, optional `;` no-op, EOF, or the start of the next #-directive — so
        // single-line forms like `#if cond; #includeI6 …; #endif` and `#if cond #includeI6 …
        // #endif` both parse correctly. When stopping on a #-directive, hand it off to
        // preScanDirective recursively so it isn't lost.
        string condText;
        token t = file.getBasicToken(true);
        bool stoppedOnDirective = false;
        while(t.isNot("\n") && t.isNot(";") && t.isNot(eTokenType::eof)){
            if(!t.value.empty() && t.value[0] == '#'){ stoppedOnDirective = true; break; }
            condText += t.value;
            t = file.getBasicToken(true);
        }
        if(!evaluateCondition(condText))
            preScanSkipConditionalBlock();
        else if(stoppedOnDirective)
            preScanDirective(t);
    } else if(tok.is("#elif")){
        // Reached here because prior #if branch was TRUE — skip to #endif
        int startLine1 = tok.src.line + 1;  // first line after the #elif directive
        int depth = 1;
        int endLine1 = startLine1;
        while(depth > 0){
            token t = file.getToken();
            if(t.is(eTokenType::eof)) break;
            if(t.is("#if")) depth++;
            if(t.is("#endif")) { depth--; if(depth == 0) endLine1 = t.src.line - 1; }
        }
        recordInactiveRange(startLine1, endLine1);
    } else if(tok.is("#else")){
        // Reached here because prior #if was TRUE — skip to #endif
        int startLine1 = tok.src.line + 1;  // first line after the #else directive
        int depth = 1;
        int endLine1 = startLine1;
        while(depth > 0){
            token t = file.getToken();
            if(t.is(eTokenType::eof)) break;
            if(t.is("#if")) depth++;
            if(t.is("#endif")) { depth--; if(depth == 0) endLine1 = t.src.line - 1; }
        }
        recordInactiveRange(startLine1, endLine1);
    } else if(tok.is("#endif")){
        // no-op — consumed naturally
    }
    // Other directives (#message, #error, #warning, #exit, ##ifdef, etc.) are ignored during pre-scan
}

void bglParser::preScanFile(string filename, const std::string* contentOverride){
    string absPath;
    try { absPath = filesystem::canonical(filesystem::absolute(filename)).string(); }
    catch(...) { absPath = filename; }  // contentOverride callers may pass paths that don't exist on disk
    if(preScanOnceFiles.count(absPath)) return;

    bool isFirst = (preScanDepth == 0);

    // .inf-as-input mode: pre-scan the Beguile Language Runtime (so its declarations are
    // available inside #bgl islands), then walk the .inf body for #bglDecl{} / #bgl{} blocks
    // whose content is declaration-mode and pre-scan them too. This makes types declared in
    // an island visible to all #bgl content in the file (forward refs across the .inf body).
    if(isFirst && filesystem::path(filename).extension() == ".inf"){
        preScanDepth++;
        filesystem::path sysPath = filesystem::path(settings.libPath) / "core" / "__beguileCore.bgl";
        preScanFile(sysPath.string());
        // Open the .inf file (or use the in-memory override for the LSP path).
        // Missing-file exception is silent (matches the .bgl path); any other exception
        // (parsing errors raised while pre-scanning Beguile islands) must propagate,
        // but the file still has to be closed first.
        try {
            if(contentOverride) file.openText(*contentOverride, absPath, 1);
            else                file.open(absPath);
        }
        catch(runtime_error&){ preScanDepth--; return; }
        try { preScanInfFileBodyForDecls(); }
        catch(...){ file.close(); preScanDepth--; throw; }
        file.close();
        preScanDepth--;
        return;
    }

    try {
        if(contentOverride) file.openText(*contentOverride, absPath, 1);
        else                file.open(absPath);
    }
    catch(runtime_error&){ return; } // silently skip missing files

    preScanDepth++;

    if(isFirst){
        // Mirror main parse: always load __beguileCore.bgl first
        filesystem::path sysPath = filesystem::path(settings.libPath) / "core" / "__beguileCore.bgl";
        preScanFile(sysPath.string());
        file.moveToStart();
    }

    preScanGlobalLoop();

    file.close();
    preScanDepth--;
}

// Walks tokens in the currently-open file, registering type/global stubs until EOF.
// Extracted from preScanFile so .inf-mode declaration islands (opened as virtual files)
// can run the same registration logic.
void bglParser::preScanGlobalLoop(){
    while(true){
        token tok = file.getToken();
        if(tok.is(eTokenType::eof)) break;

        // Stray top-level `;` — silently skip. Authors sometimes write trailing `;` after a
        // brace block (e.g. `extern verb V { ... };`), which would otherwise fall through to
        // preScanSkipToSemicolon at the loop tail and swallow the next statement looking for
        // the next `;`.
        if(tok.is(token::endStatement)) continue;

        if(tok.is(eTokenType::directive)){ preScanDirective(tok); continue; }

        Qualifiers q = parseQualifiers(tok);
        bool isExtern  = q.isExtern;
        bool isEmitter = q.isEmitter;

        // extend class/object — register new members on the existing type during pre-scan
        // Note: 'extend' was consumed by parseQualifiers; tok is now 'class', 'verb', identifier, or 'extern'
        if(q.isExtend){
            // extend object by name — register added members on the existing objectDef.
            // Without this pre-pass, sibling-method calls inside the extended body fail
            // to resolve in the full pass when the caller is declared above the callee.
            if((tok.is(eTokenType::identifier) || tok.isDataType()) && !tok.is(token::classDeclaration)){
                objectDef* obj = nullptr;
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == tok.value){ obj = od; break; }
                if(obj == nullptr){
                    // Unknown object — full-pass will report it; skip body here.
                    preScanSkipBody();
                    continue;
                }
                // Open the body
                token t = file.getToken();
                while(!t.is(token::braceOpen) && !t.is(eTokenType::eof)) t = file.getToken();
                t = file.getToken();
                while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                    bool memberIsEmitter = false;
                    bool memberIsReplace = false;
                    if(t.is("replace")){ memberIsReplace = true; t = file.getToken(); }
                    if(t.is("explicit")) t = file.getToken();
                    if(t.is("emitter")){ memberIsEmitter = true; t = file.getToken(); }
                    if(t.isDataType() || t.is(eTokenType::identifier)){
                        preScanConsumeGenericSuffix(t);
                        token memberName = file.getToken();
                        if(memberName.is("operator")){
                            token opTok = file.getToken();
                            if(opTok.is(token::parenOpen)){ memberName.value = "operator()"; }
                            else if(opTok.is(token::bracketOpen)){ file.getToken(); memberName.value = "[]"; token ma = file.getToken(); if(ma.is("=")) memberName.value = "[]="; }
                            else if(opTok.is("?")) memberName.value = "?";
                            else if(opTok.is("switch")) memberName.value = "switch";
                            else memberName.value = opTok.value;
                        }
                        token afterName = file.getToken();
                        if(afterName.is(token::parenOpen)){
                            functionDef& fd = *(new functionDef());
                            fd.name = memberName.value;
                            fd.returnType = languageService.getType(t.value);
                            fd.isEmitter = memberIsEmitter;
                            fd.isPrePassStub = true;
                            preScanCaptureParams(fd.params);
                            token bodyStart = file.getToken();
                            if(bodyStart.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
                            bool exists = false;
                            for(auto it = obj->members.begin(); it != obj->members.end(); ++it){
                                if((*it)->name == fd.name){
                                    exists = true;
                                    if(memberIsReplace) *it = &fd;
                                    break;
                                }
                            }
                            if(!exists) obj->members.push_back(&fd);
                        } else {
                            // Property — skip to ;
                            while(!afterName.is(token::endStatement) && !afterName.is(token::braceClose) && !afterName.is(eTokenType::eof)){
                                if(afterName.is(token::braceOpen)){ file.getRawTextThroughClosingBrace(); break; }
                                afterName = file.getToken();
                            }
                            if(afterName.is(token::braceClose)) break;
                        }
                    } else {
                        while(!t.is(token::endStatement) && !t.is(token::braceClose) && !t.is(eTokenType::eof)) t = file.getToken();
                        if(t.is(token::braceClose)) break;
                    }
                    t = file.getToken();
                }
                continue;
            }
            if(tok.is("extern")) tok = file.getToken(); // consume "extern", now tok = "class"
            if(tok.is(token::classDeclaration)) tok = file.getToken(); // consume "class", now tok = name
            token nameTok = tok;
            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(nameTok.value));
            if(cls != nullptr){
                // Skip inheritance clause if present
                token t = file.getToken();
                while(!t.is(token::braceOpen) && !t.is(eTokenType::eof)) t = file.getToken();
                // Scan body for member stubs
                t = file.getToken();
                while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                    bool memberIsEmitter = false;
                    bool memberIsReplace = false;
                    if(t.is("replace")){ memberIsReplace = true; t = file.getToken(); }
                    if(t.is("explicit")) t = file.getToken(); // skip explicit qualifier
                    if(t.is("emitter")){ memberIsEmitter = true; t = file.getToken(); }
                    // Accept either a built-in dataType OR an identifier as the return type.
                    // The identifier path covers type parameters (e.g. `T` from `class array<T>`)
                    // and user-defined class names, which the lexer doesn't classify as dataTypes
                    // at pre-scan time. Without this, hitting `T pop()` triggered the "unrecognized"
                    // fallback below, which skipped to the emitter body's `}` and then `break`ed
                    // out of the entire extend body — leaving subsequent methods unscanned.
                    if(t.isDataType() || t.is(eTokenType::identifier)){
                        preScanConsumeGenericSuffix(t);
                        token memberName = file.getToken();
                        if(memberName.is("operator")){
                            // operator declarations — just skip to body
                            token opTok = file.getToken();
                            if(opTok.is(token::parenOpen)){ memberName.value = "operator()"; }
                            else if(opTok.is(token::bracketOpen)){ file.getToken(); memberName.value = "[]"; token ma = file.getToken(); if(ma.is("=")) memberName.value = "[]="; }
                            else if(opTok.is("?")) memberName.value = "?";
                            else if(opTok.is("switch")) memberName.value = "switch";
                            else memberName.value = opTok.value;
                        }
                        token afterName = file.getToken();
                        if(afterName.is(token::parenOpen)){
                            // Method: register stub on the class
                            functionDef& fd = *(new functionDef());
                            fd.name = memberName.value;
                            fd.returnType = languageService.getType(t.value);
                            fd.isEmitter = memberIsEmitter;
                            fd.isPrePassStub = true;
                            preScanCaptureParams(fd.params);
                            token bodyStart = file.getToken();
                            if(bodyStart.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
                            // Add if not already present. For `replace`, overwrite the existing
                            // stub in place so the class ends up with one entry per method name.
                            bool exists = false;
                            for(auto it = cls->members.begin(); it != cls->members.end(); ++it){
                                if((*it)->name == fd.name){
                                    exists = true;
                                    if(memberIsReplace) *it = &fd;
                                    break;
                                }
                            }
                            if(!exists) cls->members.push_back(&fd);
                        } else {
                            // Property — skip to ;
                            while(!afterName.is(token::endStatement) && !afterName.is(token::braceClose) && !afterName.is(eTokenType::eof)){
                                if(afterName.is(token::braceOpen)){ file.getRawTextThroughClosingBrace(); break; }
                                afterName = file.getToken();
                            }
                            if(afterName.is(token::braceClose)) break;
                        }
                    } else {
                        // Unrecognized — skip to ;
                        while(!t.is(token::endStatement) && !t.is(token::braceClose) && !t.is(eTokenType::eof)) t = file.getToken();
                        if(t.is(token::braceClose)) break;
                    }
                    t = file.getToken();
                }
            } else {
                preScanSkipBody();
            }
            continue;
        }

        // global emitter object: 'emitter Foo { }' — identifier immediately followed by '{'
        // Distinct from emitter class (requires 'class' keyword) and emitter functions (require a return type).
        if(isEmitter && tok.is(eTokenType::identifier)){
            token peek = file.peekToken();
            if(peek.is(token::braceOpen)){
                string nameStr = tok.value;
                if(!languageService.isObjectType(nameStr)){
                    classDef& stub = languageService.registerClass(nameStr, false);
                    stub.isPrePassStub = true;
                    stub.isEmitterClass = true;
                    stub.isGlobalEmitterObject = true;
                }
                preScanSkipBody();
                continue;
            }
        }

        // `alias` is consumed by parseQualifiers above (sets q.isAlias) — also accept a
        // bare `alias` here in case some path reaches here without going through it.
        bool isAliasClass = q.isAlias;
        if(tok.is("alias")) { isAliasClass = true; tok = file.getToken(); } // consume 'class'

        // class declaration
        if(tok.is(token::classDeclaration)){
            token nameTok = file.getToken();
            string nameStr = nameTok.value;
            classDef* cls = nullptr;
            if(!languageService.isObjectType(nameStr)){
                classDef& stub = languageService.registerClass(nameStr, isExtern);
                stub.isPrePassStub = true;
                if(isEmitter)     stub.isEmitterClass = true;
                if(isAliasClass)  stub.isAlias = true;
                cls = &stub;
            } else {
                cls = dynamic_cast<classDef*>(&languageService.getType(nameStr));
            }
            // Type parameter clause: `class Foo<T> {…}` — store names on the classDef
            // stub. Not registered as global types (would collide with same-named
            // instances). Member signatures parse T as identifier-typed; substitution
            // at method-lookup time replaces T with the use-site binding.
            if(file.peekToken().is("<")){
                file.getToken(); // consume '<'
                while(true){
                    token paramTok = file.getToken();
                    if(paramTok.is(eTokenType::identifier) || paramTok.isDataType()){
                        string paramName = paramTok.value;
                        if(cls != nullptr) cls->typeParameters.push_back(paramName);
                    }
                    token sep = file.getToken();
                    if(sep.value == ">") break;
                    if(!sep.is(token::comma)) break; // malformed — let main pass produce the diagnostic
                }
            }
            // Skip past any inheritance clause to find '{'
            { token t = file.getToken();
              while(!t.is(token::braceOpen) && !t.is(token::endStatement) && !t.is(eTokenType::eof))
                  t = file.getToken();
              if(t.is(token::braceOpen)) {
                  // Walk the class body at token level looking for top-level `static TYPE NAME`
                  // declarations, registering them as member stubs. Any nested `{ ... }` block
                  // (method bodies, emitter bodies, initializer blocks) is consumed as raw text
                  // via getRawTextThroughClosingBrace() because it may contain ##, $self, etc.
                  // that would confuse the tokenizer.
                  token bt = file.getToken();
                  while(!bt.is(token::braceClose) && !bt.is(eTokenType::eof)){
                      if(bt.is(token::braceOpen)){
                          file.getRawTextThroughClosingBrace();
                          bt = file.getToken();
                          continue;
                      }
                      if(bt.is("static") && cls != nullptr){
                          // static TYPE NAME [= expr] ;
                          token typeTok = file.getToken();
                          token memberName = file.getToken();
                          if(typeTok.isDataType() && memberName.is(eTokenType::identifier)){
                              bool exists = false;
                              for(typeMember* m : cls->members)
                                  if(m->name == memberName.value){ exists = true; break; }
                              if(!exists){
                                  variableDeclaration& vd = *(new variableDeclaration());
                                  vd.name = memberName.value;
                                  vd.type.name = typeTok.value;
                                  vd.isStatic = true;
                                  vd.isPrePassStub = true;
                                  cls->members.push_back(&vd);
                              }
                          }
                          // Skip to ';' — but if we hit a '{' (an init block), consume it raw
                          token s = file.getToken();
                          while(!s.is(token::endStatement) && !s.is(token::braceClose) && !s.is(eTokenType::eof)){
                              if(s.is(token::braceOpen)) { file.getRawTextThroughClosingBrace(); break; }
                              s = file.getToken();
                          }
                          if(s.is(token::braceClose)) break;
                          bt = file.getToken();
                          continue;
                      }
                      // Method or property stub: [emitter] [replace] [default] TYPE NAME (...) | TYPE NAME [= ...] ;
                      // Register a stub (functionDef for methods, variableDeclaration for properties)
                      // so source-order is preserved when main parse fills them in. Without property
                      // stubs, pre-scan adds method stubs first and main parse appends properties
                      // last, scrambling the emission order relative to source.
                      if(cls != nullptr && (bt.isDataType() || bt.is(eTokenType::identifier))){
                          // Skip optional method-modifier keywords
                          bool sawEmitter = false;
                          while(bt.is("emitter") || bt.is("replace") || bt.is("default") || bt.is("explicit")){
                              if(bt.is("emitter")) sawEmitter = true;
                              bt = file.getToken();
                          }
                          if(bt.isDataType() || bt.is(eTokenType::identifier)){
                              token typeTok = bt;
                              token afterType = file.getToken();
                              // Operator declarations — register a stub so forward references resolve.
                              // The full-pass name conventions are mirrored here (see bglParser.cpp:2244+):
                              //   operator(<>)        → "operator()"
                              //   operator[]          → "[]"      (read)
                              //   operator[]=         → "[]="     (write)
                              //   operator switch     → "switch"
                              //   operator auto       → "auto"
                              //   operator <symbol>   → "<symbol>" (e.g. "<", "==", "+")
                              //   operator id<sym>    → "id<sym>" (e.g. "prefix++")
                              //   operator ?          → "?"
                              if(afterType.value == "operator"){
                                  token opTok = file.getToken();
                                  string opName;
                                  if(opTok.is(token::parenOpen)){
                                      opName = "operator()";
                                      // ( already consumed — drain params
                                      int depth = 1;
                                      while(depth > 0){ token p = file.getToken(); if(p.is(eTokenType::eof)) break; if(p.is(token::parenOpen)) depth++; else if(p.is(token::parenClose)) depth--; }
                                  } else if(opTok.is(token::bracketOpen)){
                                      file.getToken();                 // ]
                                      token maybeAssign = file.getToken();
                                      if(maybeAssign.is(token::assignment)) opName = "[]=";
                                      else                                  opName = "[]";
                                  } else if(opTok.is("switch") || opTok.is("auto")){
                                      opName = opTok.value;
                                  } else if(opTok.is(eTokenType::identifier)){
                                      token opSym = file.getToken();
                                      opName = opTok.value + opSym.value;
                                  } else if(opTok.is("?")){
                                      opName = "?";
                                  } else {
                                      opName = opTok.value;
                                  }
                                  // Register the stub
                                  bool exists = false;
                                  for(typeMember* m : cls->members)
                                      if(m->name == opName){ exists = true; break; }
                                  if(!exists){
                                      functionDef& fd = *(new functionDef());
                                      fd.name = opName;
                                      fd.returnType.name = typeTok.value;
                                      fd.isEmitter = sawEmitter;
                                      fd.isPrePassStub = true;
                                      cls->members.push_back(&fd);
                                  }
                                  // Drain to end of declaration (body or ;)
                                  token s = file.getToken();
                                  while(!s.is(token::braceOpen) && !s.is(token::endStatement) && !s.is(token::braceClose) && !s.is(eTokenType::eof)){
                                      s = file.getToken();
                                  }
                                  if(s.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
                                  if(s.is(token::braceClose)) break;
                                  bt = file.getToken();
                                  continue;
                              }
                              if(afterType.is(eTokenType::identifier) && file.peekToken().is(token::parenOpen)){
                                  // Method declaration — register functionDef stub
                                  string mname = afterType.value;
                                  bool exists = false;
                                  for(typeMember* m : cls->members)
                                      if(m->name == mname){ exists = true; break; }
                                  if(!exists){
                                      functionDef& fd = *(new functionDef());
                                      fd.name = mname;
                                      fd.returnType.name = typeTok.value;
                                      fd.isEmitter = sawEmitter;
                                      fd.isPrePassStub = true;
                                      cls->members.push_back(&fd);
                                  }
                                  // Consume parens and body
                                  file.getToken(); // '('
                                  int depth = 1;
                                  while(depth > 0){
                                      token p = file.getToken();
                                      if(p.is(eTokenType::eof)) break;
                                      if(p.is(token::parenOpen)) depth++;
                                      else if(p.is(token::parenClose)) depth--;
                                  }
                                  token bodyOrSemi = file.getToken();
                                  if(bodyOrSemi.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
                                  bt = file.getToken();
                                  continue;
                              }
                              if(afterType.is(eTokenType::identifier)){
                                  // Property declaration — register variableDeclaration stub
                                  string pname = afterType.value;
                                  bool exists = false;
                                  for(typeMember* m : cls->members)
                                      if(m->name == pname){ exists = true; break; }
                                  if(!exists){
                                      variableDeclaration& vd = *(new variableDeclaration());
                                      vd.name = pname;
                                      vd.type.name = typeTok.value;
                                      vd.isPrePassStub = true;
                                      cls->members.push_back(&vd);
                                  }
                                  // Skip to ';' — handle initializer, brace block, or bare decl
                                  token s = file.getToken();
                                  while(!s.is(token::endStatement) && !s.is(token::braceClose) && !s.is(eTokenType::eof)){
                                      if(s.is(token::braceOpen)) { file.getRawTextThroughClosingBrace(); break; }
                                      s = file.getToken();
                                  }
                                  if(s.is(token::braceClose)) break;
                                  bt = file.getToken();
                                  continue;
                              }
                              // Not a method or property — fall through
                              bt = afterType;
                              continue;
                          }
                      }
                      bt = file.getToken();
                  }
              }
            }
            continue;
        }

        // enum / bnum declaration
        if(tok.is(token::enumDeclaration) || tok.is(token::bnumDeclaration)){
            bool isBnum = tok.is(token::bnumDeclaration);
            token nameTok = file.getToken();
            string nameStr = nameTok.value;
            if(!languageService.isObjectType(nameStr)){
                enumDef& newEnum = languageService.registerEnum(nameStr, isExtern);
                newEnum.isPrePassStub = true;
                newEnum.isBnum = isBnum;
                // Optional shared-base clause: `bnum Name : Base { ... }` — only valid for bnums.
                // Base grouping allows sibling bnums to combine via `|` (see spec). The base
                // also relaxes the power-of-2 constraint (children occupy packed sub-fields).
                if(file.peekToken().is(":")){
                    file.getToken(); // consume ':'
                    token baseTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
                    if(!isBnum)
                        parsingError(format("enum '{0}': shared-base inheritance is only valid for bnum declarations", nameStr));
                    enumDef* base = dynamic_cast<enumDef*>(&languageService.getType(baseTok.value));
                    if(!base || !base->isBnum)
                        parsingError(format("bnum '{0}': base '{1}' is not a declared bnum", nameStr, baseTok.originalValue));
                    newEnum.baseBnum = base;
                }
                bool hasBase = newEnum.baseBnum != nullptr;
                file.getToken(); // consume '{'
                token t = file.getToken();
                int val = 1;
                while(t.isNot(token::braceClose)){
                    enumValueDef& ev = *(new enumValueDef());
                    ev.name = t.value;
                    ev.displayName = t.originalValue;
                    ev.docComment = t.docComment;
                    t = file.getToken({token::braceClose, token::comma, token::assignment});
                    if(t.is(token::assignment)){
                        bool negate = false;
                        if(file.peekToken().is("-")){ file.getToken(); negate = true; }
                        token numTok = file.getToken(eTokenType::integer);
                        val = stoi(numTok.value);
                        if(negate) val = -val;
                        if(isBnum && negate)
                            parsingError(format("bnum '{0}': negative value {1} is not allowed", nameStr, val));
                        // Power-of-2 check is relaxed for bnums with a shared base — children
                        // of a packed composite may occupy sub-fields with non-power-of-2 patterns.
                        if(isBnum && !hasBase && val != 0 && (val & (val - 1)) != 0)
                            parsingError(format("bnum '{0}': explicit value {1} is not a power of 2", nameStr, val));
                        t = file.getToken({token::braceClose, token::comma});
                    }
                    ev.value = val;
                    if(isBnum) val <<= 1; else val++;
                    newEnum.namedValues.push_back(&ev);
                    if(t.is(token::comma)) t = file.getToken();
                }
            } else {
                preScanSkipBody(); // already registered — just skip the body
            }
            continue;
        }

        // object keyword: 'object Name { }' is an objectDef; 'object Name;' / 'extern object Name;' is a variable stub
        if(!isEmitter && (tok.is("object") || (tok.isDataType() && file.peekToken(1).is(eTokenType::identifier)
             && (file.peekToken(2).is(token::braceOpen) || file.peekToken(2).is(":"))))){
            // Could be: object Name { }, ClassName Name { }, or ClassName Name : Parent { }
            string classType = tok.value;
            token nameTok = file.getToken();
            string nameStr = nameTok.value;
            transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
            // Check if class is verb-derived — create verbObjectDef instead of objectDef
            bool isVerbType = false;
            {   function<bool(classDef*)> checkVerb = [&](classDef* c) -> bool {
                    if(!c) return false;
                    if(c->name == "verb") return true;
                    for(classDef* b : c->baseClasses) if(checkVerb(b)) return true;
                    return false;
                };
                if(auto* cls = dynamic_cast<classDef*>(&languageService.getType(classType)))
                    isVerbType = checkVerb(cls);
            }
            token peek = file.peekToken();
            if(peek.is(token::braceOpen) || peek.is(":")){ // object body
                objectDef* objStub = nullptr;
                if(!languageService.isObjectType(nameStr)){
                    if(isVerbType){
                        verbObjectDef& vs = languageService.registerVerbObject(nameTok.value, isExtern);
                        vs.isPrePassStub = true;
                        objStub = &vs;
                    } else {
                        objStub = &languageService.registerObject(nameStr, isExtern);
                        objStub->isPrePassStub = true;
                    }
                    // Set objectClass from the declared type so forward references resolve correctly
                    if(classType != "object")
                        if(auto* cls = dynamic_cast<classDef*>(&languageService.getType(classType)))
                            objStub->objectClass = cls;
                } else {
                    // Already registered — find it
                    for(typeDef* g : languageService.globals)
                        if(auto* od = dynamic_cast<objectDef*>(g))
                            if(od->name == nameStr){ objStub = od; break; }
                }
                // Skip inheritance clause to reach '{'
                { token t = file.getToken();
                  while(!t.is(token::braceOpen) && !t.is(eTokenType::eof)) t = file.getToken(); }
                // Scan body for member stubs: look for type name ( patterns → register as method stubs
                if(objStub != nullptr){
                    token t = file.getToken();
                    while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                        bool memberIsEmitter = false;
                        if(t.is("explicit")) t = file.getToken(); // skip explicit qualifier
                        if(t.is("emitter")){ memberIsEmitter = true; t = file.getToken(); }
                        if(t.isDataType()){
                            preScanConsumeGenericSuffix(t);
                            token memberName = file.getToken();
                            token afterName = file.getToken();
                            if(afterName.is(token::parenOpen)){
                                // Method: register a stub
                                functionDef& fd = *(new functionDef());
                                fd.name = memberName.value;
                                fd.returnType = languageService.getType(t.value);
                                fd.isEmitter = memberIsEmitter;
                                fd.isPrePassStub = true;
                                preScanCaptureParams(fd.params);
                                token bodyStart = file.getToken();
                                if(bodyStart.is(token::braceOpen))
                                    file.getRawTextThroughClosingBrace();
                                // Only add if not already registered
                                bool exists = false;
                                for(typeMember* m : objStub->members)
                                    if(m->name == fd.name){ exists = true; break; }
                                if(!exists) objStub->members.push_back(&fd);
                            } else {
                                // Property — register a stub so forward references from sibling
                                // methods resolve correctly. Without this, a method declared above
                                // a property would not find the property in currentObject->members
                                // during full-pass body parsing, fall through to file-scope, and
                                // resolve against an unrelated enum value or global with the same
                                // name (compiler can't disambiguate; the user's intent of self.X
                                // is silently swapped for the file-scope hit).
                                // Skip emitters (e.g. `emitter int wordsize {WORDSIZE}`) — those
                                // look like a property shape without parens but are value-emitters
                                // that the full pass installs differently.
                                if(!memberIsEmitter){
                                    variableDeclaration& vd = *(new variableDeclaration());
                                    vd.name = memberName.value;
                                    vd.displayName = memberName.originalValue;
                                    vd.type = languageService.getType(t.value);
                                    vd.isPrePassStub = true;
                                    bool exists = false;
                                    for(typeMember* m : objStub->members)
                                        if(m->name == vd.name){ exists = true; break; }
                                    if(!exists) objStub->members.push_back(&vd);
                                }
                                // Skip the rest of the declaration up to AND INCLUDING the
                                // terminating ';' (or until the object's closing '}'). If we
                                // left a stray ';' as the next outer token, the outer loop's
                                // else-branch would feed it to preScanSkipToSemicolon, which
                                // then scans into the following statement looking for the
                                // *next* ';' — silently swallowing the next method body and
                                // its trailing property.
                                while(!afterName.is(token::endStatement) && !afterName.is(token::braceClose) && !afterName.is(eTokenType::eof)){
                                    if(afterName.is(token::braceOpen)){
                                        file.getRawTextThroughClosingBrace();
                                        // After consuming the {...}, continue reading until ';'
                                        // (or '}') so the trailing ';' doesn't leak to the outer loop.
                                        afterName = file.getToken();
                                        continue;
                                    }
                                    afterName = file.getToken();
                                }
                                if(afterName.is(token::braceClose)) break;
                            }
                        } else if(t.is("#i6")){
                            // Raw I6 block inside object — skip
                            token b = file.getToken();
                            if(b.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
                        } else {
                            // Inherited member or unknown — skip to ;
                            preScanSkipToSemicolon();
                        }
                        t = file.getToken();
                    }
                } else {
                    file.getRawTextThroughClosingBrace();
                }
            } else {
                // extern ClassName Name; or ClassName Name = ...; — variable or object stub
                if(isVerbType){
                    // extern verb-derived: register as verb object
                    bool alreadyReg = false;
                    for(verbObjectDef* v : languageService.verbs)
                        if(v->name == nameStr){ alreadyReg = true; break; }
                    if(!alreadyReg){
                        verbObjectDef& vs = languageService.registerVerbObject(nameTok.value, isExtern);
                        vs.isPrePassStub = true;
                    }
                } else {
                    bool alreadyReg = false;
                    for(typeDef* g : languageService.globals)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                            if(vd->name == nameStr){ alreadyReg = true; break; }
                    if(!alreadyReg){
                        variableDeclaration& stub = *(new variableDeclaration());
                        stub.name = nameStr;
                        stub.type.name = "object";
                        stub.isPrePassStub = true;
                        stub.isExternal = isExtern;
                        languageService.globals.push_back(&stub);
                    }
                }
                preScanSkipToSemicolon();
            }
            continue;
        }

        // grammar, attribute, beguilerSettings — skip
        if(tok.is("grammar") || tok.value == "beguilerSettings"){
            file.getToken(); // name
            preScanSkipBody();
            continue;
        }
        if(tok.is("attribute")){
            // Register a stub at this source position so the main-parse emitter can see the
            // attribute's declaration index. Without this, `extern attribute light;` gets
            // appended to globals at main-parse time (after class pre-scan stubs), which
            // breaks source-order dependency checks for `has light` clauses.
            token nameTok = file.getToken();
            string nameStr = nameTok.value;
            transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
            bool alreadyReg = false;
            for(typeDef* g : languageService.globals)
                if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                    if(vd->name == nameStr && vd->type.name == "attribute"){ alreadyReg = true; break; }
            if(!alreadyReg){
                variableDeclaration& stub = *(new variableDeclaration());
                stub.name = nameStr;
                stub.type.name = "attribute";
                stub.isPrePassStub = true;
                stub.isExternal = isExtern;
                languageService.globals.push_back(&stub);
            }
            preScanSkipToSemicolon();
            continue;
        }

        // Data type declaration: function, typed object, or global variable
        if(tok.is(eTokenType::dataType) || tok.is(eTokenType::identifier)){
            string typeName = tok.value;
            preScanConsumeGenericSuffix(tok);
            // Consume a dotted namespace type path: identifier.identifier.identifier …
            // Without this, a decl like `bgl.glulx.window winStatus;` never registers a stub,
            // so the variable gets appended to `globals` at the END of main-parse registration,
            // causing it to be emitted after its uses (I6 "Variable must be defined before use").
            while(file.peekToken().is(token::period)){
                token seg = file.peekToken(2);
                if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
                file.getToken(); file.getToken();
                typeName += "." + seg.value;
            }
            token nameTok = file.getToken();
            if(!nameTok.is(eTokenType::identifier)){ preScanSkipToSemicolon(); continue; }
            string nameStr = nameTok.value;
            transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);

            // Optional ': ClassName' for typed object declarations
            token sym = file.getToken();
            if(sym.is(":")){
                file.getToken(); // class name
                sym = file.getToken(); // should be '{'
            }

            if(sym.is(token::parenOpen)){
                // Function declaration — register stub with return type.
                // We dedup by full signature (name + param types) rather than name
                // alone, so each overload of an overloaded function (e.g. print(int)
                // vs print(string) vs print(stringLiteral)) gets its own stub. Without
                // this, only the first overload was registered and resolveGlobalCall
                // would reject calls whose arg types matched a later overload.
                functionDef& stub = *(new functionDef());
                stub.name = nameStr;
                stub.returnType.name = typeName;
                stub.isEmitter = isEmitter;
                stub.isPrePassStub = true;
                preScanCaptureParams(stub.params);
                bool alreadyReg = false;
                for(typeDef* g : languageService.globals){
                    auto* fd = dynamic_cast<functionDef*>(g);
                    if(fd == nullptr || fd->name != nameStr) continue;
                    if(fd->params.size() != stub.params.size()) continue;
                    bool sameSig = true;
                    for(size_t i = 0; i < fd->params.size(); i++){
                        if(fd->params[i]->type.name != stub.params[i]->type.name){
                            sameSig = false; break;
                        }
                    }
                    if(sameSig){ alreadyReg = true; break; }
                }
                functionDef* stubPtr = nullptr;
                if(!alreadyReg){
                    languageService.globals.push_back(&stub);
                    stubPtr = &stub;
                }
                token peek = file.peekToken();
                if(peek.is(token::braceOpen)){
                    // For emitter functions, capture the body during prescan so forward
                    // calls inside the same .inf island (where the stub remains in globals)
                    // can inline correctly. Without this, the stub has a null body and the
                    // call site falls back to verbatim emission. Main pass will replace the
                    // stub with a fresh functionDef when it processes the actual declaration.
                    if(stubPtr != nullptr && isEmitter){
                        file.getToken(); // consume '{'
                        i6Block* body = new i6Block();
                        body->i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
                        stubPtr->body = body;
                    } else preScanSkipBody();
                } else preScanSkipToSemicolon();
            } else if(sym.is(token::braceOpen) && isEmitter){
                // Emitter value: emitter Type name { body } — register stub, skip body
                bool alreadyReg = false;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == nameStr){ alreadyReg = true; break; }
                if(!alreadyReg){
                    functionDef& stub = *(new functionDef());
                    stub.name = nameStr;
                    stub.returnType.name = typeName;
                    stub.isEmitter = true;
                    stub.isValueEmitter = true;
                    stub.isPrePassStub = true;
                    languageService.globals.push_back(&stub);
                }
                file.getRawTextThroughClosingBrace();
            } else if(sym.is(token::braceOpen)){
                // Type-named object: Room myRoom { }
                if(!languageService.isObjectType(nameStr)){
                    objectDef& stub = languageService.registerObject(nameStr, isExtern);
                    stub.isPrePassStub = true;
                    // Record the declared class so forward references resolve correctly
                    if(!typeName.empty() && typeName != "object"){
                        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(typeName));
                        if(cls != nullptr) stub.objectClass = cls;
                    }
                }
                preScanSkipBodyContents();
            } else if(sym.is(token::endStatement) || sym.is(token::assignment) ||
                      sym.is(token::bracketOpen)){
                // Global variable declaration
                bool alreadyReg = false;
                for(typeDef* g : languageService.globals)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == nameStr){ alreadyReg = true; break; }
                if(!alreadyReg){
                    variableDeclaration& stub = *(new variableDeclaration());
                    stub.name = nameStr;
                    stub.type.name = typeName;
                    stub.isPrePassStub = true;
                    stub.isExternal = isExtern;
                    languageService.globals.push_back(&stub);
                }
                if(sym.isNot(token::endStatement)) preScanSkipToSemicolon();
            } else {
                preScanSkipToSemicolon();
            }
            continue;
        }

        // Anything else — skip to next semicolon or brace block
        preScanSkipToSemicolon();
    }
}

// Pre-scan the bgl content of one #bgl-variant island as if it were a fragment of a
// .bgl file at global scope. Opens `content` as a virtual file, runs preScanGlobalLoop,
// closes. Caller is responsible for having already loaded any prerequisite types
// (typically BLR via the outer preScanFile call).
//
// Source-order discipline: any entries pushed to `languageService.globals` during the
// island's pre-scan are erased on return. Type registrations in `objectTypes` (and the
// equivalent enum/object/verb lists) persist so forward type-references resolve in main
// parse. Main parse will push the declarations into `globals` at their correct source
// positions via parseInfFileBody's interleave-with-compositeNode flow; the modified
// register* helpers in bglLanguageService re-push entries when filling in a stub that
// has been removed from globals.
void bglParser::preScanBglIslandContent(const std::string& content, const std::string& virtualName, int startLine){
    size_t globalsBefore = languageService.globals.size();
    file.openText(content, virtualName, startLine);
    preScanGlobalLoop();
    file.close();
    if(languageService.globals.size() > globalsBefore){
        // Erase the additions to preserve source-order interleaving with raw-I6 compositeNodes
        // during Pass 2. Exception: keep functionDef stubs (regular and emitter functions).
        // These need to remain visible during Pass 2 so forward calls — `the(noun)` referencing
        // `emitter void the(object)` declared later in the island — resolve correctly. The
        // stub gets claimed in place by the main-parse register helper when the actual decl
        // is processed; positional drift relative to raw-I6 is acceptable for functions
        // because their I6 emission is order-independent (forward references work in I6 too).
        auto it = languageService.globals.begin() + globalsBefore;
        while(it != languageService.globals.end()){
            if(dynamic_cast<functionDef*>(*it)) ++it;
            else it = languageService.globals.erase(it);
        }
    }
}

// .inf-mode Pass 1: walk the open .inf file looking for #bgl-variant islands.
// For #bglDecl islands and #bgl islands whose first content token signals declaration
// mode (matching the runtime discriminator in parseInfFileBody), pre-scan the island
// content via preScanBglIslandContent so types declared there register before Pass 2.
// #bglStmt islands and statement-mode #bgl islands contain no top-level declarations
// and are skipped.
void bglParser::preScanInfFileBodyForDecls(){
    int depth = 1;  // pseudo-depth: stays > 0 until EOF (forced to 0 by getRawText...'s eofTerminates)
    while(depth > 0){
        eBglDirective directive = eBglDirective::NotFound;
        // Discard the raw I6 text between islands — pre-scan only cares about Beguile
        // content. The full body is re-walked during Pass 2 (parseInfFileBody).
        (void)file.getRawTextUntilCloseOrBgl(directive, depth, depth, /*eofTerminates=*/true);
        if(directive == eBglDirective::NotFound) break;

        // Capture the island's bgl content (multi-line or single-line form).
        sourceLocation hereLoc = file.currentLocation();
        bool isMultiLine = false;
        while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
        if(file.peekChar() == '{'){
            file.readChar();
            isMultiLine = true;
        }
        string bglContent;
        if(isMultiLine){
            file.braceDepth++;
            bglContent = file.getRawTextThroughClosingBrace();
        } else {
            char c = file.peekChar();
            while(c != '\n' && c != (char)EOF){
                file.readChar();
                bglContent += c;
                c = file.peekChar();
            }
        }

        // Decide whether this island contains declarations that need pre-scanning.
        bool shouldPreScan = false;
        if(directive == eBglDirective::BglDecl){
            shouldPreScan = true;
        } else if(directive == eBglDirective::Bgl){
            // Mirror the auto-detect logic in parseInfFileBody. We open a temporary virtual
            // file just to peek the first few tokens, then close it (the actual pre-scan
            // re-opens fresh below).
            file.openText(bglContent, hereLoc.file, hereLoc.line);
            file.bleedSpaces();
            if(file.peekChar() != (char)EOF){
                token t1 = file.peekToken();
                if(t1.is(eTokenType::directive)){
                    shouldPreScan = true;
                } else if(t1.is("class") || t1.is("enum") || t1.is("namespace") ||
                          t1.is("grammar") || t1.is("emitter") || t1.is("extend") ||
                          t1.is(token::external) || t1.is("static") || t1.is("explicit")){
                    shouldPreScan = true;
                } else if(t1.is("void") || t1.isDataType()){
                    token t2 = file.peekToken(2);
                    if(t2.is(eTokenType::identifier)){
                        token t3 = file.peekToken(3);
                        if(t3.is(token::parenOpen)) shouldPreScan = true;
                    }
                }
            }
            file.close();
        }
        // BglStmt: never pre-scan (no top-level declarations).

        if(shouldPreScan)
            preScanBglIslandContent(bglContent, hereLoc.file, hereLoc.line);
    }
}
