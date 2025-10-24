#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>

#include "fileReader.h"
#include "parser.h"
#include "orbit.h"

using namespace std;

void fileReader::open(string filename){
    ifstream& inputFileStream=*(new ifstream(filename));  
    
    if (!inputFileStream.is_open()) beguiler.parseError("Unable to open file "+filename+".");
    files.push(make_tuple(&inputFileStream, filename, 1, 0)); //take the current file and push it on the stack    
}
void fileReader::close(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top(); 
    inputFileStream->close();
    delete inputFileStream;
    files.pop();
}
int fileReader::numOpen(){
    return files.size();
}
void fileReader::moveToStart(){
    currentStream()->seekg(currentStream()->beg); 
}
//-- read file methods----------------------------------------------
ifstream* fileReader::currentStream(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top(); 
    return inputFileStream;   
}

// ignore all spaces before the next token
void fileReader::bleedSpaces(){
    char c=peekChar(); 
    while(c!=EOF && isspace(c)) { //this is just whitespace; discard it
        readChar(); 
        c=peekChar(); 
    }
}

char fileReader::peekChar(){
    return currentStream()->peek();
}
char fileReader::readChar(){
    int retval;
    auto&[inputFileStream, fileName, curLine, curCol]=files.top(); 

    retval=(char)(inputFileStream->get());
    if(retval=='\n'){
        curLine++;
        curCol=0;
    }

    curCol++;
    return retval;
}
// bool parser::isSymbolChar(char c){
//     string symbols="[](){}#.\";'";
//     if(symbols.find(c)==string::npos) return false;
//     return true;
// }
bool fileReader::isKeywordChar(char c){
    if(isalnum(c)) return true;
    if(c=='_') return true;
    return false;
}
tuple<ifstream*, string, int, int> fileReader::getDetail(){
    return files.top();
}
//scan the next characters in the file and return a basic token which fits one of three types:
// a single valid "symbol" character.  For example, parentheses, semicolon, braces, etc... Not all symbols are valid in the language.
// a string of characters made up of characters which are valid for keywords.  Note that this doesn't check for validity:  1abc is not a valid identifier; but will still be returned; as would 45.
// a quoted string of text, potentially containing any characters (except a quote, presently)
token fileReader::getToken(bool suppressBleed){
    token retval;
    
    if(!suppressBleed) bleedSpaces();
    char c=peekChar(); 

    while(c!=EOF){
        if(retval.tokenType==eTokenType::unknown) {
            
            retval.text+=c;
            readChar(); //dispose of the character we previewed    

            if(c=='\"') 
                retval.tokenType=eTokenType::quote;
            else if(isKeywordChar(c)) 
                retval.tokenType=eTokenType::text;
            else {
                retval.tokenType=eTokenType::symbol;
                break;
            }
            c=peekChar(); //peek at the next character to process
            continue;
        }
        
        if(retval.tokenType==eTokenType::quote){
            retval.text+=c;
            readChar(); //dispose of the character we previewed    
            if(c=='\"')  break; //closing quote
            c=peekChar(); //peek at the next character to process
            continue;
        } 
        
        //we are building a token from contiguous keyword characters
        if(!isKeywordChar(c)) break;  //break at the first non-keyword character
        retval.text+=c;
        
        readChar();  //dispose of the character we previewed
        c=peekChar(); //peek at the next character to process
    }
    
    if(c==EOF) {
        if(beguiler.resolveCurrentCompileScope()!=eCompileScope::root) beguiler.parseError("End of file encountered prematurely");
        retval.tokenType=eTokenType::eof;
    }
    return retval;
}
token fileReader::getToken(eTokenType tokenType){
    token retval=getToken();
    return retval.assertOneOf({tokenType}); 
}
token fileReader::getToken(vector<eTokenType> types){
    token retval=getToken();
    return retval.assertOneOf(types); 
}
token fileReader::getToken(string val){
    token retval=getToken();
    return retval.assertOneOf({val}); 
}
token fileReader::getToken(std::vector<string> vals){
    token retval=getToken();
    return retval.assertOneOf(vals); 
}