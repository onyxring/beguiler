#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <algorithm>

#include "helpers.h"
#include "bglParser.h"
#include "bglLanguageService.h"
#include "typeDef.h"

using namespace std;

// Re-push a stub being filled in (registerClass/registerObject/registerEnum/...) to
// `globals` if it has been removed since pre-scan time. Background: .inf-mode pre-scan
// erases its own globals additions on each island return so source-order interleaving
// with raw-I6 compositeNodes is preserved during main parse. For .bgl files, the stub
// is still in `globals` at its pre-scan position, so this is a no-op (the find succeeds).
template <typename T>
static void rePushIfMissing(vector<typeDef*>& globals, T* existing, bool isExternal, bool atGlobalScope){
    if(!atGlobalScope || isExternal) return;
    if(find(globals.begin(), globals.end(), (typeDef*)existing) == globals.end())
        globals.push_back((typeDef*)existing);
}

typeDef emptyTDef;
beguilerSettingsDef beguilerSettings;

bglLanguageService::bglLanguageService(){
    registerType("void");
    registerType("var");
    registerType("auto");
    registerType("func");
    // intliteral, stringliteral, charliteral are now declared as extern class in __beguileCore.bgl
}
void bglLanguageService::reset(){
    // Clear all accumulated state (leak the old typeDef* — acceptable for LSP)
    objectTypes.clear();
    objectInstances.clear();
    globals.clear();
    globalInits.clear();
    startupBlocks.clear();
    emitFirstBlocks.clear();
    emitLastBlocks.clear();
    verbs.clear();
    ternaryTempCount = 0;
    tryCatchNeeded = false;
    tryCatchCounter = 0;
    captureCounter = 0;
    switchTempNeeded = false;
    infHeader.clear();
    infTrailer.clear();
    isInfMode = false;
    sawBglIsland = false;
    // Re-register the four base types
    registerType("void");
    registerType("var");
    registerType("auto");
    registerType("func");
}
bool bglLanguageService::isObjectType(string name){
    if(getType(name)==emptyTDef) return false;
    return true;
}
bool bglLanguageService::isClassType(string name){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    for(typeDef* ot : objectTypes)
        if(ot->name == name) return true;
    // Recognize type parameters of the currently-being-parsed class as types.
    // Scoped to the class body via parser.currentClass; substitution at method-lookup
    // time replaces them with concrete types, so global registration isn't required
    // (and would collide with same-named instances like `Temperature t;`).
    if(parser.getCurrentClass() != nullptr)
        for(const string& tp : parser.getCurrentClass()->typeParameters)
            if(tp == name) return true;
    return false;
}
typeDef& bglLanguageService::getType(string name){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    for (typeDef* ot : objectTypes) {
        if(ot->name == name) return *ot;
    }
    for (typeDef* oi : objectInstances) {
        if(oi->name == name) return *oi;
    }
    // Type-parameter fallback: when `name` matches a type parameter of the currently-being-
    // parsed class, return a synthesized typeDef carrying that name. Lets callers like
    // `funcDef.returnType = getType("t")` preserve the parameter name through to the
    // method-resolution layer where substitution applies. Stored per-name in a static map
    // for stable references; allocated lazily on first request.
    if(parser.getCurrentClass() != nullptr){
        for(const string& tp : parser.getCurrentClass()->typeParameters){
            if(tp == name){
                static map<string, typeDef*> tparamSlots;
                auto it = tparamSlots.find(name);
                if(it == tparamSlots.end()){
                    typeDef* td = new typeDef();
                    td->name = name;
                    it = tparamSlots.emplace(name, td).first;
                }
                return *it->second;
            }
        }
    }
    return emptyTDef;
}

// Format a source location for "originally defined at ..." messages.
static string fmtSrc(const sourceLocation& src){
    if(src.file.empty()) return "unknown location";
    return format("{0}:{1}", src.file, src.line);
}

