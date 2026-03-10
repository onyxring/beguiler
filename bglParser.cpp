#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>
#include <string_view>

#include "types.h"
#include "helpers.h"
#include "settings.h"
#include "typeDef.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"

using namespace std;

bglParser::bglParser(){ 
    openCompileContext(eCompileContext::global);
    //emit.to(cout);  //write the result to the terminal window for now
}

// Open an input file, process each statement.  This is the entry point to this whole parsing process
bool bglParser::parseFile(string filename){
    string absPath = filesystem::absolute(filename).string();
    // Duplicate include: error (future: #define guards will allow conditional re-inclusion)
    if(loadedFiles.count(absPath)){
        parsingError(format("File '{0}' has already been included.", filename));
        return false;
    }
    loadedFiles.insert(absPath);

    try{
        file.open(absPath);
    }
    catch(runtime_error& e){
        return parsingError(e.what());
        return true;
    }

    if(file.getNumberOfOpenFiles()==1){ //this is the first file, so let's do a little inspection and try to determine our default mode
        if(file.readChar()=='!' && file.readChar()=='%'){ //is the first line one of Inform's IGL declarations?
            compileLanguageStack.push_front(eCompileLanguage::i6);        //if so, since IGL's are not legal in Beguile, we can assume Beguile is being used as a preprocessor for I6.
            cout<<format("Detected IGL in first line of file {0}. Dawning the preprocessor mantle.\n",filename);
        }
        else{
            compileLanguageStack.push_front(eCompileLanguage::beguile); //use default mode
            cout<<format("Beguiling file \"{0}\"\n",filename);
        }

        //now that we've checked for that convention, rewind the stream pointer to the beginning for actual processing.
        file.moveToStart();

        // Always load the system library first so built-in types (eBool, eTarget, etc.) are available
        filesystem::path systemPath = filesystem::path(settings.libPath) / "system.bgl";
        parseFile(systemPath.string());
    }

    //process all statements in the file one by one.  This may include recursive calls for included files.
    while(processNextStatement()==false){
        //cout<<"Block processed."<<endl;
    }
    //emit.out<<endl;
    file.close();
    return false;
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
    token tok=file.getToken();
    sourceLocation stmtLoc = file.currentLocation();

    if(tok.is(token::braceClose)) return true;  //return true: signal to the calling routine that we've reached the end of the code/scope block.
    if(tok.is(eTokenType::eof)) return true;    //return true: same as above; end of file is just another form of end of code block.  TODO: this approach may need to be re-evaluated since an unclosed code block at the end of a file is a common error condition that we may want to report differently than a properly closed code block.

    //decide what to do with this token----------------------------------------------------------------------------
    if(tok.is(eTokenType::directive)) return processDirective(tok, contextObject); //handle preprocessor directives

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
    if(tok.isOneOf({token::enumDeclaration, token::bnumDeclaration})) return processEnumDeclaration(tok, isExtern);       //handle the enumeration types
    if(tok.is(token::classDeclaration)) return processClassDeclaration(tok, isExtern, isExtend);                          //handle type declarations

    if(tok.isDataType()) { //if the token is a datatype, then this is the start of either a variable declaration or a global routine declaration.  We need to look ahead a little bit to decide which one it is.
        // Handle array<T> generic type
        string arrayElementType;
        if(tok.value == "array") {
            file.getToken("<");
            arrayElementType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
        }
        token name=file.getToken(eTokenType::identifier);
        if(!arrayElementType.empty()) {
            token symbol = file.getToken({token::bracketOpen, token::assignment, token::endStatement});
            processArrayDeclaration(tok, name, arrayElementType, symbol, contextObject, isExtern);
            return false;
        }
        token symbol=file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});

        if(symbol.is(token::parenOpen))
            processRoutineDeclaration(tok, name, contextObject, isExtern, isEmitter);
        else if(symbol.is(token::braceOpen))
            processObjectDeclaration(tok, name, isExtern);
        else
            processVariableDeclaration(tok, name, symbol, contextObject, isExtern, isConst);
        return false;
    }

    if(tok.is("beguilersettings")) return processBeguilerSettings();

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
bool bglParser::processEnumDeclaration(token tok, bool isExternal){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration)) isBnum=true;
    token name=file.getToken(eTokenType::identifier); //enum name
    tok=file.getToken(token::braceOpen);  
    enumDef& newEnum=languageService.registerEnum((string)name, isExternal);
    int val=1;
    while(tok.isNot(token::braceClose)){
        enumValueDef& newVal=*new enumValueDef();
        newVal.name=file.getToken(eTokenType::identifier);
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

bool bglParser::processClassDeclaration(token tok, bool isExternal, bool isExtend){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Class declarations are only allowed in global context:'{0}'", (string) tok));

    token nameTok = file.getToken(isExtend ? eTokenType::dataType : eTokenType::identifier);
    classDef* classPtr = nullptr;
    if(isExtend){
        classPtr = dynamic_cast<classDef*>(&languageService.getType((string)nameTok));
        if(classPtr == nullptr) parsingError(format("extend class '{0}': no previously defined class with that name", (string)nameTok));
    } else {
        //create an empty class definition object and register it immediately, so that we can refer to this type within its own definition (e.g. comparison operators)
        classPtr = &languageService.registerClass((string)nameTok, isExternal);
    }
    classDef& newClass = *classPtr;

    classDef* savedClass = currentClass;
    currentClass = &newClass;
    openCompileContext(eCompileContext::objectDef);

    file.getToken(token::braceOpen);
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
        returnType=tok.assertDataType();
        name=file.getToken(eTokenType::identifier);
        if(name.is("operator")){
            isOperator=true;
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)){
                // conversion operator: emitter <type> operator()
                name.value = "operator()";
                tok = opTok; // the ( is already consumed; reuse it
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
            if(isExternal && !isEmitter){
                // extern class non-emitter: declaration only, no body allowed
                token next = file.getToken({token::endStatement, token::braceOpen});
                if(next.is(token::braceOpen))
                    parsingError(format("Non-emitter function '{0}' in extern class may not have a body", funcDef.name));
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
            newClass.members.push_back(&funcDef);
        }
        else{
            if(isOperator==true) parsingError("Operators must be functions.");
            if(isEmitter==true) parsingError("Emitters must be functions.");
            if(tok.isNot(token::endStatement) && tok.isNot(token::assignment))
                parsingError(format("Expected '=' or ';' after member '{0}'", (string)name));
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
bool bglParser::processParameterList(functionDef& funcDef){
    token tok=file.getToken(); // first type, or ")" for empty list
    while(tok.isNot(token::parenClose)){
        paramDef& param=*new paramDef();
        tok.assertDataType();
        param.type=languageService.getType((string) tok);
        tok=file.getToken(); // name, "=", ",", or ")"
        if(tok.is(eTokenType::identifier)){
            param.name=(string) tok;
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

std::string bglParser::resolveIdentifierType(std::string name, functionDef* func, statementBlock* body){
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
        }
    }
    return "";
}

// Returns the I6-qualified form of an identifier based on scope:
//   - found in local params/vars → name (unqualified)
//   - found in current object's members → "self.name"
//   - found in enum values or globals → name (unqualified)
//   - not found → "" (caller should report an error)
std::string bglParser::qualifyIdentifier(std::string name, functionDef* func, statementBlock* body){
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
    // Tier 2: current object/class member variables → qualify with self
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return "self." + name;
    if(currentClass != nullptr)
        for(typeMember* m : currentClass->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return "self." + name;
    // Tier 3: enum values and globals
    if(!languageService.getEnumType(name).empty()) return name;
    for(typeDef* g : languageService.globals)
        if(g->name == name) return name;
    return "";
}

bool bglParser::isTypeCompatible(std::string argType, std::string paramType){
    if(paramType == "var") return true;  // var accepts any type without checking
    if(argType == paramType) return true;
    // Check target type's operator = (argType)
    classDef* cls = dynamic_cast<classDef*>(&languageService.getType(paramType));
    if(cls != nullptr)
        for(typeMember* m : cls->members)
            if(auto* fn = dynamic_cast<functionDef*>(m))
                if(fn->name == "=" && !fn->params.empty() && fn->params[0]->type.name == argType)
                    return true;
    // Check source type's conversion operator: emitter <paramType> operator()
    classDef* argCls = dynamic_cast<classDef*>(&languageService.getType(argType));
    if(argCls != nullptr)
        for(typeMember* m : argCls->members)
            if(auto* fn = dynamic_cast<functionDef*>(m))
                if(fn->name == "operator()" && fn->params.empty() && fn->returnType.name == paramType)
                    return true;
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
        for(typeMember* m : argCls->members){
            if(auto* fn = dynamic_cast<functionDef*>(m))
                if(fn->name == "operator()" && fn->params.empty() && fn->isEmitter && fn->returnType.name == paramType)
                    if(auto* blk = dynamic_cast<i6Block*>(fn->body)){
                        string body = blk->i6Body;
                        string argText = args[i]->text();
                        size_t pos = 0;
                        while((pos = body.find("$self", pos)) != string::npos){
                            body.replace(pos, 5, argText);
                            pos += argText.size();
                        }
                        // Replace the arg expression tokens with the converted body
                        args[i]->tokens.clear();
                        args[i]->tokens.push_back(body);
                        args[i]->resolvedType = paramType;
                        break;
                    }
        }
    }
}

expression* bglParser::parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body){
    expression* expr = new expression();
    int parenDepth = 0;
    token cur = firstToken;
    optional<token> prefetched = nullopt;

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
        else if(cur.is(eTokenType::quote)){
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
            expr->tokens.push_back(cur.value);
        }
        else if(cur.is(eTokenType::identifier)){
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
                        string b = blk->i6Body;
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
                if(retType == "void")
                    parsingError(format("Cannot use void function '{0}' in an expression", cur.value));
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                expr->tokens.push_back(cur.value);
                expr->tokens.push_back(token::parenOpen);
                int callDepth = 1;
                token argTok = file.getToken();
                while(callDepth > 0){
                    if(argTok.is(token::parenOpen)) callDepth++;
                    else if(argTok.is(token::parenClose)){ callDepth--; if(callDepth==0) break; }
                    expr->tokens.push_back(argTok.value);
                    if(callDepth > 0) argTok = file.getToken();
                }
                expr->tokens.push_back(token::parenClose);
            }
            else if(parenDepth == 0 && next.is(token::period)){
                token member = file.getToken(eTokenType::identifier);
                if(cur.value == "self"){
                    // self.member → look up member type in current class/object
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
                    token afterMember = getNext();
                    if(afterMember.is(token::parenOpen)){
                        // method call in expression context: obj.method(args)
                        string objName = cur.value;
                        string methName = member.value;
                        string objType = resolveIdentifierType(objName, func, body);
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

                        // find best matching method
                        functionDef* method = nullptr;
                        functionDef* varFallback = nullptr;
                        for(typeMember* m : cls->members)
                            if(auto* fd = dynamic_cast<functionDef*>(m))
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
                        if(method == nullptr) method = varFallback;
                        if(method == nullptr) parsingError(format("No method '{0}' on type '{1}' matches arguments", methName, objType));

                        if(expr->resolvedType.empty()) expr->resolvedType = method->returnType.name;

                        // Compute $prop for array method calls in expression context
                        string exprPropValue = (objType == "array") ? "0" : "<$prop undefined>";

                        if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = blk->i6Body;
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
                                string b = blk->i6Body;
                                b = replaceWord(b, "$self", objName);
                                b = replaceWord(b, "$prop", propName);
                                if(!getMethod->params.empty())
                                    b = replaceWord(b, getMethod->params[0]->name, indexExpr->text());
                                expr->tokens.push_back(b);
                            }
                    } else {
                        // Qualified enum: TypeName.memberName → _TypeName_memberName in I6
                        prefetched = afterMember;
                        if(expr->resolvedType.empty()) expr->resolvedType = cur.value;
                        expr->tokens.push_back("_" + cur.value + "_" + member.value);
                    }
                }
            }
            else {
                prefetched = next;
                if(func != nullptr){
                    string qualified = qualifyIdentifier(cur.value, func, body);
                    if(qualified.empty())
                        parsingError(format("Undeclared identifier '{0}'", cur.value));
                    if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body);
                    expr->tokens.push_back(qualified);
                } else {
                    if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body);
                    expr->tokens.push_back(cur.value);
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
                    else if(rhs.is(eTokenType::quote))      { rhsType="stringliteral"; rhsText=rhs.value; }
                    else if(rhs.is(eTokenType::identifier)) {
                        rhsType=resolveIdentifierType(rhs.value,func,body);
                        rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value,func,body) : rhs.value;
                        if(func != nullptr && rhsText.empty()){ rhsText=rhs.value; } // fallback; type error will catch it
                    }

                    functionDef* matchedOp = nullptr;
                    for(typeMember* m : cls->members)
                        if(auto* opFn = dynamic_cast<functionDef*>(m))
                            if(opFn->name==opName && !opFn->params.empty() &&
                               (rhsType.empty() || opFn->params[0]->type.name==rhsType))
                                { matchedOp=opFn; break; }

                    // Conversion fallback: if no exact operator match, check if LHS class has
                    // emitter <rhsType> operator(){} (a no-param conversion operator returning rhsType).
                    // If so, the RHS is I6-compatible with the LHS — emit raw lhsText op rhsText.
                    bool useRawFallback = false;
                    if(!matchedOp && !rhsType.empty()){
                        for(typeMember* m : cls->members)
                            if(auto* opFn = dynamic_cast<functionDef*>(m))
                                if(opFn->name=="operator()" && opFn->params.empty() &&
                                   opFn->isEmitter && opFn->returnType.name==rhsType)
                                    { useRawFallback=true; break; }
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
                        if(rhs.is(eTokenType::identifier)){
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
                        string b = blk->i6Body;
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
            } else {
                expr->tokens.push_back(cur.value);
            }
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
    sourceLocation stmtLoc = file.currentLocation();
    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    switch(tok.chk()){
        case chk("for"): {
            forStatement& forStmt = *(new forStatement());
            forStmt.src = stmtLoc;
            file.getToken(token::parenOpen);

            // init: raw tokens until ;
            string initText;
            token t = file.getToken();
            while(t.isNot(token::endStatement)){
                if(!initText.empty()) initText += " ";
                initText += t.value;
                t = file.getToken();
            }
            forStmt.initText = initText;

            // condition: expression terminated by ;
            forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);

            // increment: raw tokens until )
            string incrText;
            t = file.getToken();
            while(t.isNot(token::parenClose)){
                if(!incrText.empty()) incrText += " ";
                incrText += t.value;
                t = file.getToken();
            }
            forStmt.incrementText = incrText;

            // body: braced block or single statement
            forStmt.body = new statementBlock();
            functionDef forCtx;
            if(func != nullptr){
                forCtx.returnType = func->returnType;
                forCtx.params = func->params;
            }
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
            file.getToken(token::braceOpen);
            // parse cases until closing brace
            while(true){
                token t = file.getToken();
                if(t.is(token::braceClose)) break;
                switchCase& sc = *(new switchCase());
                if(t.is("default")){
                    sc.value = nullptr;
                } else {
                    t.assert("case", "Expected 'case' or 'default' inside switch.");
                    sc.value = parseExpression(file.getToken(), {":"}, func, body);
                }
                file.getToken(":"); // consume the colon
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
        case chk("rtrue"):
            return false;
            break;
        case chk("rfalse"):
            return false;
            break;
        case chk("return"):
            returnStatement& returnStmnt=*(new returnStatement());
            returnStmnt.src = stmtLoc;
            nextToken=file.getToken();
            if(nextToken.isNot(token::endStatement)) {
                if(func != nullptr && func->returnType.name == "void")
                    parsingError(format("Cannot return a value from void routine '{0}'", func->name));
                if(func != nullptr && nextToken.is(eTokenType::identifier)){
                    string qualified = qualifyIdentifier(nextToken.value, func, body);
                    returnStmnt.returnExpression = qualified.empty() ? nextToken.value : qualified;
                } else {
                    returnStmnt.returnExpression = nextToken.value;
                }
                file.getToken(token::endStatement);
            }
            if(body != nullptr) body->statements.push_back(&returnStmnt);
            return false;
            break;
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
                string b = blk->i6Body;
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

        classDef* classType = leftType != nullptr ? dynamic_cast<classDef*>(&languageService.getType(leftType->name)) : nullptr;

        // helper: apply operator= emitter lookup to an assignment node for a given rhs expression
        auto resolveEmitter = [&](assignmentStatement& a, expression* val){
            if(classType != nullptr && val != nullptr){
                string valueTypeName = val->resolvedType;
                if(!valueTypeName.empty()){
                    bool found = false;
                    for(typeMember* member : classType->members)
                        if(auto* opFunc = dynamic_cast<functionDef*>(member))
                            if(opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1 && opFunc->params[0]->type.name==valueTypeName)
                                if(auto* blk = dynamic_cast<i6Block*>(opFunc->body)){
                                    a.emitterBody=blk->i6Body;
                                    a.emitterParam=opFunc->params[0]->name;
                                    found = true;
                                    break;
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

        if(body != nullptr) body->statements.push_back(&assignExpr);
        return false;
    }
    if(symbol.is(token::parenOpen))  { //then this is a function call.
        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        callStmt.functionName = (string)tok;

        // parse argument list using parseExpression
        token firstArgTok = file.getToken();
        while(firstArgTok.isNot(token::parenClose)){
            expression* arg = parseExpression(firstArgTok, {token::comma, token::parenClose}, func, body);
            callStmt.args.push_back(arg);
            if(arg->terminator == token::parenClose) break;
            firstArgTok = file.getToken();
        }
        file.getToken(token::endStatement);

        size_t dotPos = callStmt.functionName.rfind('.');  // use LAST dot for method name
        if(dotPos != string::npos){
            // method call: validate and resolve emitter
            string objectPath = callStmt.functionName.substr(0, dotPos);  // may be "obj" or "obj.prop"
            string methodName = callStmt.functionName.substr(dotPos + 1);
            string objectName = objectPath;  // kept for backward compat in non-emitter emit path
            string objectType = resolvePathType(objectPath, func, body);
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
            for(typeMember* m : cls->members)
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
                                else { method = fd; break; }
                            }
                        }
                    }
            if(method == nullptr) method = varFallback;
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
                    string b = blk->i6Body;
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
        } else {
            // global function call: find by name, enforce arity/types, resolve emitter
            functionDef* globalNameMatch = nullptr;
            functionDef* globalArityMatch = nullptr;
            functionDef* globalMatch = nullptr;
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
                            for(size_t i = 0; i < callStmt.args.size() && argsOk; i++){
                                string argType = callStmt.args[i]->resolvedType;
                                string paramType = fd->params[i]->type.name;
                                if(paramType == "var") usesVar = true;
                                else if(!argType.empty() && !isTypeCompatible(argType, paramType))
                                    argsOk = false;
                            }
                            if(argsOk){
                                if(usesVar){ if(globalVarFallback == nullptr) globalVarFallback = fd; }
                                else { globalMatch = fd; break; }
                            }
                        }
                    }
            }
            if(globalMatch == nullptr) globalMatch = globalVarFallback;
            if(globalNameMatch == nullptr)
                parsingError(format("Undeclared function '{0}'", callStmt.functionName));
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
                    callStmt.emitterBody = blk->i6Body;
                    for(paramDef* p : globalMatch->params) callStmt.emitterParams.push_back(p->name);
                }
        }

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
    if(auto* cd = dynamic_cast<classDef*>(&td))
        for(typeMember* m : cd->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == tail) return vd->type.name;
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
bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol, abstractObject& contextObj, bool isExternal, bool isConst){
    variableDeclaration& varDecl = *new variableDeclaration();
    varDecl.src = file.currentLocation();
    varDecl.name=(string) variableName;
    varDecl.type=languageService.getType((string) dataType);
    varDecl.isExternal=isExternal;
    varDecl.isConst=isConst;

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;

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
                    bool found=false;
                    for(typeMember* member : classType->members){
                        functionDef* fn=dynamic_cast<functionDef*>(member);
                        if(fn != nullptr && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name==valueTypeName){
                            found=true; break;
                        }
                    }
                    if(!found) parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", valueTypeName, (string)dataType));
                }
            }
        }
    }

    if(body != nullptr)
        body->statements.push_back(&varDecl);
    else
        languageService.registerInstance(varDecl);

    // init / deinit: if the variable's type is a class with init/deinit emitters, inject them
    // For local variables: inject init as an i6RawNode and register deinit in func->cleanups.
    // For global variables: record init in languageService.globalInits for the synthesised initBeguile routine.
    if(!isConst && body != nullptr && func != nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0) continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = blk->i6Body;
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

    // Global scope: record init body in globalInits for initBeguile
    if(!isConst && body == nullptr && func == nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0 || fn->name != "init") continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = blk->i6Body;
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
            if(next.is(eTokenType::quote)){
                // quoted form: treat as an I6 include directive
                i6RawNode& node=*(new i6RawNode());
                node.text=format("#include {0};", next.value);
                languageService.globals.push_back(&node);
            } else if(next.is("<")){
                token name=file.getToken(eTokenType::name);
                file.getToken(">");
                filesystem::path libPath=filesystem::path(settings.libPath) / (name.value+".bgl");
                parseFile(libPath.string());
            }
            return false;
            break;
        }    
        case chk("#includei6"):{
            token filename=file.getToken(eTokenType::quote);
            i6RawNode& node=*(new i6RawNode());
            node.text=format("#include {0};", filename.value);
            languageService.globals.push_back(&node);
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
            if(body != nullptr)
                body->statements.push_back(dynamic_cast<statement*>(&node));
            else
                languageService.globals.push_back(&node);
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
    }
    return parsingError("Unrecognized directive '"+tok.value+"'.");
}
bool bglParser::processRoutineDeclaration(token returnType, token name, abstractObject& contextObject, bool isExternal, bool isEmitter){
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
    }

    languageService.globals.push_back(&funcDef);
    return false;
}

