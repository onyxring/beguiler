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
static string rewritePathSeps(const string& path){
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
static filesystem::path findCaseInsensitive(const filesystem::path& dir, const string& target){
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

// Open an input file, process each statement.  This is the entry point to this whole parsing process
bool bglParser::parseFile(string filename){
    string absPath = filesystem::absolute(filename).string();
    // If this file declared #once, silently skip subsequent inclusions.
    if(onceFiles.count(absPath)) return false;
    // Guard against runaway or circular includes.
    if(includeDepth > maxIncludeDepth)
        return parsingError(format("Maximum include nesting depth ({0}) exceeded while including '{1}'", maxIncludeDepth, filename));
    includeDepth++;

    try{
        file.open(absPath);
    }
    catch(runtime_error& e){
        includeDepth--;
        return parsingError(e.what());
    }

    if(file.getNumberOfOpenFiles()==1){ //this is the first file, so let's do a little inspection and try to determine our default mode
        if(file.readChar()=='!' && file.readChar()=='%'){ //is the first line one of Inform's IGL declarations?
            compileLanguageStack.push_front(eCompileLanguage::i6);        //if so, since IGL's are not legal in Beguile, we can assume Beguile is being used as a preprocessor for I6.
            cout<<format("Detected IGL in first line of file {0}. Dawning the preprocessor mantle.\n",filename);
        }
        else{
            compileLanguageStack.push_front(eCompileLanguage::beguile); //use default mode
            cout<<format("Beguiling file \"{0}\"...\n",filename);
        }

        //now that we've checked for that convention, rewind the stream pointer to the beginning for actual processing.
        file.moveToStart();

        // Always load the system library first so built-in types (eBool, eTarget, etc.) are available
        filesystem::path systemPath = filesystem::path(settings.libPath) / "_beguileCore.bgl";
        parseFile(systemPath.string());
        // Load built-in I6 templates (for-in loop etc.) from beguilib/_builtins.i6b
        filesystem::path builtinsPath = filesystem::path(settings.libPath) / "_builtins.i6b";
        emitter.loadBuiltinTemplates(builtinsPath.string());
    }

    //process all statements in the file one by one.  This may include recursive calls for included files.
    try{
        while(processNextStatement()==false){
            //cout<<"Block processed."<<endl;
        }
    } catch(exitFileSignal&){
        // #exit directive: treat as end-of-file, discarding any open directive nesting
    }
    //emit.out<<endl;
    file.close();
    includeDepth--;
    return false;
}

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
void bglParser::preScanSkipToSemicolon(){
    while(true){
        token t = file.getToken();
        if(t.is(token::endStatement) || t.is(eTokenType::eof)) return;
        if(t.is(token::braceOpen)){ file.getRawTextThroughClosingBrace(); return; }
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
        preScanOnceFiles.insert(filesystem::absolute(name).string());
    } else if(tok.is("#include")){
        token next = file.getToken();
        if(next.isString()){
            string includeName = next.value;
            if(includeName.size() >= 2 && includeName.front()=='"' && includeName.back()=='"')
                includeName = includeName.substr(1, includeName.size()-2);
            auto [stream, curFile, line, col] = file.getCurrentFileDetail();
            filesystem::path curDir = filesystem::path(curFile).parent_path();
            filesystem::path candidate = curDir / (includeName + ".bgl");
            if(!filesystem::exists(candidate)) candidate = curDir / includeName;
            if(!filesystem::exists(candidate)){
                for(const string& sp : beguilerSettings.bglIncludePaths){
                    filesystem::path c2 = filesystem::path(sp) / (includeName + ".bgl");
                    if(!filesystem::exists(c2)) c2 = filesystem::path(sp) / includeName;
                    if(filesystem::exists(c2)){ candidate = c2; break; }
                }
            }
            if(filesystem::exists(candidate)) preScanFile(candidate.string());
            // raw I6 includes: nothing to pre-scan
        } else if(next.is("<")){
            string includeName2;
            token t2 = file.getToken();
            while(!t2.is(">") && !t2.is(eTokenType::eof)){
                includeName2 += t2.originalValue.empty() ? t2.value : t2.originalValue;
                t2 = file.getToken();
            }
            filesystem::path libPath = findCaseInsensitive(settings.libPath, includeName2 + ".bgl");
            preScanFile(libPath.string());
        }
    } else if(tok.is("#define")){
        file.getToken(); // symbol name — skip rest of line handled naturally
    } else if(tok.value == "#beguilersettings"){
        preScanSkipBody(); // skip { target = Glulx; ... } so the closing } doesn't corrupt the token stream
    } else if(tok.value == "#includei6"){
        token filename = file.getToken(); // consume the quoted filename
        string innerPath = filename.value;
        if(innerPath.size() >= 2 && innerPath.front()=='"' && innerPath.back()=='"')
            innerPath = '"' + rewritePathSeps(innerPath.substr(1, innerPath.size()-2)) + '"';
        i6RawNode& stub = *(new i6RawNode());
        stub.text = format("#include {0};", innerPath);
        stub.isPrePassStub = true;
        languageService.globals.push_back(&stub);
    } else if(tok.value == "#i6"){
        // Reserve position in globals so main-pass content appears in source order
        i6RawNode& stub = *(new i6RawNode());
        stub.text = "#i6_placeholder";
        stub.isPrePassStub = true;
        languageService.globals.push_back(&stub);
        preScanSkipBody(); // skip actual content — not Beguile declarations
    }
    // All other directives (#ifdef, #if, #else, #endif, etc.) are ignored:
    // the pre-scan reads through both sides of conditional blocks, which is
    // correct because we want all declared names visible regardless of conditions.
}

void bglParser::preScanFile(string filename){
    string absPath = filesystem::absolute(filename).string();
    if(preScanOnceFiles.count(absPath)) return;

    try { file.open(absPath); }
    catch(runtime_error&){ return; } // silently skip missing files

    bool isFirst = (preScanDepth == 0);
    preScanDepth++;

    if(isFirst){
        // Mirror main parse: always load _beguileCore.bgl first
        filesystem::path sysPath = filesystem::path(settings.libPath) / "_beguileCore.bgl";
        preScanFile(sysPath.string());
        file.moveToStart();
    }

    while(true){
        token tok = file.getToken();
        if(tok.is(eTokenType::eof)) break;

        if(tok.is(eTokenType::directive)){ preScanDirective(tok); continue; }

        bool isExtern  = false;
        bool isEmitter = false;
        if(tok.is("extern"))  { isExtern  = true; tok = file.getToken(); }
        if(tok.is("emitter")) { isEmitter = true; tok = file.getToken(); }
        if(tok.is("replace")) { tok = file.getToken(); }
        if(tok.is("const"))   { tok = file.getToken(); }

        // extend class — skip; full pass will find the existing class and add members
        if(tok.is("extend")){
            file.getToken(); // "class"
            file.getToken(); // class name
            preScanSkipBody();
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
            if(!languageService.isObjectType(nameStr)){
                classDef& stub = languageService.registerClass(nameStr, isExtern);
                stub.isPrePassStub = true;
                if(isEmitter)     stub.isEmitterClass = true;
                if(isAliasClass)  stub.isAlias = true;
            }
            // Skip past any inheritance clause then the body.
            { token t = file.getToken();
              while(!t.is(token::braceOpen) && !t.is(token::endStatement) && !t.is(eTokenType::eof))
                  t = file.getToken();
              if(t.is(token::braceOpen)) file.getRawTextThroughClosingBrace(); }
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
        if(tok.is("object")){
            token nameTok = file.getToken();
            string nameStr = nameTok.value;
            transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
            token peek = file.peekToken();
            if(peek.is(token::braceOpen) || peek.is(":")){ // object Name { } or object Name : Class { }
                if(!languageService.isObjectType(nameStr)){
                    objectDef& stub = languageService.registerObject(nameStr, isExtern);
                    stub.isPrePassStub = true;
                }
                preScanSkipBody();
            } else {
                // extern object Name; or object Name = ...; — variable stub
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
                preScanSkipToSemicolon();
            }
            continue;
        }

        // verb keyword
        if(tok.is("verb")){
            token nameTok = file.getToken();
            string nameStr = nameTok.value;
            transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
            bool alreadyReg = false;
            for(verbObjectDef* v : languageService.verbs)
                if(v->name == nameStr){ alreadyReg = true; break; }
            if(!alreadyReg){
                verbObjectDef& stub = languageService.registerVerbObject(nameTok.value, isExtern);
                stub.isPrePassStub = true;
            }
            if(isExtern) preScanSkipToSemicolon();
            else         preScanSkipBody();
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

//------------------------------------------------------------------
// Parse the next statement in the input file.  Return true if we've
//      reached the end of the current code block, false otherwise.
//      If there is a parse error, throw an exception with the details.
bool bglParser::processNextStatement(abstractObject& contextObject){
    bool isExtern=false;
    bool isEmitter=false;
    bool isConst=false;
    bool isExtend=false;
    bool isReplace=false;
    token tok=file.getToken();
    sourceLocation stmtLoc = file.currentLocation();

    if(tok.is(token::braceClose)) return true;  //return true: signal to the calling routine that we've reached the end of the code/scope block.
    if(tok.is(eTokenType::eof)) return true;    //return true: same as above; end of file is just another form of end of code block.  TODO: this approach may need to be re-evaluated since an unclosed code block at the end of a file is a common error condition that we may want to report differently than a properly closed code block.

    //decide what to do with this token----------------------------------------------------------------------------
    if(tok.is(eTokenType::directive)) return processDirective(tok, contextObject); //handle preprocessor directives

    if(tok.is(token::replace)) { //replace qualifier: replace an existing global function with this definition
        isReplace=true;
        tok=file.getToken();
    }
    if(tok.is(token::extend)) { //extend qualifier: add members to an existing class definition
        isExtend=true;
        tok=file.getToken();
    }
    if(tok.is(token::constantDeclararion)) { //const qualifier: compile-time constant
        isConst=true;
        tok=file.getToken();
    }
    if(tok.is(token::external)) { //whatever this token is, if it is preceded by the extern qualifier, capture that and grab the next for processing
        isExtern=true;
        tok=file.getToken();
    }
    if(tok.is(token::constantDeclararion)) { //const after extern: extern const <type> name;
        isConst=true;
        tok=file.getToken();
    }
    if(tok.is("emitter")) { //emitter qualifier: the body of this function is raw I6 code
        isEmitter=true;
        tok=file.getToken();
    }
    if(tok.isOneOf({token::enumDeclaration, token::bnumDeclaration})) return processEnumDeclaration(tok, isExtern);

    // Global emitter object: 'emitter Foo { }' — emitter modifier followed by bare identifier then '{'
    if(isEmitter && (tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)) && file.peekToken().is(token::braceOpen)){
        // Re-use processClassDeclaration; pass tok as nameOverride since we've already consumed the name.
        return processClassDeclaration(tok, false, isExtend, true, false, tok);
    }

    bool isAliasClass = false;
    if(tok.is("alias")) { isAliasClass = true; tok = file.getToken(); } // consume 'class'
    if(tok.is(token::classDeclaration)) return processClassDeclaration(tok, isExtern, isExtend, isEmitter, isAliasClass);
    // verb/grammar keywords must be checked before the isDataType() branch because 'verb' is
    // also a registered class name and would otherwise be caught as a type declaration.
    if(!isExtern && tok.is("verb"))    return processVerbDeclaration(isExtern);
    if(!isExtern && tok.is("grammar")) return processGrammarDeclaration();

    // A declared object's name is both its type and its singleton instance reference.
    // In non-global context it can never appear as a type, so route directly to
    // processStatement where it will be re-classified as an identifier.
    if(!isExtern && getCurrentCompileContext() != eCompileContext::global && tok.isDataType())
        if(dynamic_cast<objectDef*>(&languageService.getType(tok.value)) != nullptr){
            processStatement(tok, contextObject);
            return false;
        }

    if(tok.isDataType()) { //if the token is a datatype, then this is the start of either a variable declaration or a global routine declaration.  We need to look ahead a little bit to decide which one it is.
        // Handle array<T> generic type
        string arrayElementType;
        if(tok.value == "array") {
            file.getToken("<");
            arrayElementType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
        }
        // Handle func<ReturnType, ParamTypes...> generic type
        if(tok.value == "func") {
            tok.value = parseFuncType();
        }
        token name=file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(!arrayElementType.empty()) {
            token symbol = file.getToken({token::bracketOpen, token::assignment, token::endStatement});
            processArrayDeclaration(tok, name, arrayElementType, symbol, contextObject, isExtern);
            return false;
        }
        // Check for optional ': ClassName' class annotation before the '{' (object instance declaration)
        string objectClassName;
        if(file.peekToken().is(":")){
            file.getToken(); // consume ':'
            token classNameTok = file.getToken(eTokenType::dataType);
            objectClassName = classNameTok.value;
        }
        // Check for optional 'as i6name' I6 alias on global instance declarations
        string i6alias;
        if(file.peekToken().is("as")){
            file.getToken(); // consume 'as'
            token aliasTok = file.getToken(eTokenType::identifier);
            i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
        }
        token symbol=file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});

        if(symbol.is(token::parenOpen))
            processRoutineDeclaration(tok, name, contextObject, isExtern, isEmitter, isReplace);
        else if(symbol.is(token::braceOpen))
            processObjectDeclaration(tok, name, isExtern, objectClassName, i6alias);
        else
            processVariableDeclaration(tok, name, symbol, contextObject, isExtern, isConst, i6alias);
        return false;
    }

    // beguilerSettings is now a directive (#beguilerSettings); handled in processDirective
    if(tok.is("verb"))    return processVerbDeclaration(isExtern);
    if(tok.is("grammar")) return processGrammarDeclaration();

    //that's all we allow in the global context.  Throw an error otherwise...
    if(getCurrentCompileContext()==eCompileContext::global) parsingError(format("Illegal global identifier:'{0}'", (string) tok));
    
    //nothing beyond this point can be marked as external...
    if(isExtern==true) parsingError(format("Extern declaration only valid for global variables, classes, routines, and enums:'{0}'", (string) tok));
    
    //must be in the context of a code block at this point, so we are expecting an executable statement.
    processStatement(tok, contextObject);
    return false;
}

//===============================================================================================================================
// Routines to parser larger blocks of the source code
//-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
#pragma region Parsing functions
// Returns true if the statement block contains a returnStatement anywhere (recursive).
static bool hasReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* is = dynamic_cast<ifStatement*>(s))
            if(hasReturn(is->thenBlock) || hasReturn(is->elseBlock)) return true;
        if(auto* ws = dynamic_cast<whileStatement*>(s))   if(hasReturn(ws->body))  return true;
        if(auto* ds = dynamic_cast<doStatement*>(s))      if(hasReturn(ds->body))  return true;
        if(auto* fs = dynamic_cast<forStatement*>(s))     if(hasReturn(fs->body))  return true;
        if(auto* sw = dynamic_cast<switchStatement*>(s))
            for(switchCase* c : sw->cases) if(hasReturn(c->body)) return true;
    }
    return false;
}