bool bglLanguageService::isKnownPropertyName(const string& name) const {
    if(name.empty()) return false;
    // Free-standing `property foo;` / `extern property foo;` — registered as a global
    // variableDeclaration with type "property".
    for(typeDef* g : globals)
        if(auto* vd = dynamic_cast<variableDeclaration*>(g))
            if(vd->type.name == "property" && vd->name == name) return true;
    // Class members — every member name auto-registers in I6's property table when its class
    // is emitted, so any of them is a valid `provides()` operand.
    for(typeDef* t : objectTypes){
        if(auto* cd = dynamic_cast<classDef*>(t))
            for(typeMember* m : cd->members)
                if(m->name == name) return true;
    }
    // Object instance members — a stand-alone `object foo { int bar; }` registers `bar`
    // in the I6 property table just like a class member would.
    for(typeDef* t : objectInstances){
        if(auto* od = dynamic_cast<objectDef*>(t))
            for(typeMember* m : od->members)
                if(m->name == name) return true;
    }
    return false;
}

bool bglLanguageService::isKnownClassName(const string& name) const {
    if(name.empty()) return false;
    for(typeDef* t : objectTypes)
        if(auto* cd = dynamic_cast<classDef*>(t))
            if(cd->name == name) return true;
    return false;
}

// Find a global variable by name (case-insensitive). Returns nullptr if not found.
static variableDeclaration* findGlobal(vector<typeDef*>& globals, const string& lowerName){
    for(typeDef* g : globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(g))
            if(vd->name == lowerName) return vd;
    }
    return nullptr;
}

