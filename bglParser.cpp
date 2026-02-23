#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>
#include <string_view>

#include "types.h"
#include "globals.h"
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
    try{
        file.open(filesystem::absolute(filename).string());
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
    token tok=file.getToken(); 
    
    if(tok.is(token::braceClose)) return true;  //return true: signal to the calling routine that we've reached the end of the code/scope block.
    if(tok.is(eTokenType::eof)) return true;    //return true: same as above; end of file is just another form of end of code block.  TODO: this approach may need to be re-evaluated since an unclosed code block at the end of a file is a common error condition that we may want to report differently than a properly closed code block.  

    //decide what to do with this token...    
    if(tok.is(eTokenType::directive)) return processDirective(tok);                                             //handle preprocessor directives
    if(tok.isOneOf({token::enumDeclaration, token::bnumDeclaration})) return processEnumDeclaration(tok);       //handle the enumeration types
    if(tok.is(token::classDeclaration)) return processClassDeclaration(tok);                                    //handle type declarations
    
    if(tok.isDataType()) { //this is the start of either a variable declaration or a routine declaration, so we need to look ahead a little bit to decide which one it is.  
        token name=file.getToken(eTokenType::identifier);
        token symbol=file.getToken({token::assignment, token::parenOpen, token::endStatement});

        if(symbol.is(token::parenOpen)) 
            processRoutineDeclaration(tok,name, contextObject);         
        else
            processVariableDeclaration(tok, name, symbol, contextObject); 
        return false;
    }
    
    //that's all we allow in the global context.  Throw an error otherwise...
    if(getCurrentCompileContext()==eCompileContext::global) parsingError(format("Illegal global identifier:'{0}'", (string) tok));
    
    //must be in the context of a code block at this point, so we are expecting an executable statement.
    processStatement(tok, contextObject);
    return false;
}

//===============================================================================================================================
// Routines to parser larger blocks of the source code
//-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
#pragma region Parsing functions
bool bglParser::processEnumDeclaration(token tok){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
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
    languageService.registerType(newEnum);
    return false;
}

bool bglParser::processClassDeclaration(token tok){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Class declarations are only allowed in global context:'{0}'", (string) tok));
    
    //create an empty class definition object and register it immediately, so that we can refer to this type within its own definition (e.g. comparison operators)
    classDef tmp;
    tmp.name=(string)file.getToken(eTokenType::identifier); //class name
    classDef& newClass = languageService.registerType(tmp); //newClass is a reference to the actual registered class defintion, so we can fill it in.

    openCompileContext(eCompileContext::objectDef);

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
            functionDef funcDef;
            funcDef.name=(string) name;
            funcDef.returnType=languageService.getType((string) returnType);
            funcDef.isEmitter=isEmitter;
            //newType.registerNewMember(funcDef);
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
            if(isOperator==true) parsingError("Operators must be functions.");
            if(isEmitter==true) parsingError("Emitters must be functions.");
            variableDeclaration varDef;
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
    return false;    
}
bool bglParser::processParameterList(functionDef& funcDef){
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
    return false;
}
bool bglParser::processStatement(token tok, abstractObject& funcDef){
    token nextToken=_nullToken;    

    switch(tok.chk()){
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

    if(tok.is(eTokenType::identifier)==false) return parsingError(format("Unrecognized statement starting with token '{0}'", (string) tok));          
    
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
    return parsingError(format("Unhandled token '{0}'",tok.value));
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

    return parsingError("Unexpected value '"+symbol.value+"'.");
   
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
bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol,abstractObject& funcDef){
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
bool bglParser::processDirective(token directive){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Directive are only allowed in global context:'{0}'", (string) directive));

    token tok;
    switch(directive.chk()){
        case chk("#include"):
            file.getToken(eTokenType::quote);
            file.getToken(token::endStatement);
            //TODO: do something with this
            return false;
            break;    
        case chk("#includeI6"):
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
            return false;
            break;
    }
    return parsingError("Unrecognized directive '"+tok.value+"'.");
}
bool bglParser::processRoutineDeclaration(token returnType, token name, abstractObject& contextObject){
    functionDef funcDef;
    funcDef.name=(string) name;
    funcDef.returnType=languageService.getType((string) returnType);
    processParameterList(funcDef);
    
    file.getToken(token::braceOpen); //consume the open brace
    
    
    openCompileContext(eCompileContext::codeBlock);

    while(processNextStatement(funcDef)==false){ 
        
    }
    closeCompileContext(eCompileContext::codeBlock);
    //TODO: save the function definition, including its body, to the current scope.  This will likely involve creating a new node in the parse tree for this function, and saving the function definition details to that node, including the body as a child node or series of nodes.
    return false;
}

bool bglParser::processObjectDeclaration(token objectType, token name){
    
    /*
    parseTreeNode pNode; 
    
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