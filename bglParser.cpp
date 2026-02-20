#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>
#include <string_view>

#include "globals.h"
#include "typeDef.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"

using namespace std;

/*#baseVal void;
#baseVal true;
#baseVal false;
*/

bglParser::bglParser(){ 
    //emit.to(cout);  //write the result to the terminal window for now
    
    parseTree.type=eNodeType::root;
    pushCurrentNode(parseTree);
    
    languageService.addObjectType("void");
    
    
    /*
    registerNewBaseDataType("var"); 
    registerNewBaseDataType("void"); 
    registerNewBaseDataType("bool"); 
    registerNewBaseDataType("int"); 
    registerNewBaseDataType("string"); 
    */
}

// Open an input file, process each statement.  This is the entry point to this whole parsing process
bool bglParser::parseFile(string filename){
    try{
        file.open(filesystem::absolute(filename).string());
    }
    catch(runtime_error& e){
        return parseError(e.what());
        return true;
    }

    if(file.getNumberOfOpenFiles()==1){ //this is the first file, so let's do a little inspection and try to determine our default mode
        if(file.readChar()=='!' && file.readChar()=='%'){ //is the first line one of Inform's IGL declarations? 
            compileLanguageStack.push_front(eCompileLanguage::i6);        //if so, since IGL's are not legal in Beguile, we can assume Beguile is being used as a preprocessor for I6. 
            cout<<format("Detected IGL in first line of file {0}. Adopting preprocessor mode.\n",filename);
        }
        else{ 
            compileLanguageStack.push_front(eCompileLanguage::beguile); //use default mode
            cout<<format("Beguiling file \"{0}\"\n",filename);
        }
        
        //now that we've checked for that convention, rewind the stream pointer to the beginning for actual processing.
        file.moveToStart(); 
        openCompileContext(eCompileContext::global);
    }
    
    //process all statements in the file one by one.  This may include recursive calls for included files.
    while(processNextStatement()==false){ 
        //cout<<"Block processed."<<endl;
    }
    emit.out<<endl;
    file.close();
    return false;
}

//------------------------------------------------------------------
// Transcribe the current statement into our parseTree.  This will be called recursively which the statement includes nested statements (e.g. function definitions, object definitions, etc.)
bool bglParser::processNextStatement(){
    token tok=file.getToken(); 
    
    if(tok.is(token::braceClose)) return true;  //return true: signal to the calling routine that we've reached the end of the code/scope block.
    if(tok.is(eTokenType::eof)) return true;    //return true: same as above; end of file is just another form of end of code block.  TODO: this approach may need to be re-evaluated since an unclosed code block at the end of a file is a common error condition that we may want to report differently than a properly closed code block.  

    //decide what to do with this token...
    if(tok.is(eTokenType::directive)) return processDirective(tok);
    if(tok.isOneOf({token::enumDeclaration, token::bnumDeclaration})) return processEnumDeclaration(tok);      //handle "primitive", or "non-object", data types and constant declarations
    if(tok.is(token::classDeclaration)) return processClassDeclaration(tok);      //handle "primitive", or "non-object", data types and constant declarations
    if(tok.is(token::constantDeclararion) || tok.isDataType()) return processDataType(tok);      //handle "primitive", or "non-object", data types and constant declarations
    

    //that's all we allow in global context.  Throw an error otherwise...
    if(getCurrentCompileContext()==eCompileContext::global) parseError(format("Unrecognized identifier:'{0}'", (string) tok));
    
    processStatement(tok);
}