bool bglParser::processObjectDeclaration(token objectType, token name, bool isExternal){
    objectDef& newObj = languageService.registerObject((string)name, isExternal);
    objectDef* savedObject = currentObject;
    currentObject = &newObj;
    openCompileContext(eCompileContext::objectDef);

    token tok = file.getToken(); // already consumed '{' before this call
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::directive)){
            if(tok.is("#i6")){
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
                newObj.members.push_back((typeMember*)&node);
            } else {
                parsingError(format("Unsupported directive in object body: '{0}'", tok.value));
            }
        } else if(tok.isDataType()){
            // Handle array<T> property declarations
            if(tok.value == "array") {
                file.getToken("<");
                string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
                file.getToken(">");
                token propName = file.getToken(eTokenType::identifier);
                token sym = file.getToken({token::bracketOpen, token::assignment, token::endStatement});
                arrayDeclaration& arrDecl = *(new arrayDeclaration());
                arrDecl.name = (string)propName;
                arrDecl.type = languageService.getType("array");
                arrDecl.elementType = elemType;
                if(sym.is(token::bracketOpen)) {
                    token sizeTok = file.getToken(eTokenType::integer);
                    arrDecl.arraySize = stoi(sizeTok.value);
                    file.getToken(token::bracketClose);
                    file.getToken(token::endStatement);
                } else if(sym.is(token::assignment)) {
                    file.getToken(token::braceOpen);
                    initializerList* list = new initializerList();
                    token t = file.getToken();
                    while(!t.is(token::braceClose) && !t.is(eTokenType::eof)) {
                        expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
                        list->elements.push_back(elem);
                        if(elem->terminator == token::braceClose) break;
                        t = file.getToken();
                    }
                    if(file.peekToken().is(token::endStatement)) file.getToken();
                    arrDecl.declaredExpressionValue = list;
                }
                newObj.members.push_back((typeMember*)&arrDecl);
                tok = file.getToken();
                continue;
            }

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
            if(sym.is(token::parenOpen)){
                // method on object
                functionDef& funcDef = *(new functionDef());
                funcDef.name = (string)propName;
                funcDef.returnType = languageService.getType((string)tok);
                processParameterList(funcDef);
                file.getToken(token::braceOpen);
                funcDef.body = new statementBlock();
                functionDef* savedFunc = currentFunc;
                currentFunc = &funcDef;
                openCompileContext(eCompileContext::codeBlock);
                while(processNextStatement(funcDef) == false){}
                closeCompileContext(eCompileContext::codeBlock);
                currentFunc = savedFunc;
                newObj.members.push_back((typeMember*)&funcDef);
            } else if(sym.is(token::assignment)){
                variableDeclaration& prop = *(new variableDeclaration());
                prop.name = (string)propName;
                prop.type = languageService.getType((string)tok);
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
                    // type-check each element against the class's first single-param method
                    classDef* listClass = dynamic_cast<classDef*>(&languageService.getType((string)tok));
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
                    prop.declaredExpressionValue = parseExpression(first, {token::endStatement}, nullptr, nullptr);
                }
                newObj.members.push_back((typeMember*)&prop);
            } else {
                variableDeclaration& prop = *(new variableDeclaration());
                prop.name = (string)propName;
                prop.type = languageService.getType((string)tok);
                newObj.members.push_back((typeMember*)&prop);
            }
        } else {
            parsingError(format("Unexpected token '{0}' in object body", tok.value));
        }
        tok = file.getToken();
    }

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;
    return false;
}

