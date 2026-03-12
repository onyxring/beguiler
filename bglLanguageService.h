#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include "typeDef.h"

using namespace std;

class bglLanguageService{
    public:
        
    vector<typeDef*> objectTypes;  //used to store named object definitions; both classes and object instances.
    vector<typeDef*> globals;
    vector<pair<string,string>> globalInits;  // {varName, initBody} — populated for globals with init emitters
    vector<verbDef*> verbs;        // all verb declarations (extern and non-extern) for action-constant lookup
    
    bglLanguageService();
        typeDef& getType(string);
        bool isObjectType(string);
        
        typeDef& registerType(string); //for creating types by name, for example for type members of the core language (e.g., var and void)
        enumDef& registerEnum(string name, bool isExternal=false);
        classDef& registerClass(string name, bool isExternal=false);
        objectDef& registerObject(string name, bool isExternal=false);
        
        variableDeclaration& registerInstance(variableDeclaration&);
        verbDef& registerVerb(string name, bool isExternal=false);
        string getEnumType(string valueName); //returns the enum type name for a given enum value name, or "" if not found
        
        
        vector<string> operators={"-=","+=","?=","==","!=","<=",">=","&&","||","++","--","<<",">>","*=","/=","%=","&=","|=","^="};
};

extern bglLanguageService languageService;