bool bglParser::processEnumDeclaration(token tok){
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration)) isBnum=true;
    token name=file.getToken(eTokenType::identifier); //enum name
    tok=file.getToken(token::braceOpen);  
    enumDef newEnum;
    newEnum.name=(string) name;
    int val=1;
    while(tok.isNot(token::braceClose)){
        enumValueDef newVal;
        newVal.name=file.getToken(eTokenType::identifier); 
        newVal.value=val;
        if(isBnum)
            val<<=1;
        else
            val++;
        newEnum.namedValues.push_back(newVal);
        tok=file.getToken({token::braceClose, token::comma}); 
    }
    languageService.registerEnum(newEnum);
}
bool bglParser::processClassDeclaration(token tok){
    tok=file.getToken(eTokenType::identifier); //class name
    
    typeDef newType=languageService.addObjectType((string) tok);

    parseNode pNode; 
    pNode["className"]=tok;
    pNode.type=eNodeType::classDeclaration;
    file.getToken(token::braceOpen);
    tok=file.getToken();
    while(tok.isNot(token::braceClose)){
        bool isEmitter=false;
        bool isOperator=false;
        token returnType;
        token name;
        if(tok.is("emitter")) { 
            isEmitter=true; 
            tok=file.getToken();
        }
        returnType=tok.assertDataType(); 
        name=file.getToken(eTokenType::identifier); 
        if(name.is("operator")){
            isOperator=true;
            name = file.getToken(eTokenType::oper);                
        }
        tok=file.getToken(eTokenType::symbol);
        
        if(tok.is(token::parenOpen))  { //this is a function
            memberFunction funcDef;
            funcDef.name=(string) name;
            funcDef.returnType=languageService.getType((string) returnType);
            funcDef.isEmitter=isEmitter;
            newType.registerNewMember(funcDef);
            //now process parameters and body... 
            processParameterList(funcDef);
            file.getToken(token::braceOpen); //consume the open brace;
            if(funcDef.isEmitter){
                i6Block rawblock;
                rawblock.i6Body=file.getRawTextThroughClosingBrace();
                funcDef.body=rawblock;
            }
            else{
//TODO
            }
        }
        else{
            if(isOperator==true) parseError("Operators must be functions.");
            if(isEmitter==true) parseError("Emitters must be functions.");
            memberVariable varDef;
            varDef.name=(string) name;
            varDef.type=languageService.getType((string) returnType);

        }
        //TODO: make sure the name hasn't already been defined 
    
        
        
        tok=file.getToken();
    }   
    //if...
    //file.getToken("emitter");
    //token retval=file.getToken(eTokenType::dataType);

}
bool bglParser::processParameterList(memberFunction& funcDef){
    token tok=file.getToken(); //first token of the parameter listd
    while(tok.isNot(token::parenClose)){
        paramDef param;
        tok.assertDataType();
        param.type=languageService.getType((string) tok);
        tok=file.getToken(eTokenType::identifier);
        param.name=(string) tok;
        funcDef.params.push_back(param);
        //todo: handle default values for parameters here.  

        tok=file.getToken({token::comma, token::parenClose});
    }
}
bool bglParser::processStatement(token tok){
    token nextToken=_nullToken;    
    
    parseNode pNode; 
    pNode["statement"]=tok;
    pNode.type=eNodeType::executableStatement;

    switch(tok.chk()){
        case chk("print"):
            file.getToken(token::parenOpen);
            pNode["value"]=file.getToken({eTokenType::quote, eTokenType::identifier});
            file.getToken(token::parenClose);
            file.getToken(token::endStatement);
            commitNode(pNode);
            return false;
            break;
        case chk("return"):
    
            nextToken=file.getToken();
            //TODO: check return values here...
            if(nextToken.is(token::endStatement)) {
                //return;
            }
            else{
                pNode["returnValue"]=nextToken;
                file.getToken(token::endStatement);
            }
            commitNode(pNode);
            return false;
            break;
        default:
            if(tok.is(eTokenType::identifier)==false) return parseError(format("Unrecognized statement starting with token '{0}'", (string) tok));  
            
            token symbol = file.getToken(eTokenType::symbol);

            if(symbol.is(token::period)) {
                tok.value=tok.value+symbol.value+file.getToken(eTokenType::identifier).value; //combine the tokens to make a single token with the full member access path.  This allows us to support member accesses in function calls, e.g. myObject.myFunction()
                symbol = file.getToken(eTokenType::symbol);
            }

            if(symbol.is(token::assignment))  {
                pNode.type=eNodeType::executableStatement;   
            }
            if(symbol.is(token::parenOpen))  {
        
            pNode.type=eNodeType::executableStatement;
            pNode["functionName"]=tok;
            
            token symbol = file.getToken(eTokenType::symbol);

            if(symbol.is(token::parenOpen))  {
                //simple function call
                //processFunctionCall(tok);
            }
            else{
                symbol.assert(".");
                
                token member = file.getToken(eTokenType::identifier);
                pNode["memberName"]=member;
                file.getToken(token::parenOpen);

                //processFunctionCall(tok, member);
            }
        }
    }
    //return parseError(format("Unhandled token '{0}'",tok.value));
}

