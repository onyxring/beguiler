#include <iostream>
#include <vector>

#include "token.h"
#include "beguiler.h"
#include "bglParser.h"
#include "bglLanguageService.h"

using namespace std;

const token _nullToken;

#pragma region Type checks
    bool token::is(eTokenType type){return isOneOf({type});}
    bool token::is(string text){return isOneOf({text});}
    bool token::isNot(eTokenType type){return !isOneOf({type});}
    bool token::isNot(string text){return !isOneOf({text});}

    bool token::isOneOf(vector<eTokenType> types){
        for (eTokenType &type : types) {
            if(type == eTokenType::name){
                if(tokenType == eTokenType::identifier || tokenType == eTokenType::dataType) return true;
            } else {
                if(tokenType == type) return true;
            }
        }
        return false;
    }
    bool token::isOneOf(vector<string> vals){
        for (string &val : vals) {
            if(value==val) return true;
        }
        return false;
    }
    bool contains(vector<string> vals, string val){
        for (string &v : vals) {
            if(v==val) return true;
        }
        return false;
    }
    //primarily used to test for _nullToken, which is defined as a default value for optional parameters
    bool token::isNull(){ 
        return is(eTokenType::unknown); 
    }
    
    bool token::isDataType(){
        return languageService.isObjectType(value);    
    }
    bool token::isNumeric(){
        for(char c:value){
            if(isnumber(c)==false) return false;
        }
        return true;
    }
    bool token::isValidIdentifier(){
        if(!(isalpha(value[0]) || value[0]=='_')) return false;
        for (int i = 0; value[i] != '\0'; ++i){
            if(isalnum(value[i]==false&&value[i]!='_')) return false;   
        }
        return true;
    }
#pragma endregion

#pragma region Conversion operators
    token::operator string(){
        if(tokenType==eTokenType::quote) return unescape(value);
        return value;
    }
#pragma endregion

#pragma region Assertions

    token token::assert(eTokenType type, std::string errMsg){ return assertOneOf({type}, errMsg); }
    token token::assert(std::string text, std::string errMsg){ return assertOneOf({text}, errMsg); }

    token token::assertOneOf(vector<eTokenType> types, string errMsg){
        if(!isOneOf(types)) {
            if(errMsg=="") parser.parsingError(assertFailedMessage(types));
            parser.parsingError(errMsg);
        }
        return *this;
    }
    token token::assertOneOf(vector<string> vals, string errMsg){
        if(!isOneOf(vals)) {
            if(errMsg=="") parser.parsingError(assertFailedMessage(vals));
            parser.parsingError(errMsg);
        }
        return *this;
    }
    token token::assertDataType(){
        if(!isDataType()) parser.parsingError("Expected data type.");
        return *this;
    }        

    string token::assertFailedMessage(vector<eTokenType> types){
        string retval=format("Unexpected {0} '{1}'.  Expected ",(tokenType==eTokenType::quote)?"literal string":"token", value);
        
        types.erase(std::remove(types.begin(), types.end(), eTokenType::eof), types.end()); 
        types.erase(std::remove(types.begin(), types.end(), eTokenType::unknown), types.end()); 

        int printed=0;
        for(int t=0;t<types.size();t++){
            string val;
            if(t>0) {
                retval+=", ";
                if(t==types.size()-1) retval+="or ";
            }
            val=tokenTypeToString(types[t]);
            if(val=="") continue;
            retval+=val;
            printed++;
        }
        retval+=".";
        return retval;
    }
    string token::assertFailedMessage(vector<string> vals){
        string retval=format("Unexpected {0} '{1}'.  Expected ",(tokenType==eTokenType::quote)?"literal string":"token", value);

        for(int t=0;t<vals.size();t++){
            if(t>0) {
                retval+=", ";
                if(t==vals.size()-1) retval+="or ";
            }
            retval+="'"+vals[t]+"'";
        }
        retval+=".";
        return retval;
    }
#pragma endregion

//generate a checksum for this token; primarily to make the token values useable in switch statements
size_t token::chk() {
    const long long p = 131;
    const long long m = 4294967291; // 2^32 - 5, largest 32 bit prime
    long long total = 0;
    long long current_multiplier = 1;
    for (int i = 0; value[i] != '\0'; ++i){
        total = (total + current_multiplier * value[i]) % m;
        current_multiplier = (current_multiplier * p) % m;
    }
    return total;
}
//pass the token text off to the emitter
token token::emit(){
    // if(tokenType==eTokenType::quote){
    //     parser.emit.put(unescape(value));
    // }
    // else{
    //     parser.emit.put(value);
    // }
    return *this;
}
string token::unescape(string value){
    value=replaceAll(value,"\\n","^");
    value=replaceAll(value,"\\\"","~");
    return value;
}
string token::replaceAll(string str, const string& from, const string& to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); 
    }
    return str;
}
string token::tokenTypeToString(eTokenType type){
    switch(type){
        case eTokenType::identifier: return "identifier";
            break;
        case eTokenType::quote: return "literal string";
            break;
        case eTokenType::symbol: return "symbol";
            break;
        case eTokenType::unknown: return "Unknown";
            break;
        case eTokenType::eof: return "EoF";
            break;
        case eTokenType::comment: return "comment";
            break;
        case eTokenType::dataType: return "data type";
            break;
        case eTokenType::name: return "name";
            break;
        case eTokenType::directive: return "directive";
            break;
        case eTokenType::unclassifiedText: return "unrecognized pattern";
            break;
        case eTokenType::integer: return "integer";
            break;
        case eTokenType::oper: return "operator";
            break;
        case eTokenType::dictionaryWord: return "dictionary word";
            break;
        case eTokenType::dictionaryWordPlural: return "plural dictionary word";
            break;
        case eTokenType::charLiteral: return "character literal";
            break;
    }
    return "Unknown";
}
