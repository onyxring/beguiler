#include <iostream>
#include <vector>
#include "token.h"
#include "orbit.h"
#include "parser.h"

using namespace std;

token _nullToken;

bool token::is(eTokenType type){return isOneOf({type});}
bool token::is(string text){return isOneOf({text});}
bool token::isNot(eTokenType type){return !isOneOf({type});}
bool token::isNot(string text){return !isOneOf({text});}

bool token::isOneOf(vector<eTokenType> types){
    for (eTokenType &type : types) {
        //if(type==eTokenType::unknown || tokenType==type) return true;
        if(tokenType==type) return true;
    }
    return false;
}
bool token::isOneOf(vector<string> vals){
    for (string &val : vals) {
        if(text==val) return true;
    }
    return false;
}

bool token::isNull(){
    return is(eTokenType::unknown);
}
bool token::isDataType(){
    if(string("void,int,string").find(text)!=string::npos) return true;
    return false;
}

token token::assert(eTokenType type, std::string errMsg){ return assertOneOf({type}, errMsg); }
token token::assert(std::string text, std::string errMsg){ return assertOneOf({text}, errMsg); }

token token::assertOneOf(vector<eTokenType> types, string errMsg){
    if(!isOneOf(types)) {
        if(errMsg=="") beguiler.parseError(assertFailedMessage(types));
        beguiler.parseError(errMsg);
    }
    return *this;
}
token token::assertOneOf(vector<string> vals, string errMsg){
    if(!isOneOf(vals)) {
        if(errMsg=="") beguiler.parseError(assertFailedMessage(vals));
        beguiler.parseError(errMsg);
    }
    return *this;
}
token token::assertDataType(){
    if(!isDataType()) beguiler.parseError("Expected data type.");
    return *this;
}        

string token::assertFailedMessage(vector<eTokenType> types){
    string retval=format("Unexpected {0} '{1}'.  Expected ",(tokenType==eTokenType::quote)?"literal string":"token", text);
    
    
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
    string retval=format("Unexpected {0} '{1}'.  Expected ",(tokenType==eTokenType::quote)?"literal string":"token", text);

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
size_t token::chk() {
    const long long p = 131;
    const long long m = 4294967291; // 2^32 - 5, largest 32 bit prime
    long long total = 0;
    long long current_multiplier = 1;
    for (int i = 0; text[i] != '\0'; ++i){
        total = (total + current_multiplier * text[i]) % m;
        current_multiplier = (current_multiplier * p) % m;
    }
    return total;
}
token token::emit(){
    beguiler.emit.put(text);
    return *this;
}
string token::tokenTypeToString(eTokenType type){
    switch(type){
        case eTokenType::text: return "keyword";
            break;
        case eTokenType::quote: return "literal string";
            break;
        case eTokenType::symbol: return "operator";
            break;
        case eTokenType::unknown: return "Unknown";
            break;
        case eTokenType::eof: return "EoF";
            break;
        
    }
}
token token::expand(){
    text+=beguiler.file.getToken().text;
    return *this;
}


