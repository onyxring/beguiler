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
    
    //parseTree.type=eNodeType::root;
    //pushCurrentNode(parseTree);
    
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
// parse the next statement in the input file.  Return true if we've reached the end of the current code block, false otherwise.  If there is a parse error, throw an exception with the details.
bool bglParser::processNextStatement(){
    token tok=file.getToken(); 
    
    if(tok.is(token::braceClose)) return true;  //return true: signal to the calling routine that we've reached the end of the code/scope block.
    if(tok.is(eTokenType::eof)) return true;    //return true: same as above; end of file is just another form of end of code block.  TODO: this approach may need to be re-evaluated since an unclosed code block at the end of a file is a common error condition that we may want to report differently than a properly closed code block.  

    //decide what to do with this token...
    
    if(tok.is(eTokenType::directive)) return processDirective(tok);
    if(tok.isOneOf({token::enumDeclaration, token::bnumDeclaration})) return processEnumDeclaration(tok);       //handle the enumeration types
    if(tok.is(token::classDeclaration)) return processClassDeclaration(tok);                                    //handle type declarations
    
    if(tok.isDataType()) {
        token name=file.getToken(eTokenType::identifier);
        token symbol=file.getToken({token::assignment, token::parenOpen, token::endStatement});

        if(symbol.is(token::parenOpen)) return processRoutineDeclaration(tok,name);         
        return  processVariableDeclaration(tok, name, symbol); 
        
    }
    
    //that's all we allow in global context.  Throw an error otherwise...
    if(getCurrentCompileContext()==eCompileContext::global) parseError(format("Unrecognized identifier:'{0}'", (string) tok));
    
    //must be in the context of a code block at this point, so we are expecting an executable statement.
    processStatement(tok);
}

bool bglParser::processEnumDeclaration(token tok){
    if(getCurrentCompileContext()!=eCompileContext::global) parseError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
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
    if(getCurrentCompileContext()!=eCompileContext::global) parseError(format("Class declarations are only allowed in global context:'{0}'", (string) tok));
    tok=file.getToken(eTokenType::identifier); //class name
    
    openCompileContext(eCompileContext::objectDef);
    
    typeDef newType=languageService.addObjectType((string) tok);

    //parseNode pNode; 
    //pNode["className"]=tok;
    //pNode.type=eNodeType::classDeclaration;
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
    closeCompileContext(eCompileContext::objectDef);
    
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
    
    //parseNode pNode; 
    //pNode["statement"]=tok;
    //pNode.type=eNodeType::executableStatement;

    switch(tok.chk()){
        // case chk("print"):
        //     file.getToken(token::parenOpen);
        //     pNode["value"]=file.getToken({eTokenType::quote, eTokenType::identifier});
        //     file.getToken(token::parenClose);
        //     file.getToken(token::endStatement);
        //     commitNode(pNode);
        //     return false;
        //     break;
        
        // for
        // while
        // do
        // objectloop
        // if/else
        // return

        case chk("for"):
            return false;
            break;
        case chk("while"):
            return false;
            break;
        case chk("do"):
            return false;
            break;
        case chk("objectloop"):
            return false;
            break;
        case chk("if"):
            return false;
            break;
        case chk("rtrue"):
            return false;
            break;
        case chk("rfalse"):
            return false;
            break;
        case chk("return"):    
            returnStatement returnStmnt;
            nextToken=file.getToken(); 
            if(nextToken.isNot(token::endStatement)) {
                 returnStmnt.returnExpression=nextToken.value; //TODO: expand to support more complex return expressions
                 file.getToken(token::endStatement);
            }
            //todo: save this return statement to the parse tree, including any return value expression.  This will likely involve creating a new node in the parse tree for this statement, and saving the return value expression as a child node or series of nodes under that.
            return false;
            break;
    }

    if(tok.is(eTokenType::identifier)==false) return parseError(format("Unrecognized statement starting with token '{0}'", (string) tok));          
    
    //make sure the identifier is complete, including any member access paths
    token symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    if(symbol.is(token::period)) {
        tok.value=tok.value+symbol.value+file.getToken(eTokenType::identifier).value; //combine the tokens to make a single token with the full member access path.  This allows us to support member accesses in function calls, e.g. myObject.myFunction()
        symbol = file.getToken({eTokenType::symbol, eTokenType::oper}); 
    }
    
    //----------------------------------------------------------------------
    //We've encountered an indentifier, which could be a variable assignment
    //  or function call.
    if(symbol.is(token::assignment))  {
        //pNode.type=eNodeType::executableStatement;   
        assignmentStatement assignExpr;
        assignExpr.variableLeft=(string) tok;
        assignExpr.assignedExpression=file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer}).value; //TODO: expand to include expressions
        //todo commit
        file.getToken(token::endStatement);
        return false;
    }
    if(symbol.is(token::parenOpen))  { //then this is a function call. 
        // pNode.type=eNodeType::executableStatement;
        // pNode["functionName"]=tok;
        
        // token symbol = file.getToken(eTokenType::symbol);

        // if(symbol.is(token::parenOpen))  {
        //     //simple function call
        //     //processFunctionCall(tok);
        // }
        // else{
        //     symbol.assert(".");
            
        //     token member = file.getToken(eTokenType::identifier);
        //     pNode["memberName"]=member;
        //     file.getToken(token::parenOpen);

            //processFunctionCall(tok, member);
        return false;
    }
    return parseError(format("Unhandled token '{0}'",tok.value));
}