bool bglParser::processBeguilerSettings(){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("beguilerSettings is only allowed in global context");

    beguilerSettingsDef& cfg = *(new beguilerSettingsDef());
    cfg.name = "beguilerSettings";

    file.getToken(token::braceOpen);
    token tok = file.getToken();
    while(!tok.is(token::braceClose) && !tok.is(eTokenType::eof)){
        // each entry: <type> <name> = <value> ;
        // type is accepted as any identifier (beguilerSettings is parsed before includes register types)
        if(!tok.is(eTokenType::identifier) && !tok.isDataType()){
            parsingError(format("Expected type in beguilerSettings, got '{0}'", tok.value));
        }
        token propName = file.getToken(eTokenType::identifier);
        file.getToken(token::assignment);
        token val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer});
        file.getToken(token::endStatement);

        string key = propName.value;
        string strVal = (val.is(eTokenType::quote)) ? val.unescape(val.value) : val.value;
        // quote token values include surrounding " characters — strip them
        if(val.is(eTokenType::quote) && strVal.size() >= 2 && strVal.front() == '"' && strVal.back() == '"')
            strVal = strVal.substr(1, strVal.size() - 2);

        if(key == "errorformat") cfg.errorFormat = strVal;
        else if(key == "beguilibpath")  cfg.beguiLibPath = strVal;
        else if(key == "informpath")    cfg.informBinaryPath = strVal;
        else if(key == "includepath") cfg.includePaths.push_back(strVal);
        else if(key == "release")     cfg.release = stoi(strVal);
        else if(key == "target"){
            if(languageService.getEnumType(strVal) != "etarget")
                parsingError(format("Invalid target '{0}'. Must be a value of eTarget (Glulx, Z3, Z5, or Z8).", strVal));
            cfg.target = strVal;
        }
        else parsingError(format("Unknown beguilerSettings property '{0}'", key));

        tok = file.getToken();
    }

    // Default target to Glulx with a warning if not specified
    if(cfg.target.empty()){
        cout << "warning: beguilerSettings has no target; defaulting to Glulx.\n";
        cfg.target = "glulx";
    }

    // Apply path overrides immediately so subsequent includes use them
    if(!cfg.beguiLibPath.empty())     settings.libPath = cfg.beguiLibPath;
    if(!cfg.informBinaryPath.empty()) settings.informPath = cfg.informBinaryPath;

    languageService.globals.push_back(&cfg);
    return false;
}
#pragma endregion

//-------------------------------------------------------------------------------------------------------------------------------
// Throw an error, formatting the output to point to the current line 
bool bglParser::parsingError(string msg){
    string errorMessage;
    if(file.getNumberOfOpenFiles()>0) {
        auto [inputFileStream, fileName, curLine, curCol]=file.getCurrentFileDetail(); 
        //errorMessage=format("\033[1m{0}:{1}:{2}: \x1b[31merror:\x1b[0m {3}",fileName,curLine,curCol,msg); 
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

bglParser parser;