#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>

#include <string_view>

#include "parser.h"
#include "fileLexer.h"
#include "globals.h"

using namespace std;

parser::parser(){ 
    
    parseTree.type=eNodeType::root;
    pushCurrentNode(parseTree);
    
    //emit.to(cout);  //write the result to the terminal window for now
    registerNewObjectType("object"); 
    registerNewObjectType("class"); 
    registerNewObjectType("enum"); 
    registerNewObjectType("bnum"); 
    // objects.push_back("enum");
    // objects.push_back("bitFlags");
    // //objects.push_back("string"); //todo: this should move to an #insert language extension 
}


// Open an input file, process each statement.  This is the entry point to this whole parsing process
bool parser::parseFile(string filename){
    file.open(filesystem::absolute(filename).string());

    if(file.numOpen()==1){ //this is the first file, so let's do a little inspection and try to determine our default mode
        if(file.readChar()=='!' && file.readChar()=='%'){ //is the first line one of Inform's IGL declarations? 
            compileLanguageStack.push_front(eCompileLanguage::i6);        //if so, since IGL's are not legal in Beguile, we can assume Beguile is being used as a preprocessor for I6. 
            cout<<format("Detected IGL in first line of file {0}. Adopting preprocessor mode.\n",filename);
        }
        else{ 
            compileLanguageStack.push_front(eCompileLanguage::beguile); //use default mode
            cout<<format("Beguiling file {0}.\n",filename);
        }
        
        //now that we've checked for that convention, rewind the stream pointer to the beginning for actual processing.
        file.moveToStart(); 
        openCompileScope(eCompileScope::root);
    }
    //process all statements in the file one by one
    while(processNextStatement()==false){ 
        //cout<<"Block processed."<<endl;
    }
    emit.out<<endl;
    file.close();
    return false;
}
//------------------------------------------------------------------
// Process a complete statement. 
// We have a finite set of allowable patterns which may or may not
// be valid, depending on the Scope.  We match the input to these
// and raise compile-time errors if we cant.
/*bool parser::processNextStatement(){
    processI6(); //we may be in the middle of emitting I6 code; do that until it is done.
    
    token tok=file.getToken({eTokenType::identifier, eTokenType::directive, eTokenType::dataType, eTokenType::symbol, eTokenType::eof});
    
    if(tok.is(token::bracesClose)){
        closeCompileScope();
        return false;
    }

    if(resolveCurrentCompileScope()==eCompileScope::root){ //we are at the root of the source code heirarchy, where global declarations and class definitions all live    
    
        if(tok.is(eTokenType::eof)) return true; //end of file at global scope... exit

        token next;

        if(tok.is(eTokenType::directive)){
            switch(tok.chk()){
                case chk("#include"): 
                    next=file.getToken(eTokenType::quote);
                    emit.put(format("{0} {1};\n",tok.value, next.value));
                    return false;
            }
        }
        if(tok.isDataType()){ 
            token name = file.getToken(eTokenType::identifier);
            token symbol = file.getToken(eTokenType::symbol);
            token val;
            switch(symbol.chk()){
                case chk(token::endStatement):
                    emit.globalVariable(tok, name); 
                    return false;
                    break;
                case chk(token::assignment):
                    val = file.getToken({eTokenType::identifier, eTokenType::quote}); 
                    file.getToken(token::endStatement);
                    emit.globalVariable(tok, name, val); 
                    return false;
                    break;
                case chk(token::parenOpen): 
                    emit.globalFunction(tok, name);
                    return false;
                    break;
                case chk(token::bracesOpen): 
                    //processObjectDeclaration(tok, name);
                    return false;
                    break;
            }
            parseError("Invalid character '"+symbol.value+"'.");
        }
        if(tok.isOneOf({"enum","bitFlags"})) {
            token name = file.getToken(eTokenType::identifier);
            token symbol = file.getToken(eTokenType::symbol);
            
            symbol.assert(token::bracesOpen);
            processEnumOrFlags(name, tok.is("bitFlags"));
            return false;
        }
        parseError("Invalid keyword '"+tok.value+"'.");
    } 

    if(resolveCurrentCompileScope()==eCompileScope::codeBlock){ //we are inside a code block, where executable commands live
        if(tok.is(token::bracesClose)) return true; //exiting the code block
        emit.indent();
        
        if(tok.is("return")){
            tok.emit(); 
            token nextToken=file.getToken();
            if(nextToken.is(";")) {
                nextToken.emit(); //return;
            }
            else{
                emit.out<<" "; 
                nextToken.emit(); 
                file.getToken(";").emit(); //return val;
            }
            return false;
        }
        if(tok.is("print")){
            file.getToken("(");
            token nextToken=file.getToken();
            if(nextToken.is(")")) {
                file.getToken(";");
                emit.put("new_line;"); 
                emit.newLine();
                return false; 
            }
            if(nextToken.isOneOf({eTokenType::quote, eTokenType::identifier})) {
                file.getToken(")"); //TODO: expand this to support expressions 
                file.getToken(";");
                emit.put("print ");
                nextToken.emit();
                emit.endStatement(); 
                return false; 
            }
            parseError("Could evaluate print parameter '"+nextToken.value+"'.");
        }
        token symbol = file.getToken(eTokenType::symbol);

        //TODO: add inline variable declarations
        
        if(symbol.is(token::parenOpen))  
            processFunctionCall(tok);
        else{
            symbol.assert(".");
            
            token member = file.getToken(eTokenType::identifier);
            file.getToken(token::parenOpen);

            processFunctionCall(tok, member);
        }
    } 
    
    return false;
}
*/
//------------------------------------------------------------------
// Transcribe the current statement into our parseTree.
bool parser::processNextStatement(){
    token tok=file.getToken();
    
    if(tok.is(token::bracesClose)) return true; //true: signal to the calling routine that we've reached the end of the current scope
    if(tok.is(eTokenType::eof)) return true; //true: signal to the calling routine that we've reached the end of the file
    if(tok.is(eTokenType::directive)) return processDirective(tok);
    if(tok.is(token::constant) || tok.isDataType()) return processDataType(tok);      //handle "primitive", or "non-object", data types and constant declarations
    return processStatement(tok);//TODO: make sure statements aren't happening at illegal scopes
}