bool bglParser::processDataType(token dataType){
    token name;
    token symbol;

    if(dataType.is(token::constantDeclararion)){
        dataType = file.getToken(eTokenType::dataType);
        name = file.getToken(eTokenType::identifier);
        symbol = file.getToken(token::assignment);
        processConstantDeclaration(dataType, name, symbol);        
        file.getToken(token::endStatement);
        return false;
    }
    
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
    if(symbol.is(token::parenOpen)) return processRoutineDeclaration(dataType, name);

    //--an object instance declaration:
    if(symbol.is(token::braceOpen)) return processObjectDeclaration(dataType, name);

    return parseError("Unexpected value '"+symbol.value+"'.");
   
}
// bool parser::processAttribute(token tok){

// }
//--process a variable declaration statement and add it to the parse tree
//-- handles both simple declarations and declarations with assignment
//-- also handles parameter declarations within function definitions
//-- e.g.:
//--      int myVar;
//--      int myVar=99;
//--      int myParam)
//--      int myParam=5) 
//--      int myParam=5, ...
bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol){
    parseNode pNode; 
    pNode.type=eNodeType::variableDeclaration;
    pNode["dataType"]=dataType;
    pNode["variableName"]=variableName;
    
    if(symbol.value==token::assignment){
        parseNode val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer}); 
        pNode["assignedValue"]=val;
    }
    
    commitNode(pNode);
    //note the token pointer may end up at different places depending on if an assignment was present or not.
    //  the calling process, then must determine, based on if as assignment was declared, what to do with the next token, based
    //  on context.  For example, if this was a parameter declaration, the next token may be either a comma or a parenClose
    //  if this was a global variable declaration, the next token should be an endStatement.
    return false;
}
bool bglParser::processConstantDeclaration(token dataType, token variableName, token symbol){
    parseNode pNode; 
    pNode.type=eNodeType::constantDeclaration;
    pNode["dataType"]=dataType;
    pNode["variableName"]=variableName;
    
    parseNode val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer}); 
    pNode["assignedValue"]=val;

    commitNode(pNode);
    return false;
}
bool bglParser::processDirective(token directive){
    parseNode pNode; 
    pNode.type=eNodeType::directive;
    pNode.keyToken=directive;

    token tok;
    

    switch(directive.chk()){
        case chk("#include"):
            pNode["filename"]=file.getToken(eTokenType::quote);
            commitNode(pNode);
            file.getToken(token::endStatement);
            return false;
            break;    
        case chk("#i6"):
            token t=file.getToken();
            if(t.is(token::braceOpen)){ //multi line
                deque<char> closeChar;
                closeChar.push_back('}');
                
                t=file.getBasicToken(true);
                while(closeChar.size()>0){
                    if(t.value[0]==closeChar.back()){
                        closeChar.pop_back();
                        if(closeChar.size()==0) break;
                    }
                    tok.value=tok.value+t.value;  //add the new token to the i6 text we are building up

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
                    tok.value=tok.value+t.value;
                    t=file.getBasicToken(true);
                }
            }
            tok.tokenType=eTokenType::unclassifiedText;
            pNode["i6Content"]=tok;
            commitNode(pNode);
            return false;
            break;
    }
    return parseError("Unrecognized directive '"+tok.value+"'.");
}
bool bglParser::processRoutineDeclaration(token returnType, token name){
    parseNode pNode; 
    parseNode paramsNode; 

    pNode.type=eNodeType::routine;
    pNode["routineName"]=name;
    pNode["returnType"]=returnType;
    
    paramsNode.type=eNodeType::parameterListDeclaration; 
    
    pushCurrentNode(paramsNode);
    
    token datatype = file.getToken(); 
    
    while(datatype.isNot(token::parenClose)){
        datatype.assertDataType();
        
        token variableName=file.getToken(eTokenType::identifier);
        if(variableName.is("operator")){
            token symbol= file.getToken(eTokenType::oper);    
        }
        token symbol= file.getToken({token::comma, token::parenClose, token::assignment});

        processVariableDeclaration(datatype, variableName, symbol);

        if(symbol.is(token::assignment)){
            datatype=file.getToken({token::comma, token::parenClose}); 
            if(datatype.is(token::comma)) datatype = file.getToken();
        }
        else{
            datatype=symbol;
        }
    }
    popCurrentNode(); //restore: no longer saving to the params node
    
    pNode["parameters"]=paramsNode;
    
    file.getToken(token::braceOpen); //consume the open brace
    
    pushCurrentNode(pNode);
    openCompileContext(eCompileContext::codeBlock);

    while(processNextStatement()==false){ 
        
    }
    closeCompileContext();
    popCurrentNode(); //no longer saving to the routine node

    commitNode(pNode);
    return false;
}