bool bglParser::processEnumDeclaration(token tok, bool isExternal){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration)) isBnum=true;
    token name=file.getToken({eTokenType::identifier, eTokenType::dataType}); //enum name
    tok=file.getToken(token::braceOpen);
    enumDef& newEnum=languageService.registerEnum((string)name, isExternal);
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
    if(isExternal && isEmitterClass) parsingError("'extern' and 'emitter' are mutually exclusive on a class declaration");
    if(isAlias && isExternal)        parsingError("'alias' and 'extern' are mutually exclusive on a class declaration");
    if(isAlias && isEmitterClass)    parsingError("'alias' and 'emitter' are mutually exclusive on a class declaration");

    token nameTok = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType});
    classDef* classPtr = nullptr;
    if(isExtend){
        classPtr = dynamic_cast<classDef*>(&languageService.getType((string)nameTok));
        if(classPtr == nullptr) parsingError(format("extend class '{0}': no previously defined class with that name", (string)nameTok));
    } else {
        //create an empty class definition object and register it immediately, so that we can refer to this type within its own definition (e.g. comparison operators)
        classPtr = &languageService.registerClass((string)nameTok, isExternal);
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
        bool isEmitter=false;
        bool isOperator=false;
        bool isReplace=false;
        token returnType;
        token name;
        if(tok.is(token::replace)) {
            if(!isExtend) parsingError("'replace' is only valid inside an 'extend class' definition");
            isReplace=true;
            tok=file.getToken();
        }
        if(tok.is("emitter")) {
            isEmitter=true;
            tok=file.getToken();
        }
        // emitter class: 'emitter' keyword is optional — all members are implicitly emitters
        if(newClass.isEmitterClass && !isEmitter) isEmitter = true;
        returnType=tok.assertDataType();
        name=file.getToken(eTokenType::identifier);
        if(name.is("operator")){
            isOperator=true;
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)){
                // conversion operator: emitter <type> operator()
                name.value = "operator()";
                tok = opTok; // the ( is already consumed; reuse it
            } else if(opTok.is(eTokenType::identifier)){
                // qualified operator: e.g. "prefix++" — read qualifier then oper symbol
                token opSym = file.getToken(eTokenType::oper);
                name.value = opTok.value + opSym.value;  // e.g. "prefix++"
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
            funcDef.name=(string) name;
            funcDef.returnType=languageService.getType((string) returnType);
            funcDef.isEmitter=isEmitter;
            processParameterList(funcDef);
            if(isEmitter && !funcDef.params.empty() && (funcDef.name == "init" || funcDef.name == "deinit"))
                parsingError(format("Emitter '{0}' cannot accept parameters", funcDef.name));
            if((isExternal || newClass.isExternal || newClass.isAlias) && !isEmitter){
                // extern/alias class non-emitter methods not allowed
                parsingError(format("Non-emitter function '{0}' is not allowed in an extern or alias class", funcDef.name));
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
                    while(processNextStatement(funcDef) == false){}
                    closeCompileContext(eCompileContext::codeBlock);
                    currentFunc = savedFunc;
                    if(funcDef.returnType.name != "void" && !hasReturn(dynamic_cast<statementBlock*>(funcDef.body)))
                        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
                }
            }
            if(isExtend){
                // find an existing member with the same name and parameter signature
                typeMember* existing = nullptr;
                for(typeMember* m : newClass.members){
                    functionDef* fd = dynamic_cast<functionDef*>(m);
                    if(fd && fd->name == funcDef.name && fd->params.size() == funcDef.params.size()){
                        bool match = true;
                        for(size_t i=0; i<funcDef.params.size(); i++){
                            if(fd->params[i]->type.name != funcDef.params[i]->type.name){ match=false; break; }
                        }
                        if(match){ existing = m; break; }
                    }
                }
                if(existing && !isReplace)
                    parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.name, funcDef.name));
                if(!existing && isReplace)
                    cerr << format("warning: replace '{0}' in class '{1}': no existing member with that signature\n", funcDef.name, newClass.name);
                if(existing)
                    for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                        if(*it==existing){ newClass.members.erase(it); break; }
            }
            if(funcDef.isEmitter && funcDef.name == "_bglglobaldeclaration"){
                // declaration emitter — store raw body on the class, not as a member
                i6Block* body = dynamic_cast<i6Block*>(funcDef.body);
                if(body) newClass.globalDeclarationBody = body->i6Body;
            } else {
                newClass.members.push_back(&funcDef);
            }
        }
        else{
            if(isOperator==true) parsingError("Operators must be functions.");
            if(isEmitter==true) parsingError("Emitters must be functions.");
            if(newClass.isEmitterClass) parsingError(format("Variable members are not allowed in 'emitter class {0}'", newClass.name));
            if(tok.isNot(token::endStatement) && tok.isNot(token::assignment))
                parsingError(format("Expected '=' or ';' after member '{0}'", (string)name));
            if(newClass.isAlias && tok.is(token::assignment))
                parsingError(format("Member '{0}' in an alias class may not have a definition (value); declarations only", (string)name));
            variableDeclaration& varDef=*(new variableDeclaration());
            varDef.name=(string) name;
            varDef.type=languageService.getType((string) returnType);
            if(tok.is(token::assignment))
                varDef.declaredExpressionValue = parseExpression(file.getToken(), {token::endStatement}, nullptr, nullptr);
            if(isExtend){
                typeMember* existing = nullptr;
                for(typeMember* m : newClass.members){
                    variableDeclaration* vd = dynamic_cast<variableDeclaration*>(m);
                    if(vd && vd->name == varDef.name){ existing = m; break; }
                }
                if(existing && !isReplace)
                    parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.name, varDef.name));
                if(!existing && isReplace)
                    cerr << format("warning: replace '{0}' in class '{1}': no existing member with that name\n", varDef.name, newClass.name);
                if(existing)
                    for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                        if(*it==existing){ newClass.members.erase(it); break; }
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

    token bodyStart = file.getToken();
    if(bodyStart.is(token::braceOpen)){
        // Block body
        functionDef* savedFunc = currentFunc;
        currentFunc = &fd;
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatement(fd) == false){}
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

    // Lift: append to globals — I6 doesn't require routines to precede call sites,
    // and inserting at front would place lambdas before _bgl_temp (I6 compile error)
    languageService.globals.push_back(&fd);
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
            // consume <ElementType> and store full "array<ElementType>" for for-in resolution
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            paramTypeName = format("array<{0}>", elemType);
        }
        param.type=languageService.getType(paramTypeName);
        if(param.type.name.empty()) param.type.name = paramTypeName; // for func<...> types
        tok=file.getToken(); // name, "=", ",", or ")"
        if(tok.is(eTokenType::identifier)){
            param.name=(string) tok;
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
            if(cd->isGlobalEmitterObject)
                return name;
    }
    return "";
}

// Returns the I6-qualified form of an identifier based on scope:
//   - found in local params/vars → name (unqualified)
//   - found in current object's members → "self.name"
//   - found in enum values or globals → name (unqualified)
//   - not found → "" (caller should report an error)
std::string bglParser::qualifyIdentifier(std::string name, functionDef* func, statementBlock* body){
    if(name == "null") return "nothing";
    // Handle dot-path: qualify the head, then append the tail
    size_t dot = name.find('.');
    if(dot != string::npos){
        string head = name.substr(0, dot);
        string tail = name.substr(dot + 1);
        string qualifiedHead = qualifyIdentifier(head, func, body);
        if(qualifiedHead.empty()) return "";
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
    for(typeDef* g : languageService.globals)
        if(g->name == name) return g->i6name.empty() ? name : g->i6name;
    // Tier 4: verb names (action constants and verb variables)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name) return vd->name;
    }
    // Tier 5: global emitter objects (emitter bglStyle { }) — no instance, qualify as-is
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject)
                return name;
    }
    return "";
}

