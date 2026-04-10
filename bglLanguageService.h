#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include "typeDef.h"

using namespace std;

class bglLanguageService{
    public:
        
    vector<typeDef*> objectTypes;  //used to store class/type definitions (classDef, enumDef, typeDef)
    vector<typeDef*> objectInstances;  //used to store object instance definitions (objectDef)
    vector<typeDef*> globals;
    vector<pair<string,string>> globalInits;  // {varName, initBody} — populated for globals with init emitters
    vector<string> startupBlocks;             // raw I6 bodies from #startup { } directives; emitted inside bglInit()
    vector<string> emitFirstBlocks;           // raw I6 bodies from #emitfirst { } directives; emitted after ICL headers, before bglInit
    vector<string> emitLastBlocks;            // raw I6 bodies from #emitlast { } directives; emitted at end of I6 output
    vector<verbObjectDef*> verbs;  // all verb declarations (extern and non-extern) for action-constant lookup
    bool ternaryTempNeeded = false;  // set true when any ternary is lowered; drives conditional _bgl_temp emission
    bool tryCatchNeeded = false;     // set true when try/catch is used; drives conditional _bgl_catch_cookie emission
    int tryCatchCounter = 0;         // unique ID for try/catch label generation
    int captureCounter = 0;          // unique ID for closure capture globals
    bool switchTempNeeded = false;  // set true when any switch uses guards; drives conditional _bgl_sw emission
    
    bglLanguageService();
        void reset();  // clear all state and re-register base types (for LSP re-parse)
        typeDef& getType(string);
        bool isObjectType(string);
        bool isClassType(string);  // true for classes/enums/types only, not object instances
        
        typeDef& registerType(string); //for creating types by name, for example for type members of the core language (e.g., var and void)
        enumDef& registerEnum(string name, bool isExternal=false, string displayName="");
        classDef& registerClass(string name, bool isExternal=false, string displayName="");
        objectDef& registerObject(string name, bool isExternal=false, string displayName="");
        
        variableDeclaration& registerInstance(variableDeclaration&);
        verbObjectDef& registerVerbObject(string name, bool isExternal=false);
        string getEnumType(string valueName); //returns the enum type name for a given enum value name, or "" if not found
        
        
        vector<string> operators={"-=","+=","?=","==","!=","<=",">=","=~","&&","||","++","--","<<",">>","*=","/=","%=","&=","|=","^=","=>","?.","??"};
};

extern bglLanguageService languageService;
extern beguilerSettingsDef beguilerSettings;