#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>

#include "types.h"
#include "helpers.h"
#include "bglLanguageService.h"
#include "fileLexer.h"
#include "bglParser.h"
#include "beguiler.h"

using namespace std;

//vector<string> operatorList={"-=","+=","?=","==","!=","<=",">=","&&","||","++","--","<<",">>","*=","/=","%=","&=","|=","^="};  //TODO: this should be moved to a language-specific global location, which will probably need to be shared by the parser
//----------------------------------------------------------------------------------------
//--Opening and closing files, and managing which of these is the "current file" 
void fileLexer::open(string filename){
    ifstream& inputFileStream=*(new ifstream(filename));  
    
    if (!inputFileStream.is_open()) parser.parsingError("Unable to open file "+filename+".");
    files.push(make_tuple(&inputFileStream, filename, 1, 0)); //take the current file and push it on the stack    
}
void fileLexer::close(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top(); 
    inputFileStream->close();
    delete inputFileStream;
    files.pop();
}
int fileLexer::getNumberOfOpenFiles(){
    return files.size();
}
void fileLexer::moveToStart(){
    currentStream()->seekg(currentStream()->beg); 
}
ifstream* fileLexer::currentStream(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top(); 
    return inputFileStream;   
}
tuple<ifstream*, string, int, int> fileLexer::getCurrentFileDetail(){ //return saved information about the current file
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
/*----------------------------------------------------------------------------------------
    getBasicToken()

    This routine reads in one character at a time, constructing, then returning, the most 
    meaningful token than can be pulled from a stream without additional consideration.  
    All input can be translated into these tokens (see commends on getToken for more 
    information.    

        EOF             
        comment               
        quote    
        unclassifiedText
        operator 
        symbol

    Note: by default, all begining whitespace is ignored ("bled" off of the stream) and each call to getBasicToken()
    returns the first actual text token; however, if the suppressBleed argument is passed as true, and the first 
    encountered character is a whitespace, it will be returned as a symbol token.
*/
token fileLexer::getBasicToken(bool suppressBleed){
    token retval;    
    
    if(!suppressBleed) bleedSpaces();
    char c=peekChar(); 

    while(c!=EOF){
        if(retval.tokenType==eTokenType::unknown) {     /*  We don't yet know what sort of token we are building yet (this is probably  the first pass
                                                            through of our loop).  Let's determine the basic type of this token so we know how to process 
                                                            it in subsequent passes. */

            retval.value+=c;    //add the character we previewed (peeked at) to the value we are building
            readChar();         //dispose of it and move on to the next
            char nc=peekChar();                             
            string twoChars=retval.value + nc; /*   we do a lot of two character checks, so let's just build this string once and use it repeatedly; we will 
                                                    readChar() to dispose of the nc if we end up using it, but for now we just want to peek at it without consuming it*/
            
            if(twoChars=="//" || twoChars=="/*") 
                retval.tokenType=eTokenType::comment;
            else if(c=='\"')
                retval.tokenType=eTokenType::quote;
            else if(isValidIdentifierChar(c))
                retval.tokenType=eTokenType::unclassifiedText;
            else {                
                if(find(languageService.operators.begin(), languageService.operators.end(), retval.value)!=languageService.operators.end()){
                    retval.tokenType=eTokenType::oper; 
                }
                else if(find(languageService.operators.begin(), languageService.operators.end(), twoChars)!=languageService.operators.end()){
                    retval.value=twoChars; //note: this overwrites the previous value, which is just the first character
                    readChar(); //dispose of the second character we previewed   
                    retval.tokenType=eTokenType::oper;
                }
                else{
                    retval.tokenType=eTokenType::symbol;
                }
                break;
            }
            c=peekChar(); //peek at the next character to process
            continue;
        }
        
        if(retval.tokenType==eTokenType::quote){
            readChar(); //dispose of the character we previewed

            if(c=='\\')  { //translate Beguile escape sequences to I6 equivalents
                c=peekChar();
                readChar();
                if     (c=='n')  retval.value+='^';          // \n  -> ^ (I6 newline)
                else if(c=='"')  retval.value+='~';          // \"  -> ~ (I6 double-quote)
                else if(c=='\\') retval.value+="@@92";        // \\  -> @@92 (literal backslash)
                else if(c=='^')  retval.value+="@@94";       // \^  -> @@94 (literal caret)
                else if(c=='~')  retval.value+="@@126";      // \~  -> @@126 (literal tilde)
                else if(c=='@')  retval.value+="@@64";       // \@  -> @@64 (literal at-sign)
                else           { retval.value+='\\'; retval.value+=c; } // unknown: pass through
            }
            else{
                retval.value+=c;
                if(c=='\"')  break; //closing quote
            }
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
        if(parser.getCurrentCompileContext()!=eCompileContext::global) parser.parsingError("End of file encountered prematurely");
        retval.tokenType=eTokenType::eof;
    }

    return retval;//return our completed basic token
}
string fileLexer::getRawTextThroughClosingBrace(){
    string retval;
    int count=1; //we have already encountered the first open brace, which is why we are calling this function, so we start our count at 1
    char c=readChar(); 

     while(count>0){
         if(c=='}') count--;
         if(count==0) break; //don't include the closing brace in the text we return
         if(c=='{') count++;
        
         retval=retval+c; 
         c=readChar();      
     }
     return retval;
}

sourceLocation fileLexer::currentLocation(){
    if(files.empty()) return {};
    auto [stream, fileName, curLine, curCol] = files.top();
    return {fileName, curLine};
}

// token fileLexer::getRunTokenEol(){
//     token retval;
//     retval.tokenType=eTokenType::unclassifiedText;
    
//     char c=peekChar(); 

//     while(c!=EOF&&c != '\n'){
//         retval.value+=c; 
//         readChar();      
//     }
//     return retval;
// }
// token fileLexer::getRunTokenBraceClose(){
//     token retval;
//     retval.tokenType=eTokenType::unclassifiedText;
    
//     char c=peekChar(); 
//     while(c!=EOF){
//         &&c != '}'){
//         retval.value+=c; 
//         readChar();      
//     }
//     return retval;
// } 

/*----------------------------------------------------------------------------------------
    getToken()

    Return a token from the stream. This calls getBasicToken, and tries to classify it further.  After this runs, the token 
    can be any of the following: 

    EOF:                The next token encountered was the end of file.    
    quote:              The next token encountered was a quoted string.  Text contains the entirety of the string, 
                        including the quotes.
    dataType:           The next token is a valid data type.
    identifier:         The next token is a valid identifier, but not a data type.
    integer:            The next token is an integer number.
    directive:          The next token encountered was a compiler directive (e.g., #include).
    operator:           The next token encountered is a recognized language operator.
    symbol:             The next token encountered is a single symbol which was not matched to an operator.
                        Note: multiple symbols may appear contiguously in the file, but each will be 
                        returned as separate tokens.
                        Note: comments and text strings are not returned as symbols, even though they both start
                        with symbols or symbol pairs.
    unclassifiedText:   The next token encountered was a series of contiguous "identifier appropriate" characters, 
                        including the underscore and alphanumeric characters; however, it could not be further classified
                        into another token type.  Usually, this indicates an error condition.
    
    Note: comment tokens are discarded, whether single line or multiline comments 
*/
token fileLexer::getToken(){
    token next;
    token retval;
    
    //a special case: let's ignore all comment tokens
    do{
        retval=getBasicToken();
    }while(retval.tokenType==eTokenType::comment);
    
    if(retval.isOneOf({eTokenType::eof, eTokenType::quote, eTokenType::charLiteral})){ prevTokenType = retval.tokenType; return retval; } //just return tokens which we know we can't expand

    // normalize to lowercase for case-insensitive parsing (string literals excluded above)
    retval.originalValue = retval.value; // save pre-lowercase value for case-sensitive I6 emission
    transform(retval.value.begin(), retval.value.end(), retval.value.begin(), ::tolower);

    //we have our basic token, but let's try to classify it a little more specifically, possibly grabbing additional basic tokens to complete more complex ones
    if(retval.is("#")){
        if(peekChar() == '#'){
            // ## prefix — Beguile compile-time directive (evaluated by the transpiler, not passed to I6)
            readChar(); // consume second '#'
            next=getBasicToken(true);
            if(!next.isValidIdentifier()) parser.parsingError("Encountered invalid Beguile directive name '"+next.value+"'.");
            transform(next.value.begin(), next.value.end(), next.value.begin(), ::tolower);
            retval.value = "##" + next.value;
            retval.tokenType=eTokenType::directive;
            prevTokenType = eTokenType::directive;
            return retval;
        }
        next=getBasicToken(true); //to make sense, this MUST be a name directly connected to the # with no whitespaces in between
        if(!next.isValidIdentifier()) parser.parsingError("Encountered invalid directive name '"+next.value+"'.");
        transform(next.value.begin(), next.value.end(), next.value.begin(), ::tolower);
        retval.value+=next.value;
        retval.tokenType=eTokenType::directive;
        prevTokenType = eTokenType::directive;
        return retval;
    }

    // Character literals: 'x'  (single character, with escape support)
    if(retval.is("'")){
        char c = peekChar(); readChar();
        string charVal;
        if(c == '\\'){
            c = peekChar(); readChar();
            if     (c == 'n')  charVal += '^';   // \n -> ^ (I6 newline)
            else if(c == '\'') charVal += '\'';  // \' -> literal quote
            else if(c == '\\') charVal += '\\';  // \\ -> backslash
            else             { charVal += '\\'; charVal += c; } // unknown: pass through
        } else {
            charVal += c;
        }
        // consume closing '
        if(peekChar() == '\'') readChar();
        retval.value = charVal;
        retval.tokenType = eTokenType::charLiteral;
        prevTokenType = eTokenType::charLiteral;
        return retval;
    }

    // Dictionary word literals: .word (singular) and ..word (plural)
    // Only recognised when NOT immediately after an identifier (which would be dot-access).
    if(retval.is(".") && prevTokenType != eTokenType::identifier && prevTokenType != eTokenType::dataType){
        char c1 = peekChar();
        // Helper lambda: read dict-word chars (identifier chars + apostrophe).
        // A trailing '.' triggers a warning and is discarded.
        auto readDictWord = [&]() -> string {
            string w;
            char ch = peekChar();
            while(isValidIdentifierChar(ch) || ch == '\''){
                w += (char)tolower(ch);
                readChar();
                ch = peekChar();
            }
            if(ch == '.'){
                auto loc = currentLocation();
                cerr << loc.file << ":" << loc.line << ": warning: character '.' is not valid for dictionary words. ignoring.\n";
                readChar(); // discard the period
            }
            return w;
        };

        if(c1 == '.'){
            auto savepos = currentStream()->tellg();
            readChar(); // consume the second '.'
            char c2 = peekChar();
            if(isalpha(c2)){
                retval.value = readDictWord();
                retval.tokenType = eTokenType::dictionaryWord;
                retval.isPlural = true;
                prevTokenType = eTokenType::dictionaryWord;
                return retval;
            }
            currentStream()->seekg(savepos); // not a plural dict word; restore stream
        } else if(isalpha(c1)){
            retval.value = readDictWord();
            retval.tokenType = eTokenType::dictionaryWord;
            retval.isPlural = false;
            prevTokenType = eTokenType::dictionaryWord;
            return retval;
        }
    }
    
    if(retval.isValidIdentifier()) retval.tokenType=eTokenType::identifier; //if it meets the identifier format, default classification an identifier; however, 
                                                                            //  it may NOT be and could change below; other tokenTypes also meet the algorithmic 
                                                                            //  rules for valid identifiers (data types for example)
    if(retval.isNumeric())retval.tokenType=eTokenType::integer;
    if(retval.isDataType()) retval.tokenType=eTokenType::dataType; //this would replace the previous identifier assumption
    if(retval.tokenType==eTokenType::symbol && retval.isOneOf({"=","+","-","*","/","%","<",">","!","&","|","^"})){
        retval.tokenType=eTokenType::oper;
    }

    prevTokenType = retval.tokenType;
    return retval;
}
token fileLexer::peekToken(){
    return peekToken(1);
}
token fileLexer::peekToken(int tokNum){
    token retval;
    auto savepos=currentStream()->tellg();
    eTokenType savedPrev = prevTokenType;
    for(int i=0;i<tokNum;i++){
        retval=getToken();
    }
    currentStream()->seekg(savepos);
    prevTokenType = savedPrev;
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

