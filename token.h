#pragma once
#include <string>
#include <string_view>
#include <stack>
#include <vector>

enum class eTokenType{
    unknown, 
    eof,
    comment,
    unclassifiedText,
    identifier,
    directive,
    dataType,
    integer,
    symbol,
    quote,
    oper,
    //expression,
};

class parseNode; //forward declaration

class token {
     
     public:
        eTokenType tokenType=eTokenType::unknown;
        std::string value;

        static constexpr std::string endStatement=";"; 
        static constexpr std::string assignment ="="; 
        static constexpr std::string parenOpen ="(";  
        static constexpr std::string parenClose =")"; 
        static constexpr std::string braceOpen ="{";  
        static constexpr std::string braceClose ="}"; 
        static constexpr std::string bracketOpen ="["; 
        static constexpr std::string bracketClose ="]"; 
        static constexpr std::string constant="const"; 
        static constexpr std::string comma  =","; 

        bool is(eTokenType);
        bool is(std::string);
        bool isNot(eTokenType);
        bool isNot(std::string);
        bool isOneOf(std::vector<eTokenType>);
        bool isOneOf(std::vector<std::string>);
        bool isNull();
        bool isObjectType();
        bool isDataType();
        bool isValidIdentifier();
        bool isNumeric();

        token assert(eTokenType, std::string="");
        token assert(std::string, std::string="");
        token assertOneOf(std::vector<eTokenType>, std::string="");
        token assertOneOf(std::vector<std::string>, std::string="");
        token assertDataType();
        
        operator parseNode(); 
        operator std::string(); 
        
    
        std::string assertFailedMessage(std::vector<eTokenType> types);
        std::string assertFailedMessage(std::vector<std::string> vals);
        std::string tokenTypeToString(eTokenType type);

        std::string unescape(std::string value);
        std::string replaceAll(std::string str, const std::string& from, const std::string& to);

        token emit();
        size_t chk();
        
};
extern const token _nullToken;