typeDef& bglLanguageService::registerType(string name){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name))
        parser.parsingError(format("'{0}' is already defined", name));
    typeDef& baseType=*(new typeDef());
    baseType.name=name;
    objectTypes.push_back(&baseType);
    typeDef& retval=(typeDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && baseType.isExternal==false) globals.push_back(&retval);
    return retval;
}
enumDef& bglLanguageService::registerEnum(string name, bool isExternal, string dspName){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name)){
        enumDef* existing = dynamic_cast<enumDef*>(&getType(name));
        if(existing && existing->isPrePassStub){
            existing->isPrePassStub = false;
            existing->isExternal = isExternal;
            if(!dspName.empty()) existing->displayName = dspName;
            rePushIfMissing(globals, existing, isExternal,
                            parser.getCurrentCompileContext() == eCompileContext::global);
            return *existing;
        }
        string loc = existing ? fmtSrc(existing->src) : "unknown location";
        parser.parsingError(format("'{0}' is already defined (originally declared at {1})", dspName.empty() ? name : dspName, loc));
    }
    enumDef& newType=*(new enumDef());
    newType.name=name;
    newType.displayName=dspName;
    newType.isExternal=isExternal;
    newType.src = parser.file.currentLocation();
    objectTypes.push_back(&newType);
    enumDef& retval=(enumDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
classDef& bglLanguageService::registerClass(string name, bool isExternal, string dspName){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name)){
        classDef* existing = dynamic_cast<classDef*>(&getType(name));
        if(existing && existing->isPrePassStub){
            existing->isPrePassStub = false;
            existing->isExternal = isExternal;
            if(!dspName.empty()) existing->displayName = dspName;
            rePushIfMissing(globals, existing, isExternal,
                            parser.getCurrentCompileContext() == eCompileContext::global);
            return *existing;
        }
        string loc = existing ? fmtSrc(existing->src) : "unknown location";
        parser.parsingError(format("'{0}' is already defined (originally declared at {1})", dspName.empty() ? name : dspName, loc));
    }
    classDef& newType=*(new classDef());
    newType.name=name;
    newType.displayName=dspName;
    newType.isExternal=isExternal;
    newType.src = parser.file.currentLocation();
    objectTypes.push_back(&newType);
    classDef& retval=(classDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
objectDef& bglLanguageService::registerObject(string name, bool isExternal, string dspName){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name)){
        objectDef* existing = dynamic_cast<objectDef*>(&getType(name));
        if(existing && existing->isPrePassStub){
            existing->isPrePassStub = false;
            existing->isExternal = isExternal;
            existing->src = parser.file.currentLocation();
            if(!dspName.empty()) existing->displayName = dspName;
            rePushIfMissing(globals, existing, isExternal,
                            parser.getCurrentCompileContext() == eCompileContext::global);
            return *existing;
        }
        string loc = existing ? fmtSrc(existing->src) : "unknown location";
        parser.parsingError(format("'{0}' is already defined (originally declared at {1})", dspName.empty() ? name : dspName, loc));
    }
    objectDef& newType=*(new objectDef());
    newType.name=name;
    newType.displayName=dspName;
    newType.isExternal=isExternal;
    newType.src = parser.file.currentLocation();
    objectInstances.push_back(&newType);
    objectDef& retval=(objectDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
variableDeclaration& bglLanguageService::registerInstance(variableDeclaration& varDef){
    string lowerName = varDef.name;
    transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    // Check for collision with a previously registered global variable.
    // Exception: verb-typed globals are exempt from collision checks against any other global
    // because I6 action routines are accessed as ##VerbName and live in a completely separate
    // I6 namespace from regular globals (e.g. 'score' int vs 'Score' verb, 'open' attr vs 'Open' verb).
    if(variableDeclaration* existing = findGlobal(globals, lowerName)){
        string existingType = existing->type.name;
        string newType      = varDef.type.name;
        bool eitherIsVerb   = (existingType == "verb" || newType == "verb");
        // Allow full pass to claim a pre-pass stub — replace the pointer so subclass info
        // (e.g. arrayDeclaration::elementType) is preserved in the emitted output
        if(existing->isPrePassStub && !eitherIsVerb){
            for(typeDef*& g : globals)
                if(g == existing){ g = &varDef; break; }
            varDef.isPrePassStub = false;
            return varDef;
        }
        if(!eitherIsVerb){
            string loc = fmtSrc(existing->src);
            parser.parsingError(format("'{0}' is already defined (originally declared at {1})", varDef.name, loc));
        }
    }
    // Check for collision with a registered type (class/object/enum) of the same name
    if(isObjectType(lowerName)){
        string loc = "unknown location";
        typeDef& td = getType(lowerName);
        if(auto* cd = dynamic_cast<classDef*>(&td))       loc = fmtSrc(cd->src);
        else if(auto* od = dynamic_cast<objectDef*>(&td)) loc = fmtSrc(od->src);
        else if(auto* ed = dynamic_cast<enumDef*>(&td))   loc = fmtSrc(ed->src);
        parser.parsingError(format("'{0}' is already defined as a type (originally declared at {1})", varDef.name, loc));
    }
    globals.push_back(&varDef);
    return varDef;
}

verbObjectDef& bglLanguageService::registerVerbObject(string name, bool isExternal){
    // Allow full pass to claim a pre-pass stub
    string lower = name; transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for(verbObjectDef* v : verbs){
        if(v->name == lower && v->isPrePassStub){
            v->isPrePassStub = false;
            v->isExternal = isExternal;
            v->src = parser.file.currentLocation();
            if(classDef* vc = dynamic_cast<classDef*>(&getType("verb")))
                v->objectClass = vc;
            // Re-push to globals if the pre-scan removed it (.inf-mode islands erase
            // their pre-scan additions; main pass re-pushes here at the correct
            // source-order position).
            rePushIfMissing(globals, v, isExternal,
                            parser.getCurrentCompileContext() == eCompileContext::global);
            return *v;
        }
    }
    verbObjectDef& vd = *(new verbObjectDef());
    vd.name = lower;
    vd.isExternal = isExternal;
    vd.src = parser.file.currentLocation();
    if(classDef* vc = dynamic_cast<classDef*>(&getType("verb")))
        vd.objectClass = vc;
    verbs.push_back(&vd);
    if(!isExternal) globals.push_back(&vd);
    return vd;
}

string bglLanguageService::getEnumType(string valueName){
    transform(valueName.begin(), valueName.end(), valueName.begin(), ::tolower);
    for(typeDef* t : objectTypes){
        enumDef* e = dynamic_cast<enumDef*>(t);
        if(e != nullptr){
            for(enumValueDef* v : e->namedValues)
                if(v->name == valueName) return e->name;
        }
    }
    return "";
}

bglLanguageService languageService;