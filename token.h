#pragma once
#include <string>
#include <initializer_list>
#include <string_view>
#include <algorithm>
#include <stack>
#include <vector>
#include "typeDef.h"

enum class eTokenType{
    unknown,
    eof,
    comment,
    unclassifiedText,
    identifier,
    directive,
    dataType,
    name,       // matches either identifier or dataType; use isDataType() to disambiguate
    integer,
    symbol,
    quote,
    rawQuote,              // @"..."  raw string literal — no Beguile escape processing
    oper,
    dictionaryWord,        // .word (isPlural=false) or ..word (isPlural=true)
    charLiteral,           // 'x'   → I6 'x'  (integer character value)
    //expression,
};

class token {
     
     public:
        eTokenType tokenType=eTokenType::unknown;
        std::string value;
        std::string originalValue; // pre-lowercase value, for case-sensitive I6 emission (e.g. verb/grammar names)
        std::string docComment;    // accumulated /// or /** */ doc text the lexer attached to this token; transferred to a declaration's docComment at registration
        bool isPlural=false;       // true for ..word dictionary word literals
        sourceLocation src;        // file and line where this token was read

        // `const char*`, not `std::string`: a constexpr std::string needs a standard library
        // implementing P0980R1 (libc++ 15+, libstdc++ 12+), which Apple's bundled clang does
        // not ship — so the plain `clang++` on a stock macOS could not build this at all.
        // Every consumer takes `std::string` by value, so these convert implicitly.
        static constexpr const char* endStatement=";"; 
        static constexpr const char* assignment ="="; 
        static constexpr const char* parenOpen ="(";  
        static constexpr const char* parenClose =")"; 
        static constexpr const char* braceOpen ="{";  
        static constexpr const char* braceClose ="}"; 
        static constexpr const char* bracketOpen ="["; 
        static constexpr const char* bracketClose ="]"; 
        static constexpr const char* constantDeclararion="const"; 
        static constexpr const char* classDeclaration="class"; 
        static constexpr const char* enumDeclaration="enum"; 
        static constexpr const char* bnumDeclaration="bnum"; 
        static constexpr const char* comma  =","; 
        static constexpr const char* period  ="."; 
        static constexpr const char* external ="extern";
        static constexpr const char* extend ="extend";
        static constexpr const char* replace ="replace";

        bool is(eTokenType);
        bool is(std::string);
        bool isNot(eTokenType);
        bool isNot(std::string);
        bool isOneOf(std::vector<eTokenType>);
        bool isOneOf(std::vector<std::string>);
        bool isNull();
        bool isDataType();   // true if the token value is a registered class/enum/base type
        bool isValidIdentifier();
        bool isNumeric();
        bool isString();   // true for both quote and rawQuote

        token assert(eTokenType, std::string="");
        token assert(std::string, std::string="");
        token assertOneOf(std::vector<eTokenType>, std::string="");
        token assertOneOf(std::vector<std::string>, std::string="");
        // Brace-list form. With `const char*` token constants, `{a, b}` is convertible to
        // BOTH vector overloads on some standard libraries (GCC flags it as ambiguous where
        // Clang does not), so the exact-match initializer_list overload settles it.
        token assertOneOf(std::initializer_list<std::string>, std::string="");
        token assertDataType();
        
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
