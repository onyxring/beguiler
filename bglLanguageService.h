#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <map>
#include <set>
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
    // Stored (deferred) emit-first/last blocks. Each is a named raw-I6 body that is emitted ONLY
    // when its name appears in `firedStoredNames` — i.e., when some `##triggerEmitter <name>`
    // annotation (in a Beguile emitter body or in a __builtins.i6b template header) fires
    // during emission. Lets BLR authors declare on-demand helper routines that don't pay an
    // I6 "declared but not used" warning when the trigger never fires. Resolved at the
    // emit-first/last placeholder positions during i6Emitter::resolvedOutput().
    map<string,string> storedEmitFirstBlocks;
    map<string,string> storedEmitLastBlocks;
    set<string>        firedStoredNames;       // names that fired via ##triggerEmitter during emission
    vector<verbObjectDef*> verbs;  // all verb declarations (extern and non-extern) for action-constant lookup
    set<string> evictedExternWords;  // words evicted from a library verb via word-level `grammar -= {.w}`;
                                     // emitter lowers each to I6 `Extend only 'w' replace` (see i6Emitter)
    int ternaryTempCount = 0;  // number of _bgl_tempN globals needed; increments per ternary/null-coalesce
    int switchEndCounter = 0;  // names the `.label` an if-chain switch's `break` jumps to
    bool tryCatchNeeded = false;     // set true when try/catch is used; drives conditional _bgl_catch_cookie emission
    int tryCatchCounter = 0;         // unique ID for try/catch label generation
    int captureCounter = 0;          // unique ID for closure capture globals
    bool switchTempNeeded = false;  // set true when any switch uses guards; drives conditional _bgl_sw emission
    bool arrayInUse = false;        // set true when `#include <array>` resolved; gates tracked-length array layout + magic stamping + for-in length-probe
    bool linqInUse = false;         // set true when `#include <array>` resolved; drives LINQ scratch buffer + constant emission
    bool worldInUse = false;        // set true when `#include <bglWorld>` resolved; drives _BGL_WORLD_BUFSIZE constant emission
    bool forInScratchInUse = false; // set true when `for(x in {literal,list})` is lowered; drives _BGL_FORIN_SCRATCH_CAP constant emission (paired with the #storedEmitFirst scratchSupport block)
    bool bufInUse = false;          // set true when `#include <buf>` resolved; gates tracked-length byteArray layout (length+magic suffix at end)

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

        // First entry in `globals` whose `name` matches exactly (no case folding); nullptr if none.
        typeDef* findGlobal(const std::string& name);
        // First entry in `globals` matching `name` AND castable to T; scans past a same-named
        // entry of a different type. nullptr if none.
        template<class T> T* findGlobalAs(const std::string& name){
            for(typeDef* g : globals)
                if(g->name == name)
                    if(T* t = dynamic_cast<T*>(g)) return t;
            return nullptr;
        }
        // getType(name) narrowed to a class/enum/object-instance definition; nullptr if the name
        // is unknown or names a different kind of type.
        classDef*  findClass(const std::string& name){ return dynamic_cast<classDef*>(&getType(name)); }
        enumDef*   findEnum(const std::string& name){ return dynamic_cast<enumDef*>(&getType(name)); }
        objectDef* findObjectType(const std::string& name){ return dynamic_cast<objectDef*>(&getType(name)); }
        // True when `name` is a property Inform 6 treats as ADDITIVE, so its contributions
        // accumulate instead of replacing. Declared by `additive property` / `extern additive
        // property` (the core BLR declares `name`, which the I6 compiler itself makes additive).
        bool isAdditiveProperty(const std::string& name) const;
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

        // Read the integer default value declared on a class field in BLR. Returns the field's
        // `declaredExpressionValue` parsed as an int, or `fallback` if the field is absent / has no
        // default / isn't an int literal. Used to resolve compile-time defaults (e.g. `class verb`'s
        // `int priority = 10;`) without hardcoding the value in C++.
        int getClassFieldIntDefault(const string& className, const string& fieldName, int fallback);

        vector<string> operators={"-=","+=","?=","==","!=","<=",">=","=~","&&","||","++","--","<<",">>","<<=",">>=","<=>","*=","/=","%=","&=","|=","^=","=>","?.","??",":="};
};

extern bglLanguageService languageService;
extern beguilerSettingsDef beguilerSettings;

// True when compiling for the Z-machine. Glulx is the default target, and the Glulx-shaped output
// of every caller is valid I6 on any target, so an as-yet-unresolved (empty) target reads as Glulx.
bool targetIsZcode();

// The kind of literal a `#beguilerSettings.<property>` reference resolves to.
enum class eSettingKind { unknown, str, integer, boolean };
// Resolve a settings property (key already lowercased) to its compile-time value, filling exactly
// one of `sv` / `iv` / `bv` per the returned kind; `unknown` means the name is not a declared
// property. Every declared property is readable (§17.7) — this is the ONE place that knows how a
// property reads back, shared by the expression handler, the `##beguilerSettings.` raw-I6
// substitution and `#if`, so the three cannot drift apart.
eSettingKind readBeguilerSetting(const string& key, string& sv, int& iv, bool& bv);

// Virtual in-memory file overlay: maps a normalized absolute path to source text.
// Populated only in LSP mode (see LspServer::parseDocument) so that a live-scanned
// `_blorbAssets.bgl` asset enum resolves and opens from memory — no compile, no disk
// write. Empty during normal compiles, so include resolution is unchanged there.
extern map<string, string> g_virtualBglFiles;

// Normalized lookup key for the overlay (absolute, lexically-normal, forward slashes,
// lowercased — Beguile is case-insensitive). Used by the LSP registrar, resolveIncludePath,
// and fileLexer::open so they agree on the same key.
string virtualFileKey(const string& path);
// If `path` names a registered virtual file, copies its content into `out` and returns true.
bool lookupVirtualFile(const string& path, string& out);