bool bglParser::processObjectDeclaration(token objectType, token name){
    parseNode pNode; 
    
    if(languageService.isObjectType((string)objectType)==false) parseError(format("Unrecognized object type '{0}'.", (string)objectType));

    openCompileContext(eCompileContext::objectDef);
    if(objectType.is("class")) languageService.addObjectType((string) name);    

    pNode.type=eNodeType::objectDeclaration;
    pNode["objectName"]=name;
    pNode["objectType"]=objectType; 
    
    pushCurrentNode(pNode); //save all statements within the object to this node
    
    while(processNextStatement()==false){ 
        
    }   
    popCurrentNode(); 
    closeCompileContext();
    
    commitNode(pNode); //commit the object node to the parse tree
    return false;
}

// bool parser::isObjectType(string name){
//     if(find(dataTypes.begin(), dataTypes.end(), name)!=dataTypes.end()) return true; 
//     return false;
// }
// bool parser::isRoutine(string name){
//     if(find(routines.begin(), routines.end(), name)!=routines.end()) return true; 
//     return false;
// }
// objTypeDef parser::registerNewObjectType(string name){
//     objTypeDef objType;
//     !if(languageService.isObjectType(name)) parseError(format("Declared type '{0}' already exists.", name));
//     objType.name=name;    
//     objects.push_back(objType);
//     return objType;
// }
// void parser::registerNewBaseDataType(string name){
//     if(isBaseDataType(name)) parseError(format("Declared type '{0}' already exists.", name));
//     dataTypes.push_back(name);
// }
//TODO: this is temporary; it should walk the node tree to find a routine in scope
// void parser::registerNewRoutine(string name){
//     if(isRoutine(name)) parseError(format("Declared routine '{0}' already exists.", name));
//     routines.push_back(name);
// }

bool bglParser::parseError(string msg){
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

void bglParser::pushCurrentNode(parseNode& node){
    currentNodeStack.push_front(&node);    
}
void bglParser::popCurrentNode(){
    currentNodeStack.pop_front();
}
parseNode& bglParser::getCurrentNode(){
    return *currentNodeStack.front();    
}
parseNode& bglParser::commitNode(parseNode& node){
    parseNode &retval=getCurrentNode();
    retval.addChild(node);
    retval.parent=&(getCurrentNode());
    return retval;
}
int bglParser::getScopeNestingDepth(){
    return compileContextStack.size();
}
//TODO:
// eCompileLanguage parser::getCurrentLanguage(){
//     return compileLanguageStack.front();
// }

void bglParser::openCompileContext(eCompileContext newScope){
    compileContextStack.push_front(newScope); 
}
void bglParser::closeCompileContext(){
    eCompileContext oldScope=compileContextStack.front();
    compileContextStack.pop_front();
    //if(oldScope==eCompileContext::languageBlock) compileLanguageStack.pop_front();
}
eCompileContext bglParser::getCurrentCompileContext(){ 
    return compileContextStack.front();
    /*for(int t=0; t<compileScopeStack.size();t++){
        if(compileScopeStack.at(t)!=eCompileContext::languageBlock) return compileScopeStack.at(t);
    }*/
    
    //throw runtime_error("Internal Error: Unable to detect Scope.");
}