#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>

#include "fileLexer.h"
#include "parser.h"
#include "orbit.h"

using namespace std;

//----------------------------------------------------------------------------------------
//--Opening and closing files, and managing which of these is the "current file" 
void fileLexer::open(string filename){
    ifstream& inputFileStream=*(new ifstream(filename));  
    
    if (!inputFileStream.is_open()) bglParser.parseError("Unable to open file "+filename+".");
    files.push(make_tuple(&inputFileStream, filename, 1, 0)); //take the current file and push it on the stack    
}
void fileLexer::close(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top(); 
    inputFileStream->close();
    delete inputFileStream;
    files.pop();
}
int fileLexer::numOpen(){
    return files.size();
}
void fileLexer::moveToStart(){
    currentStream()->seekg(currentStream()->beg); 
}
ifstream* fileLexer::currentStream(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top(); 
    return inputFileStream;   
}
tuple<ifstream*, string, int, int> fileLexer::getDetail(){ //return saved information about the current file
    return files.top();
}

//----------------------------------------------------------------------------------------
//--Reading and evaluating characters from current file
void fileLexer::bleedSpaces(){ // ignore all spaces before the next token
    char c=peekChar(); 
    while(c!=EOF && isspace(c)) { //this is just whitespace; discard it
        readChar(); 
        c=peekChar(); 
    }
}
char fileLexer::peekChar(){ //look at what the next character is, without actually pulling it off the stream
    return currentStream()->peek();
}
char fileLexer::readChar(){ //actually get the next character, removing it from the stream
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

bool fileLexer::isValidIdentifierChar(char c){
    if(isalnum(c)) return true;
    if(c=='_') return true;
    return false;
}
//----------------------------------------------------------------------------------------
//--Turning characters in the stream into meaningful tokens which the parser can make sense of 
//----------------------------------------------------------------------------------------
// Read in one character at a time, constructing, then returning, a "basic" token.
// A "basic" token is the most meaningful token than can be pulled from a stream without additional 
// consideration.  All input can be translated into these tokens:
//
//  EOF:                The next token encountered was the end of file.
//  comment:            The next token encountered was a comment.  Text contains the entirety of the comment, whether
//                      it is a single line comment (//) or a multiline comment (/*...*/)
//  quote:              The next token encountered was a quoted string.  Text contains the entirety of the string, 
//                      including the quotes.
//  unclassifiedText:   The next token encountered was a series of contiguous "identifier appropriate" characters, 
//                      including the underscore and alphanumeric characters.  Note that this doesn't check for validity:  
//                      1abc is not a valid identifier, but will still be returned; as would 45.
//  symbol:             The next token encountered is a single, "non-identifier appropriate" character.  Note that 
//                      multiple symbols may be contiguous in the file, but each of these will be returned as separate 
//                      with the exception of comments and text string, with both start with symbols or symbol 
//                      pairs, e.g. ", //, and /*
//
// Note: by default, all begining whitespace is ignored ("bled" off of the stream) and each call to getBasicToken()
// returns the first actual text token; however, if the suppressBleed parameter is passed as true, and the first 
// encountered character is a whitespace, it will be returned as a symbol token.
token fileLexer::getBasicToken(bool suppressBleed){
    token retval;
    
    if(!suppressBleed) bleedSpaces();
    char c=peekChar(); 

    while(c!=EOF){
        if(retval.tokenType==eTokenType::unknown) { //we don't know what sort of token we are building yet; this is probably in the first couple of pass through our loop.  So, we analyze and determine the basic type of this token for the next pass of the loop.
            retval.value+=c;  //add the character we previewed to the value we are building
            readChar();      //dispose of it and move on to the next

            if(c=='/'){     //if the token starts with a slash, its likely a comment
                char nc=peekChar();                             
                if(nc=='/'||nc=='*') retval.tokenType=eTokenType::comment;
            } 
            else if(c=='\"') 
                retval.tokenType=eTokenType::quote;
            else if(isValidIdentifierChar(c)) 
                retval.tokenType=eTokenType::unclassifiedText;
            else {
                retval.tokenType=eTokenType::symbol;
                break;
            }
            c=peekChar(); //peek at the next character to process
            continue;
        }
        
        if(retval.tokenType==eTokenType::quote){
            retval.value+=c;
            readChar(); //dispose of the character we previewed    
            if(c=='\"')  break; //closing quote
            c=peekChar(); //peek at the next character to process
            continue;
        } 
        if(retval.tokenType==eTokenType::comment){
            retval.value+=c;
            readChar(); //dispose of the character we previewed    
            c=peekChar(); //peek at the next character to process

            if(retval.value[1]=='/' && c== '\n') break; //singleline 
            
            if(retval.value[1]=='*' && retval.value.length()>3 && retval.value[retval.value.length()-1]=='*' && c=='/'){ //multiline
                retval.value+=c;
                readChar();
                break;
            } 
            continue;  
        }
        
        //we are building a token from contiguous keyword characters
        if(!isValidIdentifierChar(c)) break;  //break at the first non-keyword character
        retval.value+=c;
        
        readChar();  //dispose of the character we previewed
        c=peekChar(); //peek at the next character to process
    }

    //a special case: we've reached the end of file.  If we are not at the global scope, then throw an error, because we've terminated early
    if(c==EOF) {
        if(bglParser.resolveCurrentCompileScope()!=eCompileScope::root) bglParser.parseError("End of file encountered prematurely");
        retval.tokenType=eTokenType::eof;
    }

    return retval;//return our completed basic token
}
//Return a token from the stream. This calls getBasicToken, and classifies it further in specific cases.  After this runs, the token 
//can be any of the following: 
//
//  EOF:                The next token encountered was the end of file.
//  comment:            The next token encountered was a comment.  Text contains the entirety of the comment, whether
//                      it is a single line comment (//) or a multiline comment (/*...*/)
//  quote:              The next token encountered was a quoted string.  Text contains the entirety of the string, 
//                      including the quotes.
//  dataType:           The next token is a valid data type.
//  identifier:         The next token is a valid identifier, but not a data type.
//  integer:            The next token is an integer number.
//  unclassifiedText:   The next token encountered was a series of contiguous "identifier appropriate" characters, 
//                      including the underscore and alphanumeric characters; however, it could not be further classified
//                      into another token type.  Usually, this indicates an error condition.
//  directive:          The next token encountered was a compiler directive (e.g., #include).
//  symbol:             The next token encountered is a single, "non-identifier appropriate" character. 
token fileLexer::getToken(){
    token next;
    token retval;
    
    //a special case: let's ignore all comment tokens
    do{
        retval=getBasicToken();
    }while(retval.tokenType==eTokenType::comment);
    
    if(retval.isOneOf({eTokenType::eof, eTokenType::quote})) return retval; //just return tokens which we know we can't expand
    
    //we have our basic token, but let's try to classify it a little more specifically, possibly grabbing additional basic tokens to complete more complex ones 
    if(retval.is("#")){ 
        next=getBasicToken(true); //to make sense, this MUST be a name directly connected to the # with no whitespaces in between
        if(!next.isValidIdentifier()) bglParser.parseError("Encountered invalid directive name '"+next.value+"'.");
        retval.value+=next.value;
        retval.tokenType=eTokenType::directive;
        return retval;
    }
    
    if(retval.isValidIdentifier()) retval.tokenType=eTokenType::identifier; //if it meets the identifier format, default classification an identifier; however, 
                                                                            //  it may NOT be and could change below; other tokenTypes also meet the algorithmic 
                                                                            //  rules for valid identifiers (data types for example)
    if(retval.isNumeric())retval.tokenType=eTokenType::integer;
    if(retval.isDataType()) retval.tokenType=eTokenType::dataType; //this would replace the previous identifier assumption

    return retval; 
}
//Get tokens, limited to specific types or values. Throw a compile-time error if the next token does not match the requirements.
//These are used when the language absolutely requires the next token to conform to a specific set of features.
token fileLexer::getToken(eTokenType tokenType){
    token retval=getToken();
    return retval.assertOneOf({tokenType}); 
}
token fileLexer::getToken(vector<eTokenType> types){
    token retval=getToken();
    return retval.assertOneOf(types); 
}
token fileLexer::getToken(string val){
    token retval=getToken();
    return retval.assertOneOf({val}); 
}
token fileLexer::getToken(std::vector<string> vals){
    token retval=getToken();
    return retval.assertOneOf(vals); 
}
