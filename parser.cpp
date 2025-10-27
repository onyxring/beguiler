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

using namespace std;

parser::parser():currentParseTreeNode(parseTree){ 
    //emit.to(cout);  //write the result to the terminal window for now
    emit.to(results.bodyText);
    objects.push_back("class");
    objects.push_back("object");
    objects.push_back("enum");
    objects.push_back("bitFlags");
    //objects.push_back("string"); //todo: this should move to an #insert language extension 
}

constexpr size_t chk(string_view str) {
    const long long p = 131;
    const long long m = 4294967291; // 2^32 - 5, largest 32 bit prime
    long long total = 0;
    long long current_multiplier = 1;
    for (int i = 0; str[i] != '\0'; ++i){
        total = (total + current_multiplier * str[i]) % m;
        current_multiplier = (current_multiplier * p) % m;
    }
    return total;
}

// Open an input file, process each statement.  This is the entry point to this whole parsing process
bool parser::parseFile(string filename){
    file.open(filename);

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
    while(serializeNextStatement()==false){ 
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
bool parser::processNextStatement(){
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

//------------------------------------------------------------------
// Serialize the current statement into our parseTree.
bool parser::serializeNextStatement(){
    token tok=file.getToken();
    
    if(tok.is(token::bracesClose)) return true; //signal to the calling routine that we've reached the end of the current scope
    
    if(tok.isObjectType()) return processObjectType(tok);
    if(tok.isDataType()) return processDataType(tok);
    if(tok.is(eTokenType::directive)) return processDirective(tok);
    return parseError(format("Unhandled token '{0}'",tok.value));
}

bool parser::processObjectType(token tok){
    
    token name = file.getToken(eTokenType::identifier);
    token symbol = file.getToken({token::bracesOpen,token::endStatement, token::assignment});
    
    parseNode pNode; 
    
    pNode.type=eNodeType::objectDeclaration;
    pNode.keyToken=tok;
    pNode.properties["objectName"]=name;
    currentParseTreeNode.children.push_back(pNode);

    if(symbol.value==token::endStatement) return false;     // object obj;
    if(symbol.value==token::assignment){                    // object s_to=library;
        parseNode el;
        el.keyToken=file.getToken(eTokenType::identifier);
        currentParseTreeNode.children.push_back(el);
        file.getToken(token::endStatement);
    };
    
    parseNode saveNode=currentParseTreeNode;
    currentParseTreeNode=pNode;
    if(tok.value=="enum"|| tok.value=="bitFlags"){
        parseNode el;
        
        do{
            el.keyToken=file.getToken(eTokenType::identifier);
            currentParseTreeNode.children.push_back(el);
            symbol = file.getToken({token::comma,token::bracesClose});
        }while(symbol.is(token::comma));
    }
    else{ 
        while(serializeNextStatement()==false){ 
            //cout<<"Block processed."<<endl;
        }
    }
    currentParseTreeNode=saveNode;
    return false;
}
bool parser::processDataType(token tok){
    token name = file.getToken(eTokenType::identifier);
    token symbol = file.getToken(eTokenType::symbol);
    token val;
    parseNode pNode; 

    //--a data element
    if(symbol.value==token::endStatement || symbol.value==token::assignment){
        pNode.type=eNodeType::variableDeclaration;
        pNode.keyToken=tok;
        pNode.properties["variableName"]=name;
        if(symbol.value==token::assignment){
            val = file.getToken({eTokenType::identifier, eTokenType::quote}); 
            file.getToken(token::endStatement);
            pNode.properties["assignedValue"]=val;
        }
        currentParseTreeNode.children.push_back(pNode);
        return false;
    }
    
    //--a function
    if(symbol.tokenType==token::parenOpen) return processFunction(tok, name);

    return parseError("Unexpected value '"+symbol.value+"'.");
   
}
bool parser::processDirective(token tok){
    parseNode pNode; 
    pNode.type=eNodeType::directive;
    pNode.keyToken=tok;
    
    if(tok.value=="#include"){
        pNode.properties["filename"]=file.getToken(eTokenType::quote);
        currentParseTreeNode.children.push_back(pNode);
        return false;
    }
    return parseError("Unexpected directive '"+tok.value+"'.");
}
bool parser::processRoutine(token returnType, token name){
    parseNode pNode; 
    parseNode paramsNode; 
    parseNode bodyNode; 

    pNode.type=eNodeType::routine;
    pNode.keyToken=name;
    pNode.properties["returnType"]=returnType;
    pNode.properties["parameters"]=paramsNode;
    pNode.properties["body"]=bodyNode;
    
    //string paramDefaultInit="";
    
    token datatype = file.getToken(); 
    if(datatype.isNot(token::parenClose)){
        while(true){
            datatype.assertDataType();
            
            parseNode param;
            param.properties["dataType"]=datatype;
            param.keyToken=file.getToken(eTokenType::identifier); 

            token symbol = file.getToken({token::parenClose, token::assignment, token::comma}); 
            if(symbol.is(token::parenClose)) break;
            
            if(symbol.is(token::assignment)){
                token val=file.getToken({eTokenType::integer, eTokenType::quote});  
                if(val.tokenType==eTokenType::quote) datatype.assertOneOf({"string", "var"}, "Illegal default definition: string value is incompatible with type '"+datatype.value+"'.");
                param.properties["defaultValue"]=val;
            }
            
            paramsNode.children.push_back(param);

            datatype = file.getToken(); 
        }
    }
    file.getToken(token::bracesOpen);
    //todo: process body here
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
//         if(tok.isOneOf({"enum","bitFlags"})) {
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

void parser::dumpTree(parseNode node){

}