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
    int ternaryTempCount = 0;  // number of _bgl_tempN globals needed; increments per ternary/null-coalesce
    bool tryCatchNeeded = false;     // set true when try/catch is used; drives conditional _bgl_catch_cookie emission
    int tryCatchCounter = 0;         // unique ID for try/catch label generation
    int captureCounter = 0;          // unique ID for closure capture globals
    bool switchTempNeeded = false;  // set true when any switch uses guards; drives conditional _bgl_sw emission

    // .inf-mode anchor regions extracted from the source .inf file. When non-empty,
    // signal the emitter to skip its own ICL generation (the user's `!%` block goes at
    // the very top of the output) and to splice the trailer (from the first `end;`
    // directive onward, including any text after) at the very bottom.
    string infHeader;   // user's `!%` lines from the top of the .inf file (with newlines preserved)
    string infTrailer;  // first `end;` directive through EOF (with newlines preserved)

    // .inf-mode flags. `isInfMode` is set by parseFile when the entry file is `.inf`.
    // `sawBglIsland` is set by parseInfFileBody when any #bgl/#bglDecl/#bglStmt island is
    // encountered. Together they let the emitter suppress the bglInit synthesis (which is
    // dead weight in a .inf with zero Beguile content).
    bool isInfMode = false;
    bool sawBglIsland = false;

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

        // True when `name` matches a known I6 property name — i.e. either a member of any
        // declared class/object (auto-registered by I6) or a free-standing `property foo;`
        // (or `extern property foo;`) declaration. Lets `obj.provides(name)` resolve
        // against names that have no in-scope identifier of their own. Lowercased
        // comparison; expects `name` to already be lowercased by the lexer.
        bool isKnownPropertyName(const string& name) const;

        // True when `name` matches a registered class — `class Foo {}` or
        // `extern class Foo;`. Lets `obj.is(name)` resolve a bare class identifier
        // even when the class has no corresponding in-scope value (extern classes
        // never enter `globals`). Mirrors `isKnownPropertyName` for `provides()`.
        bool isKnownClassName(const string& name) const;

        vector<string> operators={"-=","+=","?=","==","!=","<=",">=","=~","&&","||","++","--","<<",">>","<<=",">>=","*=","/=","%=","&=","|=","^=","=>","?.","??"};
};

extern bglLanguageService languageService;
extern beguilerSettingsDef beguilerSettings;