bool bglParser::isTypeCompatible(std::string argType, std::string paramType){
    if(paramType == "var") return true;  // var accepts any type without checking
    if(argType == paramType) return true;
    // func<...> compatibility: a func value is compatible with any func<...> param type
    if(argType == "func" && paramType.rfind("func<", 0) == 0) return true;
    if(argType.rfind("func<", 0) == 0 && paramType.rfind("func<", 0) == 0) return true;
    // array<T> compatibility: array is compatible with array<T> param
    if(argType == "array" && paramType.rfind("array<", 0) == 0) return true;
    if(argType.rfind("array<", 0) == 0 && paramType.rfind("array<", 0) == 0) return true;
    // Object subtyping: any class instance is compatible with 'object'
    if(paramType == "object"){
        if(dynamic_cast<classDef*>(&languageService.getType(argType)) != nullptr) return true;
    }
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
        return fn && fn->name == "operator()" && fn->params.empty() && fn->returnType.name == paramType;
    })) return true;
    return false;
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
            return fn && fn->name == "operator()" && fn->params.empty() && fn->isEmitter && fn->returnType.name == paramType && dynamic_cast<i6Block*>(fn->body) != nullptr;
        });
        if(found){
            auto* fn = dynamic_cast<functionDef*>(found);
            auto* blk = dynamic_cast<i6Block*>(fn->body);
            string b = processBglConditionals(blk->i6Body);
            string argText = args[i]->text();
            size_t pos = 0;
            while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
            args[i]->tokens.clear();
            args[i]->tokens.push_back(b);
            args[i]->resolvedType = paramType;
        }
    }
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
    auto replaceWord = [](string str, const string& from, const string& to) -> string {
        size_t pos=0;
        while((pos=str.find(from,pos))!=string::npos){
            bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$');
            bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
            if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
            else pos+=from.size();
        }
        return str;
    };

    while(true){
        if(isTerminator(cur)){ expr->terminator = cur.value; break; }

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
        else if(cur.is(token::parenClose)){
            if(parenDepth > 0) parenDepth--;
            expr->tokens.push_back(cur.value);
        }
        else if(cur.is(eTokenType::integer)){
            if(expr->resolvedType.empty()) expr->resolvedType = "intliteral";
            expr->tokens.push_back(cur.value);
        }
        else if(cur.isString()){
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
            expr->tokens.push_back(cur.value);
        }
        else if(cur.is(eTokenType::name)){
            token next = getNext();
            if(parenDepth == 0 && next.is(token::bracketOpen)){
                // Subscript access in expression: identifier[i]
                string arrName = cur.value;
                string arrType = resolveIdentifierType(arrName, func, body);
                classDef* arrCls = dynamic_cast<classDef*>(&languageService.getType(arrType));
                expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                functionDef* getMethod = nullptr;
                if(arrCls != nullptr)
                    for(typeMember* m : arrCls->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->name == "get") { getMethod = fd; break; }
                if(getMethod == nullptr) parsingError(format("No 'get' method on type '{0}'", arrType));
                if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                if(getMethod->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                        string b = processBglConditionals(blk->i6Body);
                        string pv = (arrType == "array") ? "0" : "<$prop undefined>";
                        b = replaceWord(b, "$self", arrName);
                        b = replaceWord(b, "$prop", pv);
                        if(!getMethod->params.empty())
                            b = replaceWord(b, getMethod->params[0]->name, indexExpr->text());
                        expr->tokens.push_back(b);
                    }
            }
            else if(next.is(token::parenOpen)){
                // Function call in expression context — emit as plain I6, use return type
                string retType;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == cur.value){ retType = fd->returnType.name; break; }
                if(retType == "void"){
                    parsingError(format("Cannot use void function '{0}' in an expression", cur.value));
                }
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                expr->tokens.push_back(cur.value);
                expr->tokens.push_back(token::parenOpen);
                int callDepth = 1;
                token argTok = file.getToken();
                while(callDepth > 0){
                    if(argTok.is(token::parenOpen)){
                        if(callDepth == 1){
                            // Check for inline lambda: ( type name => ... ) or ( ) =>
                            token p1 = file.peekToken(1);
                            bool isLambda = (p1.is(")") && file.peekToken(2).is("=>")) ||
                                            ((p1.is(eTokenType::dataType) || p1.is(eTokenType::identifier)) &&
                                              file.peekToken(2).is(eTokenType::identifier));
                            if(isLambda){
                                string lambdaName = parseLambdaExpr(func, body);
                                expr->tokens.push_back(lambdaName);
                                argTok = file.getToken();
                                continue;
                            }
                        }
                        callDepth++;
                    } else if(argTok.is(token::parenClose)){ callDepth--; if(callDepth==0) break; }
                    expr->tokens.push_back(argTok.value);
                    if(callDepth > 0) argTok = file.getToken();
                }
                expr->tokens.push_back(token::parenClose);
            }
            else if(parenDepth == 0 && next.is(token::period)){
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

                        // find best matching method, walking inheritance hierarchy
                        functionDef* method = nullptr;
                        functionDef* varFallback = nullptr;
                        bool nameMatch=false;
                        std::function<void(classDef*)> searchExprHierarchy = [&](classDef* c){
                            for(typeMember* m : c->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m)){
                                    if(fd->name == methName) nameMatch=true;
                                    if(fd->name == methName && fd->params.size() == callArgs.size()){
                                        bool usesVar = false, argsOk = true;
                                        for(size_t i = 0; i < callArgs.size(); i++){
                                            string at = callArgs[i]->resolvedType;
                                            string pt = fd->params[i]->type.name;
                                            if(pt == "var") usesVar = true;
                                            else if(!at.empty() && !isTypeCompatible(at, pt)){ argsOk = false; break; }
                                        }
                                        if(argsOk){
                                            if(usesVar){ if(!varFallback) varFallback = fd; }
                                            else { method = fd; return; }
                                        }
                                    }
                                }
                            if(method == nullptr)
                                for(classDef* base : c->baseClasses){ searchExprHierarchy(base); if(method) return; }
                        };
                        searchExprHierarchy(cls);
                        if(method == nullptr) method = varFallback;
                        // Also search the objectDef's own inline members (for plain object declarations)
                        if(method == nullptr){
                            string lowerObjName = objName;
                            transform(lowerObjName.begin(), lowerObjName.end(), lowerObjName.begin(), ::tolower);
                            for(typeDef* g : languageService.globals){
                                if(g->name != lowerObjName) continue;
                                if(auto* od = dynamic_cast<objectDef*>(g)){
                                    for(typeMember* m : od->members)
                                        if(auto* fd = dynamic_cast<functionDef*>(m)){
                                            if(fd->name == methName) nameMatch = true;
                                            if(fd->name == methName && fd->params.size() == callArgs.size()){
                                                bool usesVar = false, argsOk = true;
                                                for(size_t i = 0; i < callArgs.size(); i++){
                                                    string at = callArgs[i]->resolvedType;
                                                    string pt = fd->params[i]->type.name;
                                                    if(pt == "var") usesVar = true;
                                                    else if(!at.empty() && !isTypeCompatible(at, pt)){ argsOk = false; break; }
                                                }
                                                if(argsOk){
                                                    if(usesVar){ if(!varFallback) varFallback = fd; }
                                                    else { method = fd; }
                                                }
                                            }
                                        }
                                    break;
                                }
                            }
                            if(method == nullptr) method = varFallback;
                        }
                        if(method == nullptr) {
                            if(nameMatch==false)
                                parsingError(format("No method '{0}' defined on type '{1}'", methName, objType));
                            else
                                parsingError(format("No method '{0}' on type '{1}' matches arguments", methName, objType));
                        }

                        if(expr->resolvedType.empty()) expr->resolvedType = method->returnType.name;

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
                                    b = replaceWord(b, method->params[i]->name, callArgs[i]->text());
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
                            for(typeMember* m : arrCls->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == "get") { getMethod = fd; break; }
                        if(getMethod == nullptr) parsingError(format("No 'get' method on type '{0}'", propType));
                        if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                        if(getMethod->isEmitter)
                            if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                                string b = processBglConditionals(blk->i6Body);
                                b = replaceWord(b, "$self", objName);
                                b = replaceWord(b, "$prop", propName);
                                if(!getMethod->params.empty())
                                    b = replaceWord(b, getMethod->params[0]->name, indexExpr->text());
                                expr->tokens.push_back(b);
                            }
                    } else {
                        prefetched = afterMember;
                        // Distinguish enum-qualified access (EnumType.value → _EnumType_value)
                        // from object/variable property access (obj.prop → obj.prop)
                        bool isEnum = dynamic_cast<enumDef*>(&languageService.getType(cur.value)) != nullptr;
                        if(isEnum){
                            if(expr->resolvedType.empty()) expr->resolvedType = cur.value;
                            expr->tokens.push_back("_" + cur.value + "_" + member.value);
                        } else {
                            // Object property access: emit as obj.prop
                            string propType = resolvePathType(cur.value + "." + member.value, func, body);
                            if(expr->resolvedType.empty() && !propType.empty()) expr->resolvedType = propType;
                            expr->tokens.push_back(cur.value + "." + member.value);
                        }
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
        else if(cur.is(eTokenType::oper)){
            if(!expr->resolvedType.empty()){
                classDef* cls = dynamic_cast<classDef*>(&languageService.getType(expr->resolvedType));
                if(cls != nullptr){
                    string opName = cur.value;
                    // Peek at RHS (single token; complex RHS like function calls not yet supported)
                    token rhs = getNext();
                    string rhsType, rhsText;
                    if(rhs.is(eTokenType::integer))         { rhsType="intliteral";    rhsText=rhs.value; }
                    else if(rhs.isString())                 { rhsType="stringliteral"; rhsText=rhs.value; }
                    else if(rhs.is(eTokenType::charLiteral)){ rhsType="charliteral";   rhsText="'"+rhs.value+"'"; }
                    else if(rhs.is(eTokenType::name)) {
                        rhsType=resolveIdentifierType(rhs.value,func,body);
                        rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value,func,body) : rhs.value;
                        if(rhsText.empty()){ rhsText=rhs.value; } // fallback; type error will catch it
                    }
                    else if(rhs.is(token::parenOpen)){
                        // Parenthesized RHS expression — parse fully so operator translation runs
                        expression* rhsExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
                        rhsText = "(" + rhsExpr->text() + ")";
                        // rhsType left empty — matchedOp lookup uses empty-rhsType fallback (always matches)
                    }
                    else if(rhs.is(eTokenType::directive)){
                        if(rhs.value.rfind("##", 0) == 0){
                            string verbName = rhs.value.substr(2);
                            parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly — the '##' prefix is emitted automatically by the verb type's operator ==.", verbName));
                        }
                        parsingError(format("Directive '{0}' is not valid in an expression.", rhs.value));
                    }

                    functionDef* matchedOp = nullptr;
                    if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* m){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        return opFn && opFn->name==opName && !opFn->params.empty() &&
                               (rhsType.empty() || opFn->params[0]->type.name==rhsType || opFn->params[0]->type.name=="var");
                    })) matchedOp = dynamic_cast<functionDef*>(m);

                    // Conversion fallback: if no exact operator match, check if LHS class has
                    // emitter <rhsType> operator(){} (a no-param conversion operator returning rhsType).
                    // If so, the RHS is I6-compatible with the LHS — emit raw lhsText op rhsText.
                    bool useRawFallback = false;
                    if(!matchedOp && !rhsType.empty()){
                        if(findMemberInHierarchy(cls, [&](typeMember* m){
                            auto* opFn = dynamic_cast<functionDef*>(m);
                            return opFn && opFn->name=="operator()" && opFn->params.empty() &&
                                   opFn->isEmitter && opFn->returnType.name==rhsType;
                        })) useRawFallback = true;
                    }

                    if(!matchedOp && !useRawFallback && !rhsType.empty())
                        parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'", opName, cls->name, rhsType));

                    if(matchedOp) expr->resolvedType = matchedOp->returnType.name;

                    i6Block* blk = (matchedOp && matchedOp->isEmitter) ? dynamic_cast<i6Block*>(matchedOp->body) : nullptr;
                    if(useRawFallback){
                        // Conversion fallback: LHS type has operator() returning rhsType, so the
                        // types are I6-compatible. Emit lhsText op rhsText verbatim.
                        string lhsText = expr->text();
                        expr->tokens.clear();
                        expr->tokens.push_back(lhsText + opName + rhsText);
                        // rhs already consumed; nothing to put back
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
                                prefetched = rhsNext; // not a call; put the peeked token back
                            }
                        }
                        // Inline emitter: substitute $self → LHS, param → RHS, replace token list
                        string lhsText = expr->text();
                        string b = processBglConditionals(blk->i6Body);
                        size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                        size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
                        b = replaceWord(b, "$self", lhsText);
                        b = replaceWord(b, matchedOp->params[0]->name, rhsText);
                        expr->tokens.clear();
                        expr->tokens.push_back(b);
                        // rhs (and its args if a call) consumed; prefetched set correctly above
                    } else {
                        // Non-emitter operator: push op verbatim, put RHS back for normal loop processing
                        expr->tokens.push_back(opName);
                        prefetched = rhs;
                    }
                } else {
                    expr->tokens.push_back(cur.value);
                }
            } else if(cur.value == "!"){
                // Prefix logical-not — look up operator! on the operand and inline the emitter
                token operand = getNext();
                bool handled = false;
                if(operand.is(eTokenType::name)){
                    string opType = resolveIdentifierType(operand.value, func, body);
                    string opText = (func != nullptr) ? qualifyIdentifier(operand.value, func, body) : operand.value;
                    if(opText.empty()) opText = operand.value;
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
                                handled = true;
                            }
                        }
                    }
                }
                if(!handled){
                    expr->tokens.push_back("!");
                    prefetched = operand;
                }
            } else {
                expr->tokens.push_back(cur.value);
            }
        }
        else if(cur.is(eTokenType::dictionaryWord)){
            if(expr->resolvedType.empty()) expr->resolvedType = "dictionaryword";
            // Replace apostrophes with ^ for I6 dictionary word encoding
            string w;
            for(char ch : cur.value) w += (ch == '\'') ? '^' : ch;
            string i6form = cur.isPlural ? ("'" + w + "/p'")
                                         : (w.size() == 1) ? ("'" + w + "//'") : ("'" + w + "'");
            expr->tokens.push_back(i6form);
        }
        else if(cur.is(eTokenType::charLiteral)){
            if(expr->resolvedType.empty()) expr->resolvedType = "charliteral";
            expr->tokens.push_back("'" + cur.value + "'");
        }
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

            functionDef* method = nullptr;
            functionDef* varFallback = nullptr;
            bool nameMatch = false;
            for(typeMember* m : cls->members)
                if(auto* fd = dynamic_cast<functionDef*>(m)){
                    if(fd->name == methName) nameMatch = true;
                    if(fd->name == methName && fd->params.size() == callArgs.size()){
                        bool usesVar = false, argsOk = true;
                        for(size_t i = 0; i < callArgs.size(); i++){
                            string at = callArgs[i]->resolvedType;
                            string pt = fd->params[i]->type.name;
                            if(pt == "var") usesVar = true;
                            else if(!at.empty() && !isTypeCompatible(at, pt)){ argsOk = false; break; }
                        }
                        if(argsOk){
                            if(usesVar){ if(!varFallback) varFallback = fd; }
                            else { method = fd; break; }
                        }
                    }
                }
            if(method == nullptr) method = varFallback;
            if(method == nullptr){
                if(!nameMatch)
                    parsingError(format("No method '{0}' defined on type '{1}'", methName, chainTypeName));
                else
                    parsingError(format("No method '{0}' on type '{1}' matches arguments", methName, chainTypeName));
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
                        b = replaceWord(b, method->params[i]->name, callArgs[i]->text());
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
        else if(cur.value == "?" && parenDepth == 0) {
            // Ternary expression: condition already accumulated in expr->tokens.
            // Lower to: if (cond) _bgl_temp = trueExpr; else _bgl_temp = falseExpr;
            // then replace this expression with just _bgl_temp.
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
            break;
        }
        else if(cur.is(eTokenType::directive)){
            // ##VerbName is not valid in Beguile expressions; the ## prefix is emitted automatically.
            if(cur.value.rfind("##", 0) == 0){
                string verbName = cur.value.substr(2);
                parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly — the '##' prefix is emitted automatically by the verb type's operator ==.", verbName));
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
    token nextToken=_nullToken;
    sourceLocation stmtLoc = tok.src.line > 0 ? tok.src : file.currentLocation();
    currentStatementSrc = stmtLoc;
    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    string stmtCastType; // set when statement begins with (TypeName) cast prefix

    switch(tok.chk()){
        case chk("for"): {
            file.getToken(token::parenOpen);

            // Peek at the first token inside '(' to disambiguate:
            //   dataType         → for(TYPE name in array)   [Form 2: inline declaration]
            //   identifier "in"  → for(name in array)         [Form 1: pre-declared variable]
            //   anything else    → for(init; cond; incr)      [C-style]
            bool isForIn = false;
            string elemVarName, elemVarType;

            token peek = file.peekToken();
            if(peek.isDataType()){
                token typeTok = file.getToken(eTokenType::dataType);
                token nameTok = file.getToken(eTokenType::identifier);
                if(file.peekToken().is("in")){
                    // Form 2: for(TYPE name in array) — for-in loop
                    isForIn = true;
                    elemVarName = nameTok.value;
                    elemVarType = typeTok.value;
                    // declare the element variable as a function local, but only if not already declared
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
                    // C-style for with typed init: for(TYPE name = init; cond; incr)
                    // Declare the loop variable as a local, but only if not already declared
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
                    // Build initText: strip the type, keep "name rest..."
                    forStatement& forStmt = *(new forStatement());
                    forStmt.src = stmtLoc;
                    string initText = nameTok.value;
                    token t = file.getToken();
                    while(t.isNot(token::endStatement)){
                        if(!initText.empty()) initText += " ";
                        initText += t.value;
                        t = file.getToken();
                    }
                    forStmt.initText = initText;
                    forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
                    string incrText;
                    t = file.getToken();
                    while(t.isNot(token::parenClose)){
                        if(!incrText.empty()) incrText += " ";
                        incrText += t.value;
                        t = file.getToken();
                    }
                    forStmt.incrementText = incrText;
                    forStmt.body = new statementBlock();
                    functionDef forCtx;
                    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
                    forCtx.body = forStmt.body;
                    token next = file.getToken();
                    if(next.is(token::braceOpen)){
                        openCompileContext(eCompileContext::codeBlock);
                        while(processNextStatement(forCtx) == false){}
                        closeCompileContext(eCompileContext::codeBlock);
                    } else {
                        processStatement(next, forCtx);
                    }
                    if(body != nullptr) body->statements.push_back(&forStmt);
                    return false;
                }
            } else if(peek.is(eTokenType::identifier)){
                token nameTok = file.getToken(eTokenType::identifier);
                if(file.peekToken().is("in")){
                    // Form 1: pre-declared variable
                    isForIn = true;
                    elemVarName = nameTok.value;
                    // look up the variable's type from params, body locals, then globals
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
                    // C-style for — nameTok was the first token of the init expression
                    forStatement& forStmt = *(new forStatement());
                    forStmt.src = stmtLoc;
                    string initText = nameTok.value;
                    token t = file.getToken();
                    while(t.isNot(token::endStatement)){
                        if(!initText.empty()) initText += " ";
                        initText += t.value;
                        t = file.getToken();
                    }
                    forStmt.initText = initText;
                    forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
                    string incrText;
                    t = file.getToken();
                    while(t.isNot(token::parenClose)){
                        if(!incrText.empty()) incrText += " ";
                        incrText += t.value;
                        t = file.getToken();
                    }
                    forStmt.incrementText = incrText;
                    forStmt.body = new statementBlock();
                    functionDef forCtx;
                    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
                    forCtx.body = forStmt.body;
                    token next = file.getToken();
                    if(next.is(token::braceOpen)){
                        openCompileContext(eCompileContext::codeBlock);
                        while(processNextStatement(forCtx) == false){}
                        closeCompileContext(eCompileContext::codeBlock);
                    } else {
                        processStatement(next, forCtx);
                    }
                    if(body != nullptr) body->statements.push_back(&forStmt);
                    return false;
                }
            }

            if(!isForIn){
                // C-style for — init starts with a non-identifier token (or empty)
                forStatement& forStmt = *(new forStatement());
                forStmt.src = stmtLoc;
                string initText;
                token t = file.getToken();
                while(t.isNot(token::endStatement)){
                    if(!initText.empty()) initText += " ";
                    initText += t.value;
                    t = file.getToken();
                }
                forStmt.initText = initText;
                forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
                string incrText;
                t = file.getToken();
                while(t.isNot(token::parenClose)){
                    if(!incrText.empty()) incrText += " ";
                    incrText += t.value;
                    t = file.getToken();
                }
                forStmt.incrementText = incrText;
                forStmt.body = new statementBlock();
                functionDef forCtx;
                if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
                forCtx.body = forStmt.body;
                token next = file.getToken();
                if(next.is(token::braceOpen)){
                    openCompileContext(eCompileContext::codeBlock);
                    while(processNextStatement(forCtx) == false){}
                    closeCompileContext(eCompileContext::codeBlock);
                } else {
                    processStatement(next, forCtx);
                }
                if(body != nullptr) body->statements.push_back(&forStmt);
                return false;
            }

            // for-in shared code (reached from Form 1 and Form 2)
            file.getToken("in");

            // RHS may be a bare array name or a call expression (e.g. bglWorld.getFiltered(...)).
            // Parse as a full expression, then try to resolve the text as a declared array name.
            // If found, use the name directly (existing path). If not, the RHS is an expression:
            // assign it to a synthesised temp var so the forIn template always receives a plain name.
            expression* arrExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
            string arrExprText = arrExpr ? arrExpr->text() : "";
            string arrName;
            string arrElemType = "";

            // Attempt to resolve arrExprText as a declared array name
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                        if(ad->name == arrExprText){ arrElemType = ad->elementType; arrName = arrExprText; break; }
            if(arrName.empty())
                for(typeDef* g : languageService.globals)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(g))
                        if(ad->name == arrExprText){ arrElemType = ad->elementType; arrName = arrExprText; break; }
            if(arrName.empty() && func != nullptr)
                for(paramDef* p : func->params)
                    if(p->name == arrExprText){
                        string tn = p->type.name;
                        arrElemType = (tn.size() > 6 && tn.substr(0,6) == "array<") ? tn.substr(6, tn.size()-7) : "var";
                        arrName = arrExprText;
                        break;
                    }

            if(arrName.empty()){
                // RHS is a call expression — assign to a synthesised temp var.
                // Element type cannot be statically inferred; 'var' acts as wildcard.
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

            // type check: loop var must match element type.
            // 'var' on either side is a wildcard — element type unknown (expression source)
            // or loop variable intentionally untyped.
            if(elemVarType != arrElemType && elemVarType != "var" && arrElemType != "var")
                parsingError(format("'for in': variable '{0}' has type '{1}' but '{2}' has element type '{3}'",
                    elemVarName, elemVarType, arrName, arrElemType));

            // generate a unique index counter and declare it as a function local
            string counterName = format("_bglfi{0}", forInCounter++);
            variableDeclaration& counterDecl = *(new variableDeclaration());
            counterDecl.name = counterName;
            counterDecl.type = languageService.getType("var");
            if(body != nullptr) body->statements.push_back(&counterDecl);

            // build the forInStatement node and parse the body
            forInStatement& fi = *(new forInStatement());
            fi.src = stmtLoc;
            fi.elementVar = elemVarName;
            fi.arrayVar   = arrName;
            fi.counterVar = counterName;
            fi.body = new statementBlock();
            functionDef forCtx;
            if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
            // Make the element variable visible inside the loop body for type resolution.
            // (It is declared as a local in the outer body for I6 emission; adding it here
            // as a param lets resolveIdentifierType / qualifyIdentifier find it inside forCtx.)
            paramDef& elemParam = *(new paramDef());
            elemParam.name = elemVarName;
            elemParam.type = languageService.getType(elemVarType);
            forCtx.params.push_back(&elemParam);
            forCtx.body = fi.body;
            token next = file.getToken();
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock);
                while(processNextStatement(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatement(next, forCtx);
            }
            if(body != nullptr) body->statements.push_back(&fi);
            return false;
        }
        case chk("while"): {
            whileStatement& whileStmt = *(new whileStatement());
            whileStmt.src = stmtLoc;
            file.getToken(token::parenOpen);
            whileStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
            whileStmt.body = new statementBlock();
            functionDef whileCtx;
            if(func != nullptr){
                whileCtx.returnType = func->returnType;
                whileCtx.params = func->params;
            }
            whileCtx.body = whileStmt.body;
            token next = file.getToken();
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock);
                while(processNextStatement(whileCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatement(next, whileCtx);
            }
            if(body != nullptr) body->statements.push_back(&whileStmt);
            return false;
        }
        case chk("do"): {
            doStatement& doStmt = *(new doStatement());
            doStmt.src = stmtLoc;
            doStmt.body = new statementBlock();
            functionDef doCtx;
            if(func != nullptr){
                doCtx.returnType = func->returnType;
                doCtx.params = func->params;
            }
            doCtx.body = doStmt.body;
            file.getToken(token::braceOpen);
            openCompileContext(eCompileContext::codeBlock);
            while(processNextStatement(doCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
            // expect 'while' or 'until'
            token keyword = file.getToken({eTokenType::identifier, eTokenType::dataType});
            if(keyword.is("while")) doStmt.isWhile = true;
            else if(!keyword.is("until")) parsingError(format("Expected 'while' or 'until' after do block, got '{0}'", keyword.value));
            file.getToken(token::parenOpen);
            doStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
            if(file.peekToken().is(token::endStatement)) file.getToken();
            if(body != nullptr) body->statements.push_back(&doStmt);
            return false;
        }
        case chk("objectloop"):
            return false;
            break;
        case chk("if"): {
            ifStatement& ifStmt = *(new ifStatement());
            ifStmt.src = stmtLoc;
            file.getToken(token::parenOpen);
            ifStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
            ifStmt.thenBlock = new statementBlock();
            // Use a temporary functionDef as context so processNextStatement adds to thenBlock
            functionDef thenCtx;
            if(func != nullptr){
                thenCtx.returnType = func->returnType;
                thenCtx.params = func->params; // inherit params so inner scope sees outer params
            }
            thenCtx.body = ifStmt.thenBlock;
            token next = file.getToken();
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock);
                while(processNextStatement(thenCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatement(next, thenCtx);
            }
            if(file.peekToken().is("else")){
                file.getToken(); // consume "else"
                ifStmt.elseBlock = new statementBlock();
                functionDef elseCtx;
                if(func != nullptr){
                    elseCtx.returnType = func->returnType;
                    elseCtx.params = func->params;
                }
                elseCtx.body = ifStmt.elseBlock;
                token elseNext = file.getToken();
                if(elseNext.is(token::braceOpen)){
                    openCompileContext(eCompileContext::codeBlock);
                    while(processNextStatement(elseCtx) == false){}
                    closeCompileContext(eCompileContext::codeBlock);
                } else {
                    processStatement(elseNext, elseCtx);
                }
            }
            if(body != nullptr) body->statements.push_back(&ifStmt);
            return false;
        }
        case chk("switch"): {
            switchStatement& swStmt = *(new switchStatement());
            swStmt.src = stmtLoc;
            file.getToken(token::parenOpen);
            swStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
            string conditionType = swStmt.condition->resolvedType;
            file.getToken(token::braceOpen);
            // parse cases until closing brace
            while(true){
                token t = file.getToken();
                if(t.is(token::braceClose)) break;
                switchCase& sc = *(new switchCase());
                if(t.is("default")){
                    // sc.values stays empty; consume the colon after "default"
                    file.getToken(":");
                } else {
                    t.assert("case", "Expected 'case' or 'default' inside switch.");
                    auto parseCaseValue = [&]() -> expression* {
                        expression* val = parseExpression(file.getToken(), {":", ","}, func, body);
                        // Type-check case value against switch condition type.
                        // verb case values are int-compatible (action constants are integer values).
                        if(!conditionType.empty() && !val->resolvedType.empty()
                           && !isTypeCompatible(val->resolvedType, conditionType)
                           && val->resolvedType != "verb")   // ##VerbName is always int-compatible
                            parsingError(format("Switch case type '{0}' does not match condition type '{1}'",
                                               val->resolvedType, conditionType));
                        return val;
                    };
                    sc.values.push_back(parseCaseValue());
                    while(sc.values.back()->terminator == ",")
                        sc.values.push_back(parseCaseValue());
                    // colon was already consumed by parseExpression as its terminator
                }
                sc.body = new statementBlock();
                functionDef caseCtx;
                if(func != nullptr){ caseCtx.returnType = func->returnType; caseCtx.params = func->params; }
                caseCtx.body = sc.body;
                // collect statements until next case/default/} (peek, don't consume)
                while(true){
                    token peek = file.peekToken();
                    if(peek.is(token::braceClose) || peek.is("case") || peek.is("default")) break;
                    token st = file.getToken();
                    // silently drop break statements
                    if(st.is("break")){ file.getToken(token::endStatement); continue; }
                    processStatement(st, caseCtx);
                }
                swStmt.cases.push_back(&sc);
            }
            if(body != nullptr) body->statements.push_back(&swStmt);
            return false;
        }
        case chk("break"): {
            file.getToken(token::endStatement);
            i6RawNode& brk = *(new i6RawNode());
            brk.text = "break;";
            if(body != nullptr) body->statements.push_back(&brk);
            return false;
        }
        case chk("continue"): {
            file.getToken(token::endStatement);
            i6RawNode& cont = *(new i6RawNode());
            cont.text = "continue;";
            if(body != nullptr) body->statements.push_back(&cont);
            return false;
        }
        case chk("rtrue"): {
            file.getToken(token::endStatement);
            if(func != nullptr && func->returnType.name == "void")
                parsingError(format("Cannot use 'rtrue' in void routine '{0}'", func->name));
            returnStatement& rt = *(new returnStatement());
            rt.src = stmtLoc;
            rt.returnExpression = "rtrue";
            if(body != nullptr) body->statements.push_back(&rt);
            return false;
        }
        case chk("rfalse"): {
            file.getToken(token::endStatement);
            if(func != nullptr && func->returnType.name == "void")
                parsingError(format("Cannot use 'rfalse' in void routine '{0}'", func->name));
            returnStatement& rf = *(new returnStatement());
            rf.src = stmtLoc;
            rf.returnExpression = "rfalse";
            if(body != nullptr) body->statements.push_back(&rf);
            return false;
        }
        case chk("return"):
            returnStatement& returnStmnt=*(new returnStatement());
            returnStmnt.src = stmtLoc;
            nextToken=file.getToken();
            if(nextToken.isNot(token::endStatement)) {
                if(func != nullptr && func->returnType.name == "void")
                    parsingError(format("Cannot return a value from void routine '{0}'", func->name));
                expression* retExpr = parseExpression(nextToken, {token::endStatement}, func, body);
                returnStmnt.returnExpression = retExpr ? retExpr->text() : "";
            }
            if(body != nullptr) body->statements.push_back(&returnStmnt);
            return false;
            break;
    }

    // Cast prefix: (TypeName)obj.method(args); — overrides type used for method dispatch
    if(tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
        stmtCastType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
        tok = file.getToken();  // the actual object identifier
    }

    // A declared object's name is both its type and its singleton instance reference.
    // Re-classify it as an identifier so dot-access and assignment work in statement context.
    if(tok.is(eTokenType::dataType))
        if(dynamic_cast<objectDef*>(&languageService.getType(tok.value)) != nullptr)
            tok.tokenType = eTokenType::identifier;

    // Prefix ++ / --
    if(tok.is(eTokenType::oper) && (tok.value == "++" || tok.value == "--")){
        token varName = file.getToken(eTokenType::identifier);
        string lhs = func != nullptr ? qualifyIdentifier(varName.value, func, body) : varName.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", varName.value));
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
            i6RawNode& node = *(new i6RawNode());
            node.text = tok.value + lhs + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
        }
        return false;
    }

    if(tok.is(eTokenType::identifier)==false) return parsingError(format("Unrecognized statement starting with token '{0}'", (string) tok));

    //make sure the identifier is complete, including any member access paths (chain all dots)
    token symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    while(symbol.is(token::period)) {
        tok.value += "." + file.getToken(eTokenType::identifier).value;
        symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    }

    //----------------------------------------------------------------------
    //We've encountered an identifier, which could be a variable assignment,
    //  subscript assignment, or function call.

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

        // Find 'set' method
        functionDef* setMethod = nullptr;
        for(typeMember* m : arrCls->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "set") { setMethod = fd; break; }
        if(setMethod == nullptr) parsingError(format("No 'set' method on array type '{0}'", arrType));

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
                auto replaceWord = [](string str, const string& from, const string& to) {
                    size_t p = 0;
                    while((p = str.find(from, p)) != string::npos) {
                        bool lOk = p==0 || !(isalnum(str[p-1]) || str[p-1]=='_' || str[p-1]=='$');
                        bool rOk = p+from.size()>=str.size() || !(isalnum(str[p+from.size()]) || str[p+from.size()]=='_');
                        if(lOk && rOk) { str.replace(p, from.size(), to); p += to.size(); }
                        else p += from.size();
                    }
                    return str;
                };
                b = replaceWord(b, "$self", selfValue);
                b = replaceWord(b, "$prop", propValue);
                for(size_t i = 0; i < setMethod->params.size() && i < callStmt.args.size(); i++)
                    b = replaceWord(b, setMethod->params[i]->name, callStmt.args[i]->text());
                callStmt.emitterBody = b;
            }
        if(body != nullptr) body->statements.push_back(&callStmt);
        return false;
    }

    if(symbol.is(token::assignment))  {
        assignmentStatement& assignExpr=*(new assignmentStatement());
        assignExpr.src = stmtLoc;
        string lhsOriginal = (string)tok;
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
            string ownerType = resolvePathType(ownerPath, func, body);
            if(!ownerType.empty()){
                classDef* ownerCls = dynamic_cast<classDef*>(&languageService.getType(ownerType));
                if(ownerCls != nullptr)
                    for(typeMember* m : ownerCls->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == propName){ leftType = &vd->type; break; }
            }
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
                            if(vd->name == lhsOriginal){ leftType = &vd->type; break; }
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
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter
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
                        parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", valueTypeName, leftType->name));
                }
            }
        };

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
            return false;
        }

        assignExpr.assignedExpression = rhs;
        resolveEmitter(assignExpr, rhs);

        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&assignExpr);
        return false;
    }
    // Compound assignment: +=, -=, *=, /=, %=, |=, &=
    static const vector<string> compoundOps = {"+=","-=","*=","/=","%=","|=","&="};
    if(symbol.is(eTokenType::oper) && find(compoundOps.begin(), compoundOps.end(), symbol.value) != compoundOps.end()){
        string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
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
            // No emitter: expand to I6 form: x op= y  →  x = x op y;
            // (I6 does not support compound assignment operators)
            string op = symbol.value.substr(0, symbol.value.size() - 1); // strip trailing '='
            string rhsText = rhs != nullptr ? rhs->text() : "";
            i6RawNode& node = *(new i6RawNode());
            node.text = lhs + " = " + lhs + " " + op + " " + rhsText + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
        }
        return false;
    }
    if(symbol.is(eTokenType::oper) && (symbol.value == "++" || symbol.value == "--")){
        file.getToken(token::endStatement);
        string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
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
            i6RawNode& node = *(new i6RawNode());
            node.text = lhs + symbol.value + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
        }
        return false;
    }
    if(symbol.is(token::parenOpen))  { //then this is a function call.

        // Interpolated string: print($"...") or log($"...")
        // Detected by peeking: next token is '$', and the token after that is a quote.
        // We read the string character-by-character so that {expr} segments feed directly
        // into parseExpression() from the live file stream (correct tokenisation, lowercasing, etc.)
        string funcNameLower = tok.value;
        if((funcNameLower == "print" || funcNameLower == "log") &&
            file.peekToken(1).is("$") && file.peekToken(2).is(eTokenType::quote)) {

            bool isLog = (funcNameLower == "log");

            // For log(), skip entirely unless DEBUG is defined
            if(isLog && definedSymbols.count("debug") == 0) {
                file.getToken();                        // consume '$'
                file.getToken(eTokenType::quote);       // consume the string token
                file.getToken(token::parenClose);       // consume ')'
                file.getToken(token::endStatement);     // consume ';'
                return false;
            }

            file.getToken();                            // consume '$'
            // Instead of consuming the whole quote token (which already has escapes translated),
            // we consume just the opening '"' directly so we can read raw chars for the segments.
            file.readChar();  // opening '"'

            interpolatedPrintStatement& ps = *(new interpolatedPrintStatement());
            ps.src = stmtLoc;
            ps.isLog = isLog;

            string currentStr;
            char c = file.readChar();
            while(c != '"' && c != EOF) {
                if(c == '\\') {
                    char nc = file.readChar();
                    if     (nc == 'n')  currentStr += '^';      // \n  → I6 newline
                    else if(nc == '"')  currentStr += '~';      // \"  → I6 double-quote
                    else if(nc == '\\') currentStr += "@@92";   // \\  → literal backslash
                    else if(nc == '^')  currentStr += "@@94";   // \^  → literal caret
                    else if(nc == '~')  currentStr += "@@126";  // \~  → literal tilde
                    else if(nc == '@')  currentStr += "@@64";   // \@  → literal at-sign
                    else if(nc == '{')  currentStr += '{';       // \{  → literal brace (not an expression)
                    else { currentStr += '\\'; currentStr += nc; }
                } else if(c == '{') {
                    // Flush accumulated string segment
                    if(!currentStr.empty()) {
                        interpolatedPrintStatement::Segment seg;
                        seg.isExpr = false;
                        seg.text = currentStr;
                        ps.segments.push_back(seg);
                        currentStr = "";
                    }
                    // Parse the expression from the live stream until the matching '}'
                    token exprFirst = file.getToken();
                    expression* exprNode = parseExpression(exprFirst, {"}"}, func, body);
                    interpolatedPrintStatement::Segment seg;
                    seg.isExpr = true;
                    seg.expr = exprNode;
                    // Capture any ternary injections so they emit before this segment's print
                    seg.injections = pendingInjections;
                    pendingInjections.clear();
                    ps.segments.push_back(seg);
                } else {
                    currentStr += c;
                }
                c = file.readChar();
            }
            // Flush final string segment
            if(!currentStr.empty()) {
                interpolatedPrintStatement::Segment seg;
                seg.isExpr = false;
                seg.text = currentStr;
                ps.segments.push_back(seg);
            }

            file.getToken(token::parenClose);       // consume ')'
            file.getToken(token::endStatement);     // consume ';'
            if(body != nullptr) body->statements.push_back(&ps);
            return false;
        }

        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        // Qualify bare function name: if inside an instance and the name matches an instance
        // member method, prepend "self." so it routes to the method call path below.
        {
            string rawName = (string)tok;
            if(func != nullptr && rawName.find('.') == string::npos){
                string qualified = qualifyIdentifier(rawName, func, body);
                callStmt.functionName = qualified.empty() ? rawName : qualified;
            } else {
                callStmt.functionName = (string)tok;
            }
        }

        // parse argument list using parseExpression
        token firstArgTok = file.getToken();
        while(firstArgTok.isNot(token::parenClose)){
            expression* arg = parseExpression(firstArgTok, {token::comma, token::parenClose}, func, body);
            callStmt.args.push_back(arg);
            if(arg->terminator == token::parenClose) break;
            firstArgTok = file.getToken();
        }

        string chainReturnType;
        size_t dotPos = callStmt.functionName.rfind('.');  // use LAST dot for method name
        if(dotPos != string::npos){
            // method call: validate and resolve emitter
            string objectPath = callStmt.functionName.substr(0, dotPos);  // may be "obj" or "obj.prop"
            string methodName = callStmt.functionName.substr(dotPos + 1);
            string objectName = objectPath;  // kept for backward compat in non-emitter emit path
            string objectType = !stmtCastType.empty() ? stmtCastType : resolvePathType(objectPath, func, body);
            stmtCastType = "";  // consume the cast
            if(objectType.empty())
                parsingError(format("Unknown variable '{0}'", objectPath));
            // Compute $self and $prop for emitter substitution
            size_t innerDot = objectPath.rfind('.');
            string selfValue = (innerDot == string::npos) ? objectPath : objectPath.substr(0, innerDot);
            string propValue = (innerDot == string::npos)
                ? (objectType == "array" ? "0" : "<$prop undefined>")
                : objectPath.substr(innerDot + 1);
            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(objectType));
            if(cls == nullptr)
                parsingError(format("Type '{0}' is not a class", objectType));
            functionDef* method = nullptr;
            functionDef* varFallback = nullptr;
            functionDef* nameMatch = nullptr;
            functionDef* arityMatch = nullptr;
            // Walk the full class hierarchy for method lookup
            std::function<void(classDef*)> searchHierarchy = [&](classDef* c){
                for(typeMember* m : c->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == methodName){
                            if(nameMatch == nullptr) nameMatch = fd;
                            size_t req = 0;
                            for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                            if(callStmt.args.size() >= req && callStmt.args.size() <= fd->params.size()){
                                if(arityMatch == nullptr) arityMatch = fd;
                                bool argsOk = true;
                                bool usesVar = false;
                                for(size_t i = 0; i < callStmt.args.size() && argsOk; i++){
                                    string argType = callStmt.args[i]->resolvedType;
                                    string paramType = fd->params[i]->type.name;
                                    if(paramType == "var") usesVar = true;
                                    else if(!argType.empty() && !isTypeCompatible(argType, paramType))
                                        argsOk = false;
                                }
                                if(argsOk){
                                    if(usesVar){ if(varFallback == nullptr) varFallback = fd; }
                                    else { method = fd; return; }
                                }
                            }
                        }
                if(method == nullptr)
                    for(classDef* base : c->baseClasses){ searchHierarchy(base); if(method) return; }
            };
            searchHierarchy(cls);
            if(method == nullptr) method = varFallback;
            // Also search the objectDef's own inline members (for plain object declarations)
            if(nameMatch == nullptr){
                string lowerObjPath = objectPath;
                transform(lowerObjPath.begin(), lowerObjPath.end(), lowerObjPath.begin(), ::tolower);
                for(typeDef* g : languageService.globals){
                    if(g->name != lowerObjPath) continue;
                    if(auto* od = dynamic_cast<objectDef*>(g)){
                        for(typeMember* m : od->members)
                            if(auto* fd = dynamic_cast<functionDef*>(m))
                                if(fd->name == methodName){
                                    if(nameMatch == nullptr) nameMatch = fd;
                                    size_t req = 0;
                                    for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                                    if(callStmt.args.size() >= req && callStmt.args.size() <= fd->params.size()){
                                        if(arityMatch == nullptr) arityMatch = fd;
                                        bool argsOk = true, usesVar = false;
                                        for(size_t i = 0; i < callStmt.args.size() && argsOk; i++){
                                            string argType = callStmt.args[i]->resolvedType;
                                            string paramType = fd->params[i]->type.name;
                                            if(paramType == "var") usesVar = true;
                                            else if(!argType.empty() && !isTypeCompatible(argType, paramType))
                                                argsOk = false;
                                        }
                                        if(argsOk){
                                            if(usesVar){ if(varFallback == nullptr) varFallback = fd; }
                                            else { method = fd; }
                                        }
                                    }
                                }
                        break;
                    }
                }
                if(method == nullptr) method = varFallback;
            }
            if(nameMatch == nullptr)
                parsingError(format("No method '{0}' on type '{1}'", methodName, objectType));
            if(arityMatch == nullptr){
                size_t req = 0; for(paramDef* p : nameMatch->params) if(p->defaultValue.empty()) req++;
                size_t tot = nameMatch->params.size();
                if(req == tot)
                    parsingError(format("Method '{0}' on type '{1}' expects {2} argument(s), but {3} were supplied",
                        methodName, objectType, tot, callStmt.args.size()));
                else
                    parsingError(format("Method '{0}' on type '{1}' expects {2}-{3} argument(s), but {4} were supplied",
                        methodName, objectType, req, tot, callStmt.args.size()));
            }
            if(method == nullptr)
                parsingError(format("No overload of method '{0}' on type '{1}' accepts these argument types",
                    methodName, objectType));
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
            // global function call: find by name, enforce arity/types, resolve emitter
            // Resolution priority: exact type match > conversion match > var fallback
            functionDef* globalNameMatch = nullptr;
            functionDef* globalArityMatch = nullptr;
            functionDef* globalMatch = nullptr;
            functionDef* globalConversionMatch = nullptr;
            functionDef* globalVarFallback = nullptr;
            for(typeDef* g : languageService.globals){
                if(auto* fd = dynamic_cast<functionDef*>(g))
                    if(fd->name == callStmt.functionName){
                        if(globalNameMatch == nullptr) globalNameMatch = fd;
                        size_t req = 0;
                        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                        if(callStmt.args.size() >= req && callStmt.args.size() <= fd->params.size()){
                            if(globalArityMatch == nullptr) globalArityMatch = fd;
                            bool argsOk = true;
                            bool usesVar = false;
                            bool needsConversion = false;
                            for(size_t i = 0; i < callStmt.args.size() && argsOk; i++){
                                string argType = callStmt.args[i]->resolvedType;
                                string paramType = fd->params[i]->type.name;
                                if(paramType == "var") usesVar = true;
                                else if(argType.empty() || argType == paramType) {} // exact or unknown
                                else if(isTypeCompatible(argType, paramType)) needsConversion = true;
                                else argsOk = false;
                            }
                            if(argsOk){
                                if(usesVar){ if(globalVarFallback == nullptr) globalVarFallback = fd; }
                                else if(!needsConversion){ globalMatch = fd; break; }
                                else if(globalConversionMatch == nullptr) globalConversionMatch = fd;
                            }
                        }
                    }
            }
            if(globalMatch == nullptr) globalMatch = globalConversionMatch;
            if(globalMatch == nullptr) globalMatch = globalVarFallback;
            bool isFuncVar = false;
            if(globalNameMatch == nullptr) {
                // Check if it's a func<> variable (call by address)
                string varType = resolveIdentifierType(callStmt.functionName, func, body);
                if(varType.rfind("func<", 0) == 0) {
                    isFuncVar = true;
                    // Extract return type from "func<ReturnType,...>"
                    size_t lt = varType.find('<');
                    size_t sep = varType.find(',', lt);
                    size_t gt = varType.rfind('>');
                    chainReturnType = (sep != string::npos && sep < gt)
                        ? varType.substr(lt+1, sep-lt-1)
                        : varType.substr(lt+1, gt-lt-1);
                } else {
                    parsingError(format("Undeclared function '{0}'", callStmt.functionName));
                }
            }
            if(!isFuncVar){
                if(globalArityMatch == nullptr){
                    size_t req = 0; for(paramDef* p : globalNameMatch->params) if(p->defaultValue.empty()) req++;
                    size_t tot = globalNameMatch->params.size();
                    if(req == tot)
                        parsingError(format("Function '{0}' expects {1} argument(s), but {2} were supplied",
                            callStmt.functionName, tot, callStmt.args.size()));
                    else
                        parsingError(format("Function '{0}' expects {1}-{2} argument(s), but {3} were supplied",
                            callStmt.functionName, req, tot, callStmt.args.size()));
                }
                if(globalMatch == nullptr)
                    parsingError(format("No overload of function '{0}' accepts these argument types", callStmt.functionName));
                // fill in defaults for unspecified trailing arguments
                for(size_t i = callStmt.args.size(); i < globalMatch->params.size(); i++){
                    expression* defExpr = new expression();
                    defExpr->tokens.push_back(globalMatch->params[i]->defaultValue);
                    callStmt.args.push_back(defExpr);
                }
                // apply source-type conversion operators for mismatched arg types
                applyArgConversions(callStmt.args, globalMatch);
                if(globalMatch->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(globalMatch->body)){
                        callStmt.emitterBody = processBglConditionals(blk->i6Body);
                        for(paramDef* p : globalMatch->params) callStmt.emitterParams.push_back(p->name);
                    }
                chainReturnType = globalMatch->returnType.name;
            } // end if(!isFuncVar)
        }

        // method chaining: handle optional ".method()" suffixes before the final ";"
        auto replaceWordChain = [](string str, const string& from, const string& to) -> string {
            size_t pos=0;
            while((pos=str.find(from,pos))!=string::npos){
                bool lOk = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$');
                bool rOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
                if(lOk && rOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
                else pos+=from.size();
            }
            return str;
        };
        auto resolveEmitterText = [&](functionCallStatement& cs) -> string {
            string b = cs.emitterBody;
            for(size_t i=0; i<cs.emitterParams.size() && i<cs.args.size(); i++)
                b = replaceWordChain(b, cs.emitterParams[i], cs.args[i]->text());
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
            for(typeMember* m : chainCls->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == chainMethodName){
                        if(chainNameMatch == nullptr) chainNameMatch = fd;
                        size_t req=0; for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                        if(chainArgs.size() >= req && chainArgs.size() <= fd->params.size()){
                            chainMethod = fd; break;
                        }
                    }
            if(chainNameMatch == nullptr)
                parsingError(format("No method '{0}' on type '{1}'", chainMethodName, chainReturnType));
            if(chainMethod == nullptr)
                parsingError(format("Method '{0}' on type '{1}' has wrong arity for {2} argument(s)",
                    chainMethodName, chainReturnType, chainArgs.size()));
            if(!chainMethod->isEmitter || !dynamic_cast<i6Block*>(chainMethod->body))
                parsingError(format("Chained method '{0}' on type '{1}' is not an emitter", chainMethodName, chainReturnType));
            string selfText = resolveEmitterText(callStmt);
            i6Block* chainBlk = dynamic_cast<i6Block*>(chainMethod->body);
            string b = processBglConditionals(chainBlk->i6Body);
            b = replaceWordChain(b, "$self", selfText);
            for(size_t i=0; i<chainMethod->params.size() && i<chainArgs.size(); i++)
                b = replaceWordChain(b, chainMethod->params[i]->name, chainArgs[i]->text());
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
    arrDecl.type = languageService.getType("array");
    arrDecl.elementType = elementType;
    arrDecl.isExternal = isExternal;

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    if(symbol.is(token::bracketOpen)) {
        // array<T> name[N];
        token sizeTok = file.getToken(eTokenType::integer);
        arrDecl.arraySize = stoi(sizeTok.value);
        file.getToken(token::bracketClose);
        file.getToken(token::endStatement);
    } else if(symbol.is(token::assignment)) {
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
        arrDecl.declaredExpressionValue = list;
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

    // Disallow local variable names that shadow a global, a class member, or an object member
    if(func != nullptr){
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

            // type-check each element: determine expected element type from the class's single-param methods
            classDef* listClass = dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            string expectedElemType;
            if(listClass != nullptr){
                for(typeMember* m : listClass->members){
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
                }
            }
            for(size_t i = 0; i < list->elements.size(); i++){
                expression* elem = list->elements[i];
                if(elem->resolvedType.empty())
                    parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                else if(!expectedElemType.empty() && !isTypeCompatible(elem->resolvedType, expectedElemType))
                    parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
            }

            varDecl.declaredExpressionValue = list;
        } else {
            expression* rhs = parseExpression(first, {token::endStatement}, func, body);
            varDecl.declaredExpressionValue = rhs;

            //type check: if the declared type is a class, verify the assigned value is accepted by one of its operator= signatures
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr && rhs != nullptr){
                string valueTypeName = rhs->resolvedType;
                if(!valueTypeName.empty()){
                    // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all
                    bool found = isTypeCompatible(valueTypeName, (string)dataType);
                    if(!found) found = findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name==valueTypeName;
                    }) != nullptr;
                    if(!found) found = findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="var";
                    }) != nullptr;
                    if(!found){
                        // Fallback: check if RHS type has emitter DeclaredType operator(){}
                        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(valueTypeName));
                        if(rhsCls != nullptr)
                            if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter
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
                    if(!found) parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", valueTypeName, (string)dataType));
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
                auto replaceWord=[](string str, const string& from, const string& to){
                    size_t pos=0;
                    while((pos=str.find(from,pos))!=string::npos){
                        bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$');
                        bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
                        if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
                        else pos+=from.size();
                    }
                    return str;
                };
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
                auto replaceWord=[](string str, const string& from, const string& to){
                    size_t pos=0;
                    while((pos=str.find(from,pos))!=string::npos){
                        bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$');
                        bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
                        if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
                        else pos+=from.size();
                    }
                    return str;
                };
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

            if(dir == "ifdef" || dir == "ifndef"){
                string sym = readIdent();
                bool defined = definedSymbols.count(sym) > 0;
                bool condTrue = (dir == "ifdef") ? defined : !defined;
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
            token next=file.getToken();
            if(next.isString()){
                // quoted form: first check for a local .bgl file relative to the current source file.
                // If found, parse it as Beguile. Otherwise pass through as a raw I6 include directive.
                string includeName = next.value;
                // Strip surrounding quotes if present
                if(includeName.size() >= 2 && includeName.front()=='"' && includeName.back()=='"')
                    includeName = includeName.substr(1, includeName.size()-2);
                includeName = rewritePathSeps(includeName);
                filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
                filesystem::path candidate = curDir / (includeName + ".bgl");
                if(!filesystem::exists(candidate))
                    candidate = curDir / includeName;
                if(!filesystem::exists(candidate)){
                    for(const string& sp : beguilerSettings.bglIncludePaths){
                        filesystem::path c2 = filesystem::path(sp) / (includeName + ".bgl");
                        if(!filesystem::exists(c2)) c2 = filesystem::path(sp) / includeName;
                        if(filesystem::exists(c2)){ candidate = c2; break; }
                    }
                }
                if(filesystem::exists(candidate)){
                    parseFile(candidate.string());
                } else {
                    i6RawNode& node=*(new i6RawNode());
                    node.text=format("#include {0};", next.value);
                    languageService.globals.push_back(&node);
                }
            } else if(next.is("<")){
                // Read all tokens up to '>' to support sub-paths like <bindings/i6StandardLibrary>
                string includeName;
                token t = file.getToken();
                while(!t.is(">") && !t.is(eTokenType::eof)){
                    includeName += t.originalValue.empty() ? t.value : t.originalValue;
                    t = file.getToken();
                }
                filesystem::path libPath=findCaseInsensitive(settings.libPath, includeName+".bgl");
                parseFile(libPath.string());
            }
            return false;
            break;
        }    
        case chk("#once"):{
            // Register the current file so that any future #include of it is silently skipped.
            string curFile = filesystem::absolute(file.currentLocation().file).string();
            onceFiles.insert(curFile);
            return false;
            break;
        }
        case chk("#includei6"):{
            token filename=file.getToken({eTokenType::quote, eTokenType::rawQuote});
            // Normalize path separators inside the quoted filename
            string innerPath = filename.value;
            if(innerPath.size() >= 2 && innerPath.front()=='"' && innerPath.back()=='"')
                innerPath = '"' + rewritePathSeps(innerPath.substr(1, innerPath.size()-2)) + '"';
            string nodeText = format("#include {0};", innerPath);
            // Claim the pre-scan stub if present (preserves source ordering in output)
            bool claimed = false;
            for(typeDef* g : languageService.globals){
                if(auto* raw = dynamic_cast<i6RawNode*>(g)){
                    if(raw->isPrePassStub && raw->text == nodeText){
                        raw->isPrePassStub = false;
                        claimed = true;
                        break;
                    }
                }
            }
            if(!claimed){
                i6RawNode& node=*(new i6RawNode());
                node.text=nodeText;
                languageService.globals.push_back(&node);
            }
            return false;
            break;
        }    
        case chk("#i6"):{
            token t=file.getToken();
            i6RawNode& node=*(new i6RawNode());
            if(t.is(token::braceOpen)){ //multi line
                deque<char> closeChar;
                closeChar.push_back('}');

                t=file.getBasicToken(true);
                while(closeChar.size()>0){
                    if(t.value[0]==closeChar.back()){
                        closeChar.pop_back();
                        if(closeChar.size()==0) break;
                    }
                    node.text=node.text+t.value;

                    if(closeChar.back()!='\n') {  //if we are processing an "until end of line" span (e.g. an I6 comment), then do not push more special characters on to our stack (quotes would be treated the same; however, the lexer returns whole quoted strings, so this is covered for us automatically)
                        //push context onto our stack...
                        if(t.value[0]=='{') closeChar.push_back('}'); //a nested statement
                        if(t.value[0]=='!') closeChar.push_back('\n');//a comment
                    }
                    t=file.getBasicToken(true);
                }
            }
            else{ //single line only
                while(t.isNot("\n")){
                    node.text=node.text+t.value;
                    t=file.getBasicToken(true);
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
            // optional value on the same line (read raw so we see the newline)
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
    funcDef.name=(string) name;
    funcDef.src = file.currentLocation();
    funcDef.returnType=languageService.getType((string) returnType);
    funcDef.isExternal=isExternal;
    funcDef.isEmitter=isEmitter;

    processParameterList(funcDef);

    file.getToken(token::braceOpen); //consume the open brace

    if(isEmitter){
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=file.getRawTextThroughClosingBrace();
        funcDef.body=&rawblock;
    } else {
        funcDef.body=new statementBlock();
        functionDef* savedFunc = currentFunc;
        currentFunc = &funcDef;
        openCompileContext(eCompileContext::codeBlock);
        while(processNextStatement(funcDef)==false){
        }
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        if(funcDef.returnType.name != "void" && !hasReturn(dynamic_cast<statementBlock*>(funcDef.body)))
            parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    }

    if(isReplace){
        // Emitters: match by name + full parameter-type signature (overloading is possible).
        // Regular functions: match by name only (I6 has no overloading; only one routine per name).
        for(typeDef* g : languageService.globals){
            if(auto* existing = dynamic_cast<functionDef*>(g)){
                if(existing->name != funcDef.name) continue;
                if(isEmitter){
                    if(existing->params.size() != funcDef.params.size()) continue;
                    bool paramsMatch = true;
                    for(size_t i = 0; i < funcDef.params.size() && paramsMatch; i++)
                        if(funcDef.params[i]->type.name != existing->params[i]->type.name)
                            paramsMatch = false;
                    if(!paramsMatch) continue;
                }
                existing->body = funcDef.body;
                existing->isEmitter = funcDef.isEmitter;
                existing->returnType = funcDef.returnType;
                return false;
            }
        }
        parsingError(format("replace: no existing global function '{0}' found", funcDef.name));
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
            expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
            list->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        classDef* listClass = dynamic_cast<classDef*>(&languageService.getType(typeName));
        string expectedElemType;
        if(listClass != nullptr)
            for(typeMember* m : listClass->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
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
                expr->resolvedType, prop.name, typeName));
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
    obj.members.push_back((typeMember*)&arrDecl);
}

void bglParser::processMemberMethod(objectDef& obj, token returnType, token name){
    functionDef& funcDef = *(new functionDef());
    funcDef.name = (string)name;
    funcDef.returnType = languageService.getType((string)returnType);
    processParameterList(funcDef);
    file.getToken(token::braceOpen);
    funcDef.body = new statementBlock();
    functionDef* savedFunc = currentFunc;
    currentFunc = &funcDef;
    openCompileContext(eCompileContext::codeBlock);
    while(processNextStatement(funcDef) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    currentFunc = savedFunc;
    if(funcDef.returnType.name != "void" && !hasReturn(dynamic_cast<statementBlock*>(funcDef.body)))
        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    obj.members.push_back((typeMember*)&funcDef);
}

void bglParser::processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue){
    variableDeclaration& prop = *(new variableDeclaration());
    prop.name = name;
    prop.type = languageService.getType(typeName);
    if(hasValue) parsePropertyValue(prop, typeName);
    obj.members.push_back((typeMember*)&prop);
}

void bglParser::processTypedMember(objectDef& obj, token typeTok){
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
    token sym = file.getToken({token::assignment, token::endStatement, token::parenOpen});
    if(sym.is(token::parenOpen))
        processMemberMethod(obj, typeTok, propName);
    else
        processMemberVariable(obj, typeTok.value, propName.value, sym.is(token::assignment));
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
    bool hasValue = file.getToken({token::assignment, token::endStatement}).is(token::assignment);
    processMemberVariable(obj, propTypeName, nameTok.value, hasValue);
}

bool bglParser::processObjectDeclaration(token objectType, token name, bool isExternal, string className, string i6alias){
    objectDef& newObj = languageService.registerObject((string)name, isExternal);
    if(!i6alias.empty()) newObj.i6name = i6alias;
    // Resolve objectClass: prefer explicit ': ClassName' annotation, otherwise use the
    // declared type token (e.g. 'worldObject foyer { }' where worldObject is a classDef)
    string resolvedClassName = !className.empty() ? className : objectType.value;
    if(!resolvedClassName.empty() && resolvedClassName != "object"){
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(resolvedClassName)))
            newObj.objectClass = cls;
        else if(!className.empty())
            parsingError(format("Unknown class '{0}' in object declaration", className));
    }
    objectDef* savedObject = currentObject;
    currentObject = &newObj;
    openCompileContext(eCompileContext::objectDef);

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::directive)){
            if(tok.is("#i6")) processI6InlineMember(newObj);
            else parsingError(format("Unsupported directive in object body: '{0}'", tok.value));
        } else if(tok.value == "array")
            processArrayMember(newObj);
        else if(tok.isDataType())
            processTypedMember(newObj, tok);
        else if(tok.is(eTokenType::identifier))
            processInheritedMember(newObj, tok);
        else
            parsingError(format("Unexpected token '{0}' in object body", tok.value));
        tok = file.getToken();
    }

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;
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
        else if(key == "i6includepath"){ cfg.i6IncludePaths.push_back(rewritePathSeps(strVal)); }   // additive
        else if(key == "bglincludepath"){ cfg.bglIncludePaths.push_back(rewritePathSeps(strVal)); } // additive
        else if(key == "release"){ if(cfg.release == 0) cfg.release = stoi(strVal); }
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
    
    throw runtime_error(errorMessage); 
    return true; //won't ever actually run
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
    //if(oldScope==eCompileContext::languageBlock) compileLanguageStack.pop_front();
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
// Verb and grammar declarations
//===============================================================================================================================

