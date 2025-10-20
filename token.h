#include <string>
#include <stack>
#include <vector>
#pragma once

enum class eTokenType{
    unknown,
    eof,
    text,
    symbol,
    quote,
};

class token {
     
     public:
        static std::string endStatement; 
        static std::string assignment;
        static std::string parenOpen; 
        static std::string parenClose; 
        static std::string bracesOpen; 
        static std::string bracesClose; 
        static std::string bracketOpen;
        static std::string bracketClose; 
        static std::string comma; 
        
        eTokenType tokenType=eTokenType::unknown;
        
        std::string text;

        bool is(eTokenType);
        bool is(std::string);
        bool isNot(eTokenType);
        bool isNot(std::string);
        bool isOneOf(std::vector<eTokenType>);
        bool isOneOf(std::vector<std::string>);
        bool isNull();
        bool isDataType();

        token assert(eTokenType, std::string="");
        token assert(std::string, std::string="");
        token assertOneOf(std::vector<eTokenType>, std::string="");
        token assertOneOf(std::vector<std::string>, std::string="");
        token assertDataType();
        
        std::string assertFailedMessage(std::vector<eTokenType> types);
        std::string assertFailedMessage(std::vector<std::string> vals);
        std::string tokenTypeToString(eTokenType type);

        token expand();
};
extern token _nullToken;
