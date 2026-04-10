// bglPreScanner.cpp — Pre-scanner (Pass 1) for the Beguile compiler.
// Registers type/object/function stubs so forward references resolve in Pass 2.
// These are bglParser methods extracted for code organization.

#include "bglParser.h"
#include "bglLanguageService.h"
#include "settings.h"
#include <filesystem>

using namespace std;

// Defined in bglParser.cpp
filesystem::path findCaseInsensitive(const filesystem::path& dir, const string& target);
string resolveIncludePath(const string& name, const string& ext, const filesystem::path& baseDir, const vector<string>& includePaths);
string rewritePathSeps(const string& path);

//==================================================================
// Pre-scanner (Pass 1): registers type/object/function stubs so that
// forward references resolve correctly in the full parse (Pass 2).
//==================================================================

// Body-skipping helpers use getRawTextThroughClosingBrace (char-level) to safely skip
// content containing I6 raw code like ##VerbName without triggering the token classifier.
void bglParser::preScanSkipBodyContents(){
    // Opening '{' already consumed. Skip raw chars until matching '}'.
    file.getRawTextThroughClosingBrace();
}
void bglParser::preScanSkipBody(){
    // Consume opening '{' then skip to matching '}'.
    token t = file.getToken();
    if(!t.is(token::braceOpen)) return;
    file.getRawTextThroughClosingBrace();
}
// Skip tokens in a false #if branch during pre-scan until #elif/#else/#endif at depth 0.
// On #elif: evaluate condition and either continue scanning or skip again.
// On #else: continue scanning (it's the true branch).
// On #endif: done.
void bglParser::preScanSkipConditionalBlock(){
    int depth = 1;
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) return;
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("#if")) { depth++; continue; }
        if(t.is("#endif")) { depth--; if(depth == 0) return; continue; }
        if(depth == 1 && t.is("#elif")){
            // Evaluate this branch's condition
            string condText;
            token ct = file.getBasicToken(true);
            while(ct.isNot("\n") && ct.isNot(eTokenType::eof)){ condText += ct.value; ct = file.getBasicToken(true); }
            if(evaluateCondition(condText))
                return; // condition true — resume normal pre-scanning
            // else continue skipping
        }
        if(depth == 1 && t.is("#else")){
            return; // #else is the true branch — resume normal pre-scanning
        }
    }
}
void bglParser::preScanSkipToSemicolon(){
    while(true){
        token t = file.getToken();
        if(t.is(token::endStatement) || t.is(eTokenType::eof)) return;
        if(t.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
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
            filesystem::path libPath = findCaseInsensitive(settings.libPath, includeName2 + ".bgl");
            if(filesystem::exists(libPath)) preScanFile(libPath.string());
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
    } else if(tok.value == "#beguilersettings"){
        preScanSkipBody(); // skip { target = Glulx; ... } so the closing } doesn't corrupt the token stream
    } else if(tok.value == "#includei6"){
        // Skip optional '?' marker
        if(file.peekToken().is("?")) file.getToken();
        token filename = file.getToken(); // consume the quoted filename
        string innerPath = filename.value;
        if(innerPath.size() >= 2 && innerPath.front()=='"' && innerPath.back()=='"')
            innerPath = innerPath.substr(1, innerPath.size()-2);
        // Resolve path for the stub so it matches the full-pass resolution
        filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
        string resolved = resolveIncludePath(innerPath, "", curDir, beguilerSettings.includePaths);
        if(resolved.empty()) resolved = resolveIncludePath(innerPath, ".h", curDir, beguilerSettings.includePaths);
        string emitPath = resolved.empty()
            ? "\"" + rewritePathSeps(innerPath) + "\""
            : "\"" + rewritePathSeps(resolved) + "\"";
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
        // Skip content: multi-line (braced) or single-line (to newline)
        if(file.peekToken().is(token::braceOpen))
            preScanSkipBody();
        else {
            token t = file.getBasicToken(true);
            while(t.isNot("\n") && t.isNot(eTokenType::eof)) t = file.getBasicToken(true);
        }
    } else if(tok.is("#using")){
        // Parse target name (class or object)
        token t = file.getToken();
        string targetName = t.value;
        transform(targetName.begin(), targetName.end(), targetName.begin(), ::tolower);
        // Resolve: try as a registered class, then as a global object (use its class)
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(targetName));
        if(!cls){
            for(typeDef* g : languageService.globals)
                if(auto* od = dynamic_cast<objectDef*>(g))
                    if(od->name == targetName){ cls = od->objectClass; break; }
        }
        if(cls) usingImports.push_back(cls);
    } else if(tok.is("#if")){
        // Evaluate condition and skip false branch
        string condText;
        token t = file.getBasicToken(true);
        while(t.isNot("\n") && t.isNot(eTokenType::eof)){ condText += t.value; t = file.getBasicToken(true); }
        if(!evaluateCondition(condText))
            preScanSkipConditionalBlock();
    } else if(tok.is("#elif")){
        // Reached here because prior #if branch was TRUE — skip to #endif
        int depth = 1;
        while(depth > 0){
            token t = file.getToken();
            if(t.is(eTokenType::eof)) break;
            if(t.is("#if")) depth++;
            if(t.is("#endif")) depth--;
        }
    } else if(tok.is("#else")){
        // Reached here because prior #if was TRUE — skip to #endif
        int depth = 1;
        while(depth > 0){
            token t = file.getToken();
            if(t.is(eTokenType::eof)) break;
            if(t.is("#if")) depth++;
            if(t.is("#endif")) depth--;
        }
    } else if(tok.is("#endif")){
        // no-op — consumed naturally
    }
    // Other directives (#message, #error, #warning, #exit, ##ifdef, etc.) are ignored during pre-scan
}