bool bglParser::processDataType(token dataType){
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
    variableDeclaration varDecl;
    
    varDecl.name=(string) variableName;
    varDecl.type=languageService.getType((string) dataType);

    if(symbol.value==token::assignment){
        varDecl.declaredExpressionValue=file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer}); //TODO: expand to include expressions        
        file.getToken(token::endStatement);
    }
    
    //TODO: add this to the current scope
    return false;
}
/*bool bglParser::processConstantDeclaration(token dataType, token variableName, token symbol){
    parseNode pNode; 
    pNode.type=eNodeType::constantDeclaration;
    pNode["dataType"]=dataType;
    pNode["variableName"]=variableName;
    
    parseNode val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer}); 
    pNode["assignedValue"]=val;

    commitNode(pNode);
    return false;
}*/
bool bglParser::processDirective(token directive){
    if(getCurrentCompileContext()!=eCompileContext::global) parseError(format("Directive are only allowed in global context:'{0}'", (string) directive));

    // parseNode pNode; 
    // pNode.type=eNodeType::directive;
    // pNode.keyToken=directive;

    token tok;
    

    switch(directive.chk()){
        case chk("#include"):
            //pNode["filename"]=file.getToken(eTokenType::quote);
            //commitNode(pNode);
            file.getToken(eTokenType::quote);
            file.getToken(token::endStatement);
            //TODO: do something with this
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
            //pNode["i6Content"]=tok;
            //commitNode(pNode);
            return false;
            break;
    }
    return parseError("Unrecognized directive '"+tok.value+"'.");
}
bool bglParser::processRoutineDeclaration(token returnType, token name){
    memberFunction funcDef;
    funcDef.name=(string) name;
    funcDef.returnType=languageService.getType((string) returnType);
    processParameterList(funcDef);
    
    file.getToken(token::braceOpen); //consume the open brace
    
    
    openCompileContext(eCompileContext::codeBlock);

    while(processNextStatement()==false){ 
        
    }
    closeCompileContext(eCompileContext::codeBlock);
    //TODO: save the function definition, including its body, to the current scope.  This will likely involve creating a new node in the parse tree for this function, and saving the function definition details to that node, including the body as a child node or series of nodes.
    return false;
}

bool bglParser::processObjectDeclaration(token objectType, token name){
    
    /*
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
    closeCompileContext(eCompileContext::objectDef);
    
    commitNode(pNode); //commit the object node to the parse tree
    */
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

// void bglParser::pushCurrentNode(parseNode& node){
//     currentNodeStack.push_front(&node);    
// }
// void bglParser::popCurrentNode(){
//     currentNodeStack.pop_front();
// }
// parseNode& bglParser::getCurrentNode(){
//     return *currentNodeStack.front();    
// }
/*parseNode& bglParser::commitNode(parseNode& node){
    parseNode &retval=getCurrentNode();
    retval.addChild(node);
    retval.parent=&(getCurrentNode());
    return retval;
}*/
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
string bglParser::compileContextToString(eCompileContext context){
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
    if(oldScope!=expectedScope) parseError(format("Internal Error: Attempting to close compile context '{0}' but current context is '{1}'.", compileContextToString(expectedScope), compileContextToString(oldScope)));
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