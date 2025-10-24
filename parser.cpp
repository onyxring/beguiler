#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>

#include <string_view>

#include "parser.h"
#include "fileReader.h"

using namespace std;

parser::parser(){ 
    //emit.to(cout);  //write the result to the terminal window for now
    emit.to(results.bodyText);
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
bool parser::processNextStatement(){
    processI6(); //we may be in the middle of emitting I6 code; do that until it is done.
    
    token tok=file.getToken({eTokenType::text, eTokenType::symbol, eTokenType::eof});

    if(tok.is(token::bracesClose)){
        closeCompileScope();
        return false;
    }

    if(resolveCurrentCompileScope()==eCompileScope::root){ //we are at the root of the source code heirarchy, where global declarations and class definitions all live    
    
        if(tok.is(eTokenType::eof)) return true; //end of file at global scope... exit

        //Statements which begin with a data type: 
        // int j;
        // int j=5;
        // int j(int a, int b){ print(a); print(b); return false;}
        if(tok.isDataType()){ 
            token name = file.getToken(eTokenType::text);
            token symbol = file.getToken(eTokenType::symbol);
            token val;
            switch(symbol.chk()){
                case chk(token::endStatement):
                    emit.globalVariable(tok, name); 
                    return false;
                    break;
                case chk(token::assignment):
                    val = file.getToken({eTokenType::text, eTokenType::quote}); 
                    file.getToken(token::endStatement);
                    emit.globalVariable(tok, name, val); 
                    return false;
                    break;
                case chk(token::parenOpen): 
                    emit.globalFunction(tok, name);
                    return false;
                    break;
            }
            parseError("Invalid character '"+symbol.text+"'.");
        }
        if(tok.isOneOf({"enum","bitFlags"})) {
            token name = file.getToken(eTokenType::text);
            token symbol = file.getToken(eTokenType::symbol);
            
            symbol.assert(token::bracesOpen);
            processEnumOrFlags(name, tok.is("bitFlags"));
            return false;
        }
        parseError("Invalid keyword '"+tok.text+"'.");
    } 

    if(resolveCurrentCompileScope()==eCompileScope::codeBlock){ //we are inside a code block, where executable commands live
        if(tok.is(token::bracesClose)) return true; //exiting the code block
        
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
        token symbol = file.getToken(eTokenType::symbol);

        //TODO: add inline variable declarations
        
        if(symbol.is(token::parenOpen))  
            processFunctionCall(tok);
        else{
            symbol.assert(".");
            
            token member = file.getToken(eTokenType::text);
            file.getToken(token::parenOpen);

            processFunctionCall(tok, member);
        }
    } 
    
    
    return false;
}
void parser::processI6(){
    if(getCurrentLanguage()==eCompileLanguage::beguile) return;
    
    char c=file.readChar();
    while(c!=EOF){
        if(c=='#'){
            token t=file.getToken(true);
            if(t.text=="beguile"){
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
void parser::processEnumOrFlags(token name, bool asFlag){
    int val=(asFlag)?1:0; 
    emit.out<<"object "<<name.text<<" with ";
    token tok = file.getToken({eTokenType::text, eTokenType::symbol});
    while(!tok.is(token::bracesClose)){
        tok.assert(eTokenType::text);
        emit.out<<tok.text<< " "<<val;
        if(asFlag) 
            val=val<<1; 
        else
            val++;
        tok = file.getToken(eTokenType::symbol).assertOneOf({token::comma,token::bracesClose});
        if(tok.is(token::comma)) {
            emit.out<<", ";
            tok=file.getToken(eTokenType::text);
        }
    }
    emit.out<<";"<<endl;
}
void parser::processFunctionCall(token obj, token member){
    string call=obj.text;
    if(!member.isNull()) call+="."+member.text;
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
        expression=expression+tok.text; //build the expression up from component expressions
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


//--------------------------------------------------------------------------------------------
void parser::parseError(string msg){
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
void parser::openCompileScope(eCompileScope newScope){
    compileScopeStack.push_front(newScope); 
}
void parser::closeCompileScope(){
    eCompileScope oldScope=compileScopeStack.front();
    compileScopeStack.pop_front();
    if(oldScope==eCompileScope::languageBlock) compileLanguageStack.pop_front();
}

