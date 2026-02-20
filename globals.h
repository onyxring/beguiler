#pragma once
#include <string_view>
// #include "bglParser.h"
// #include "bglLanguageService.h"
// extern bglParser parser;
// extern bglLanguageService languageService;

using namespace std;

enum class eCompileContext {global, objectDef, codeBlock};

enum class eNodeType{
    root,
    directive,
    integer,
    symbol,
    quote,
    variableDeclaration,
    constantDeclaration,
    objectDeclaration,
    classDeclaration,
    parameterListDeclaration,
    routine,
    executableStatement,
    tagDeclaration
    //expression,
};

enum class eCompileLanguage {beguile, i6};

struct resultsStruct{
    std::stringstream tempText;
    std::stringstream bodyText;
};

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
