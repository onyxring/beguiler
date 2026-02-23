#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include "typeDef.h"

using namespace std;

class bglLanguageService{
    public:
        
    vector<unique_ptr<typeDef>> objectTypes;  //used to store named object definitions; both classes and object instances.
    vector<typeDef*> globals;  
    
    bglLanguageService();
        typeDef& getType(string);
        bool isObjectType(string);
        
        objectDef& registerType(objectDef&);
        classDef& registerType(classDef&);
        enumDef& registerType(enumDef&);
        typeDef& registerType(string);
        
        vector<string> operators={"-=","+=","?=","==","!=","<=",">=","&&","||","++","--","<<",">>","*=","/=","%=","&=","|=","^="};
};

extern bglLanguageService languageService;