void bglParser::preScanFile(string filename){
    string absPath = filesystem::canonical(filesystem::absolute(filename)).string();
    if(preScanOnceFiles.count(absPath)) return;

    try { file.open(absPath); }
    catch(runtime_error&){ return; } // silently skip missing files

    bool isFirst = (preScanDepth == 0);
    preScanDepth++;

    if(isFirst){
        // Mirror main parse: always load _beguileCore.bgl first
        filesystem::path sysPath = filesystem::path(settings.libPath) / "core" / "_beguileCore.bgl";
        preScanFile(sysPath.string());
        file.moveToStart();
    }

    while(true){
        token tok = file.getToken();
        if(tok.is(eTokenType::eof)) break;

        if(tok.is(eTokenType::directive)){ preScanDirective(tok); continue; }

        Qualifiers q = parseQualifiers(tok);
        bool isExtern  = q.isExtern;
        bool isEmitter = q.isEmitter;

        // extend class/object — register new members on the existing type during pre-scan
        // Note: 'extend' was consumed by parseQualifiers; tok is now 'class', 'verb', identifier, or 'extern'
        if(q.isExtend){
            // extend object by name — just skip the body (already registered by its own decl)
            if((tok.is(eTokenType::identifier) || tok.isDataType()) && !tok.is(token::classDeclaration)){
                // extend ObjectName { } — object extension; skip body during pre-scan
                preScanSkipBody();
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
                    if(t.isDataType()){
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
                            preScanSkipParens();
                            token bodyStart = file.getToken();
                            if(bodyStart.is(token::braceOpen)) file.getRawTextThroughClosingBrace();
                            // Add if not already present (or if replace)
                            bool exists = false;
                            for(typeMember* m : cls->members)
                                if(m->name == fd.name){ exists = true; break; }
                            if(!exists || memberIsReplace) cls->members.push_back(&fd);
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

        bool isAliasClass = false;
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
                file.getToken(); // consume '{'
                token t = file.getToken();
                int val = 1;
                while(t.isNot(token::braceClose)){
                    enumValueDef& ev = *(new enumValueDef());
                    ev.name = t.value;
                    t = file.getToken({token::braceClose, token::comma, token::assignment});
                    if(t.is(token::assignment)){
                        token numTok = file.getToken(eTokenType::integer);
                        val = stoi(numTok.value);
                        if(isBnum && val != 0 && (val & (val - 1)) != 0)
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
                            // Skip array<T> generics
                            if(t.value == "array" && file.peekToken().is("<")){
                                file.getToken(); // consume <
                                file.getToken(); // consume element type
                                file.getToken(); // consume >
                            }
                            token memberName = file.getToken();
                            token afterName = file.getToken();
                            if(afterName.is(token::parenOpen)){
                                // Method: register a stub
                                functionDef& fd = *(new functionDef());
                                fd.name = memberName.value;
                                fd.returnType = languageService.getType(t.value);
                                fd.isEmitter = memberIsEmitter;
                                fd.isPrePassStub = true;
                                // Skip params and body
                                preScanSkipParens();
                                token bodyStart = file.getToken();
                                if(bodyStart.is(token::braceOpen))
                                    file.getRawTextThroughClosingBrace();
                                // Only add if not already registered
                                bool exists = false;
                                for(typeMember* m : objStub->members)
                                    if(m->name == fd.name){ exists = true; break; }
                                if(!exists) objStub->members.push_back(&fd);
                            } else {
                                // Property — skip to ; or next member
                                while(!afterName.is(token::endStatement) && !afterName.is(token::braceClose) && !afterName.is(eTokenType::eof)){
                                    if(afterName.is(token::braceOpen)){ file.getRawTextThroughClosingBrace(); break; }
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
            file.getToken(); // name
            preScanSkipToSemicolon();
            continue;
        }

        // Data type declaration: function, typed object, or global variable
        if(tok.is(eTokenType::dataType) || tok.is(eTokenType::identifier)){
            string typeName = tok.value;

            // Handle func<ReturnType,...> generic type
            if(tok.value == "func" && file.peekToken().is("<")){
                file.getToken(); // '<'
                int depth = 1;
                while(depth > 0){
                    token t2 = file.getToken();
                    if(t2.value == "<") depth++;
                    else if(t2.value == ">") depth--;
                    else if(t2.is(eTokenType::eof)) break;
                }
            }

            // Handle array<T>
            if(tok.value == "array" && file.peekToken().is("<")){
                file.getToken(); // '<'
                file.getToken(); // element type
                file.getToken(); // '>'
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
                // Function declaration — register stub with return type
                bool alreadyReg = false;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == nameStr){ alreadyReg = true; break; }
                if(!alreadyReg){
                    functionDef& stub = *(new functionDef());
                    stub.name = nameStr;
                    stub.returnType.name = typeName;
                    stub.isPrePassStub = true;
                    languageService.globals.push_back(&stub);
                }
                preScanSkipParens(); // skip parameter list
                token peek = file.peekToken();
                if(peek.is(token::braceOpen)) preScanSkipBody();
                else preScanSkipToSemicolon();
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

    file.close();
    preScanDepth--;
}