bool bglParser::processVerbDeclaration(bool isExtern){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'verb' declarations are only allowed in global context");

    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});

    string origName = name.originalValue.empty() ? name.value : name.originalValue;
    verbObjectDef& vd = languageService.registerVerbObject(origName, isExtern);

    if(isExtern){
        file.getToken(token::endStatement);   // extern verb Take;
        return false;
    }

    file.getToken(token::braceOpen);
    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is("perform")){
            file.getToken(token::parenOpen);
            file.getToken(token::parenClose);
            file.getToken(token::braceOpen);

            functionDef& doFunc = *(new functionDef());
            doFunc.name = "perform";    // I6 object method; wrapper routine verbNameSub calls obj.perform()
            doFunc.src = file.currentLocation();
            doFunc.returnType = languageService.getType("void");
            doFunc.isExternal = false;
            doFunc.isEmitter = false;
            doFunc.body = new statementBlock();

            functionDef* savedFunc = currentFunc;
            currentFunc = &doFunc;
            openCompileContext(eCompileContext::codeBlock);
            while(processNextStatement(doFunc) == false){}
            closeCompileContext(eCompileContext::codeBlock);
            currentFunc = savedFunc;

            vd.doFunc = &doFunc;
            vd.members.push_back(&doFunc);
        } else if(tok.is("grammar")){
            vector<grammarLine> lines = parseGrammarLines();
            vd.grammarLines.insert(vd.grammarLines.end(), lines.begin(), lines.end());
        } else {
            parsingError(format("Unexpected token '{0}' in verb body (expected 'perform' or 'grammar')", tok.value));
        }
        tok = file.getToken();
    }
    return false;
}

