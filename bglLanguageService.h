#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include "typeDef.h"

using namespace std;

class bglLanguageService{
    public:
        std::vector<typeDef> objectTypes;
        typeDef getType(string);
        bool isObjectType(string);
        typeDef addObjectType(string);
        void registerEnum(enumDef&);
};

extern bglLanguageService languageService;