bool parser::processStatement(token tok){
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
            //assume it's a function call
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
    //return parseError(format("Unhandled token '{0}'",tok.value));
}
bool parser::processObjectType(token tok){
/*
    parseNode pNode; 
    pNode.type=eNodeType::objectDeclaration;
    pNode.keyToken=tok;

    token name = file.getToken(eTokenType::identifier);
    token symbol = file.getToken({token::bracesOpen,token::endStatement, token::assignment});
    
    if(symbol.value==token::endStatement) return false;     // object obj;
    if(symbol.value==token::assignment){                    // object s_to=library;
        parseNode el;
        el.keyToken=file.getToken(eTokenType::identifier);
        commitNode(pNode);
        file.getToken(token::endStatement);
    };
*/    
    //parseNode* saveNode=currentParseTreeNode;
    //currentParseTreeNode=&pNode;
    
    //TODO: these should go away when macros are implemented
    /*
    if(tok.value=="enum"|| tok.value=="bitFlags"){
        parseNode el;
        
        do{
            el.keyToken=file.getToken(eTokenType::identifier);
            pNode.addChild(el);
            symbol = file.getToken({token::comma,token::bracesClose});
        }while(symbol.is(token::comma));
    }
    // else{ */
    //     while(processNextStatement()==false){ 
    //         //cout<<"Block processed."<<endl;
    //     }
    // //}
    // //currentParseTreeNode=saveNode;
    // commitNode(pNode);

    return false;
}
bool parser::processDataType(token dataType){
    token name;
    token symbol;

    if(dataType.is(token::constant)){
        dataType = file.getToken(eTokenType::dataType);
        name = file.getToken(eTokenType::identifier);
        symbol = file.getToken(token::assignment);
        processConstantDeclaration(dataType, name, symbol);        
        file.getToken(token::endStatement);
        return false;
    }
    
    name = file.getToken(eTokenType::identifier);
    symbol = file.getToken(eTokenType::symbol);

    //--a variable declaration, with optional assignment:
    //      int myVar;
    //      int myVar=99; 
    if(symbol.isOneOf({token::endStatement, token::assignment})) {
        processVariableDeclaration(dataType, name, symbol);
        if(symbol.value==token::assignment) file.getToken(token::endStatement);
        return false;
    }
  
    // {
    //     pNode.type=eNodeType::variableDeclaration;
    //     pNode["dataType"]=tok;
    //     pNode["variableName"]=name;

    //      if(symbol.value==token::assignment){
    //         val = file.getToken({eTokenType::identifier, eTokenType::quote}); 
    //         file.getToken(token::endStatement);
    //         pNode["assignedValue"]=val;
    //     }
    //     commitNode(pNode);
    //     return false;
    // }
    
    //--a function declaration:
    if(symbol.is(token::parenOpen)) return processRoutineDeclaration(dataType, name);

    //--an object instance declaration:
    if(symbol.is(token::bracesOpen)) return processObjectDeclaration(dataType, name);

    return parseError("Unexpected value '"+symbol.value+"'.");
   
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
bool parser::processVariableDeclaration(token dataType, token variableName, token symbol){
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
bool parser::processConstantDeclaration(token dataType, token variableName, token symbol){
    parseNode pNode; 
    pNode.type=eNodeType::constantDeclaration;
    pNode["dataType"]=dataType;
    pNode["variableName"]=variableName;
    
    parseNode val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::integer}); 
    pNode["assignedValue"]=val;

    commitNode(pNode);
    return false;
}
void parser::pushCurrentNode(parseNode& node){
    currentNodeStack.push_front(&node);    
}
void parser::popCurrentNode(){
    currentNodeStack.pop_front();
}
parseNode& parser::getCurrentNode(){
    return *currentNodeStack.front();    
}
parseNode& parser::commitNode(parseNode& node){
    parseNode &retval=getCurrentNode();
    retval.addChild(node);
    retval.parent=&(getCurrentNode());
    return retval;
}
bool parser::processDirective(token tok){
    parseNode pNode; 
    pNode.type=eNodeType::directive;
    pNode.keyToken=tok;
    
    if(tok.value=="#include"){
        pNode["filename"]=file.getToken(eTokenType::quote);
        commitNode(pNode);
        return false;
    }
    return parseError("Unrecognized directive '"+tok.value+"'.");
}
bool parser::processRoutineDeclaration(token returnType, token name){
    parseNode pNode; 
    parseNode paramsNode; 

    pNode.type=eNodeType::routine;
    pNode["routineName"]=name;
    pNode["returnType"]=returnType;
    
    paramsNode.type=eNodeType::parameterListDeclaration; 
    
    //TODO: refactor currentParseTreeNode to allow an optional passed-in parseNode; or possibly a stack of parseNodes, so that we can build nested structures properly
    //parseNode* currentNodeSave=currentParseTreeNode;
    pushCurrentNode(paramsNode);
    
    token datatype = file.getToken(); 
    
    while(datatype.isNot(token::parenClose)){
        datatype.assertDataType();
        
        token variableName=file.getToken(eTokenType::identifier);
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
    
    file.getToken(token::bracesOpen); //consume the open brace
    
    pushCurrentNode(pNode);
    while(processNextStatement()==false){ 
        
    }   
    popCurrentNode(); //no longer saving to the routine node

    commitNode(pNode);
    return false;
}
bool parser::processObjectDeclaration(token objectType, token name){
    parseNode pNode; 
    
    pNode.type=eNodeType::objectDeclaration;
    pNode["objectName"]=name;
    pNode["objectType"]=objectType;
    
    pushCurrentNode(pNode); //save all statements within the object to this node
    
    while(processNextStatement()==false){ 
        
    }   
    popCurrentNode(); //restore to the objects parent (likely root)
     
    commitNode(pNode); //commit the object node to the parse tree
    return false;
}

    
//     if(tok.is(token::bracesClose)){
//         closeCompileScope();
//         return false;
//     }

//     if(resolveCurrentCompileScope()==eCompileScope::root){ //we are at the root of the source code heirarchy, where global declarations and class definitions all live    
    
//         if(tok.is(eTokenType::eof)) return true; //end of file at global scope... exit

//         token next;

//         if(tok.is(eTokenType::directive)){
//             switch(tok.chk()){
//                 case chk("#include"): 
//                     next=file.getToken(eTokenType::quote);
//                     emit.put(format("{0} {1};\n",tok.value, next.value));
//                     return false;
//             }
//         }
//         if(tok.isDataType()){ 
//             token name = file.getToken(eTokenType::identifier);
//             token symbol = file.getToken(eTokenType::symbol);
//             token val;
//             switch(symbol.chk()){
//                 case chk(token::endStatement):
//                     emit.globalVariable(tok, name); 
//                     return false;
//                     break;
//                 case chk(token::assignment):
//                     val = file.getToken({eTokenType::identifier, eTokenType::quote}); 
//                     file.getToken(token::endStatement);
//                     emit.globalVariable(tok, name, val); 
//                     return false;
//                     break;
//                 case chk(token::parenOpen): 
//                     emit.globalFunction(tok, name);
//                     return false;
//                     break;
//                 case chk(token::bracesOpen): 
//                     //processObjectDeclaration(tok, name);
//                     return false;
//                     break;
//             }
//             parseError("Invalid character '"+symbol.value+"'.");
//         }
//         if(tok.isOneOf({"enum","bnum"})) { //formerly "bitFlags"
//             token name = file.getToken(eTokenType::identifier);
//             token symbol = file.getToken(eTokenType::symbol);
            
//             symbol.assert(token::bracesOpen);
//             processEnumOrFlags(name, tok.is("bitFlags"));
//             return false;
//         }
//         parseError("Invalid keyword '"+tok.value+"'.");
//     } 

//     if(resolveCurrentCompileScope()==eCompileScope::codeBlock){ //we are inside a code block, where executable commands live
//         if(tok.is(token::bracesClose)) return true; //exiting the code block
//         emit.indent();
        
//         if(tok.is("return")){
//             tok.emit(); 
//             token nextToken=file.getToken();
//             if(nextToken.is(";")) {
//                 nextToken.emit(); //return;
//             }
//             else{
//                 emit.out<<" "; 
//                 nextToken.emit(); 
//                 file.getToken(";").emit(); //return val;
//             }
//             return false;
//         }
//         if(tok.is("print")){
//             file.getToken("(");
//             token nextToken=file.getToken();
//             if(nextToken.is(")")) {
//                 file.getToken(";");
//                 emit.put("new_line;"); 
//                 emit.newLine();
//                 return false; 
//             }
//             if(nextToken.isOneOf({eTokenType::quote, eTokenType::identifier})) {
//                 file.getToken(")"); //TODO: expand this to support expressions 
//                 file.getToken(";");
//                 emit.put("print ");
//                 nextToken.emit();
//                 emit.endStatement(); 
//                 return false; 
//             }
//             parseError("Could evaluate print parameter '"+nextToken.value+"'.");
//         }
//         token symbol = file.getToken(eTokenType::symbol);

//         //TODO: add inline variable declarations
        
//         if(symbol.is(token::parenOpen))  
//             processFunctionCall(tok);
//         else{
//             symbol.assert(".");
            
//             token member = file.getToken(eTokenType::identifier);
//             file.getToken(token::parenOpen);

//             processFunctionCall(tok, member);
//         }
//     } 
    
//     return false;
// }
void parser::processI6(){
    if(getCurrentLanguage()==eCompileLanguage::beguile) return;
    
    char c=file.readChar();
    while(c!=EOF){
        //TODO: Make this better; it isn't fully implemented; since it will be triggered by #beguile even within string, expressions, and comments.
        if(c=='#'){
            token t=file.getBasicToken(true); 
            if(t.value=="beguile"){
                file.getToken("{");
                compileLanguageStack.push_front(eCompileLanguage::beguile);
                openCompileScope(eCompileScope::languageBlock);
                return;
            }
        }
        emit.out<<c;
        c=file.readChar();
    }

}
void parser::registerNewObjectType(string name){
    if(find(objects.begin(), objects.end(), name)!=objects.end()) parseError(format("Declared type '{0}' already exists.", name));
    objects.push_back(name);
}
//TODO: this is temporary; it should walk the node tree to find a routine in scope
void parser::registerNewRoutine(string name){
    if(find(routines.begin(), routines.end(), name)!=routines.end()) parseError(format("Declared routine '{0}' already exists.", name));
    routines.push_back(name);
}
void parser::processEnumOrFlags(token name, bool asFlag){
    int val=(asFlag)?1:0; 
    
    registerNewObjectType(name.value);

    emit.out<<"object "<<name.value<<" with ";
    token tok = file.getToken({eTokenType::identifier, eTokenType::symbol});
    while(!tok.is(token::bracesClose)){
        tok.assert(eTokenType::identifier);
        emit.out<<tok.value<< " "<<val;
        if(asFlag) 
            val=val<<1; 
        else
            val++;
        tok = file.getToken(eTokenType::symbol).assertOneOf({token::comma,token::bracesClose});
        if(tok.is(token::comma)) {
            emit.out<<", ";
            tok=file.getToken(eTokenType::identifier);
        }
    }
    emit.out<<";"<<endl;
}
void parser::processFunctionCall(token obj, token member){
    string call=obj.value;
    if(!member.isNull()) call+="."+member.value;
    emit.out<<call<<"(";
    
    string paramString="";
    bool complete=false;

    do{
        string exp;
        complete=getArgumentExpression(exp);
        if(paramString.length()>0) paramString+=", ";
        paramString+=exp;
    }while(complete==false);
    
    file.getToken(token::endStatement);

    emit.out<<paramString<<");"<<endl;
    
}
//TODO: this is very rudimentary at the moment.  Expand this.
bool parser::getArgumentExpression(string& expression){
    token tok = file.getToken();

    while(tok.isNot(token::parenClose)){
        if(tok.is(token::comma)) return false; //process the expression
        expression=expression+tok.value; //build the expression up from component expressions
        tok = file.getToken();
    }
    return true; //done processing arguments
}
void parser::processFunctionBody(token returnType){
    file.getToken(token::bracesOpen);
    
    openCompileScope(eCompileScope::codeBlock);
    //process all statements until a close brace is encountered
    while(processNextStatement()==false){ 
        //cout<<"Block processed."<<endl;
    }
    //closeCompileScope(); //--scope is automatically popped off the stack by processNextStatement, when the closing brace is encountered.
    
    emit.out<<endl;
    
}
// void parser::processObjectDeclaration(token tok, string name){
//     vector<tuple<string, string, string>> properties;
    
//     openCompileScope(eCompileScope::classDef);
//     //process all statements until a close brace is encountered
//     while(processNextStatement()==false){ 
//         //cout<<"Block processed."<<endl;
//     }

// }


//--------------------------------------------------------------------------------------------
bool parser::parseError(string msg){
    string errorMessage;
    if(file.numOpen()>0) {
        auto [inputFileStream, fileName, curLine, curCol]=file.getDetail(); 
        //errorMessage=format("\033[1m{0}:{1}:{2}: \x1b[31merror:\x1b[0m {3}",fileName,curLine,curCol,msg); 
        errorMessage=format("{0}:{1}:{2}: error: {3}",fileName,curLine,curCol,msg); 
    }
    else{
        errorMessage=msg; 
    }
    
    throw runtime_error(errorMessage); 
    return true; //won't every actually run
}
eCompileLanguage parser::getCurrentLanguage(){
    return compileLanguageStack.front();
}
eCompileScope parser::resolveCurrentCompileScope(){ 
    for(int t=0; t<compileScopeStack.size();t++){
        if(compileScopeStack.at(t)!=eCompileScope::languageBlock) return compileScopeStack.at(t);
    }
    
    throw runtime_error("Internal Error: Unable to detect Scope.");
}
int parser::getScopeNestingDepth(){
    return compileScopeStack.size();
}
void parser::openCompileScope(eCompileScope newScope){
    compileScopeStack.push_front(newScope); 
}
void parser::closeCompileScope(){
    eCompileScope oldScope=compileScopeStack.front();
    compileScopeStack.pop_front();
    if(oldScope==eCompileScope::languageBlock) compileLanguageStack.pop_front();
}