bool bglParser::processGrammarDeclaration(){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'grammar' declarations are only allowed in global context");

    token name = file.getToken(eTokenType::identifier);
    grammarBlock& gb = *(new grammarBlock());
    gb.verbName = name.originalValue.empty() ? name.value : name.originalValue;
    gb.grammarLines = parseGrammarLines();
    languageService.globals.push_back(&gb);
    return false;
}

// Parse a grammar line list: { {.put, held, .on, noun}, {.hang, held, .on, noun} }
// First token of each line is the verb trigger word (must be a dictionaryWord).
// Remaining tokens are pattern tokens: dict words become I6 'word', identifiers stay as-is.
vector<grammarLine> bglParser::parseGrammarLines(){
    vector<grammarLine> result;

    auto escDictWord = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch; return e;
    };
    auto dictWordI6 = [&escDictWord](const string& w) -> string {
        string e = escDictWord(w);
        return (e.size() == 1) ? ("'" + e + "//'") : ("'" + e + "'");
    };

    file.getToken(token::braceOpen);   // outer {
    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        tok.assert(token::braceOpen, "Expected '{' to start a grammar line");

        grammarLine line;

        // First token: the verb trigger word — must be a singular dict word
        token trigger = file.getToken(eTokenType::dictionaryWord);
        line.verbWord = trigger.value;   // raw word, e.g. "put"

        // Remaining pattern tokens, comma-separated until }
        // Builds I6 form for a single dict word token
        auto makeI6Word = [&escDictWord](const token& t) -> string {
            string e = escDictWord(t.value);
            if(t.isPlural) return "'" + e + "/p'";
            if(e.size() == 1) return "'" + e + "//'";
            return "'" + e + "'";
        };

        tok = file.getToken({token::comma, token::braceClose});
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
        // tok is now }
        result.push_back(line);

        // After }, expect , (more lines) or } (end of list)
        tok = file.getToken({token::comma, token::braceClose});
        if(tok.is(token::comma))
            tok = file.getToken();   // either { for next line, or } for end
    }
    return result;
}

bglParser parser;