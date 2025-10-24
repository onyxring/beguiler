#include <string>
#include <string_view>
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
        static constexpr std::string endStatement=";"; 
        static constexpr std::string assignment ="="; 
        static constexpr std::string parenOpen ="(";  
        static constexpr std::string parenClose =")"; 
        static constexpr std::string bracesOpen ="{";  
        static constexpr std::string bracesClose ="}"; 
        static constexpr std::string bracketOpen ="["; 
        static constexpr std::string bracketClose ="]"; 
        static constexpr std::string comma  =","; 
        
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
        token emit();
        size_t chk();
        
};
extern token _nullToken;
