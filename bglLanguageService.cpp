#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

#include "helpers.h"
#include "bglParser.h"
#include "bglLanguageService.h"
#include "typeDef.h"

using namespace std;

typeDef emptyTDef;
beguilerSettingsDef beguilerSettings;

bglLanguageService::bglLanguageService(){
    registerType("void");
    registerType("var");
    registerType("func");
    // intliteral, stringliteral, charliteral are now declared as extern class in _beguileCore.bgl
}
bool bglLanguageService::isObjectType(string name){ 
    if(getType(name)==emptyTDef) return false;
    return true;
}
typeDef& bglLanguageService::getType(string name){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    for (typeDef* ot : objectTypes) {
        if(ot->name == name) return *ot;
    }
    return emptyTDef;
}

// typeDef& bglLanguageService::registerType(string name){
//     typeDef newType;
//     newType.name=name;
    
//     //TODO
//     return getType(newType.name);  //return a reference to the copy we just made in the vector, so that the caller can modify it if they want to add members etc.
//     //return registerType(newType); //register a copy of the new type and return a reference to the copy
// }
// typeDef& bglLanguageService::registerType(objectDef typeVal){
//     if(isObjectType(typeVal.name)) throw runtime_error(format("Declared type '{0}' already exists.", typeVal.name));
//     //objectTypes.push_back(typeVal); //copy the passed in type into the vector of registered types
//     return getType(typeVal.name);  //return a reference to the copy we just made in the vector, so that the caller can modify it if they want to add members etc.
// }
// typeDef& bglLanguageService::registerType(enumDef typeVal){
//     if(isObjectType(typeVal.name)) throw runtime_error(format("Declared type '{0}' already exists.", typeVal.name));
//     //objectTypes.push_back(typeVal); //copy the passed in type into the vector of registered types
//     return getType(typeVal.name);  //return a reference to the copy we just made in the vector, so that the caller can modify it if they want to add members etc.
// }
// Format a source location for "originally defined at ..." messages.
static string fmtSrc(const sourceLocation& src){
    if(src.file.empty()) return "unknown location";
    return format("{0}:{1}", src.file, src.line);
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
enumDef& bglLanguageService::registerEnum(string name, bool isExternal){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name)){
        enumDef* existing = dynamic_cast<enumDef*>(&getType(name));
        // Allow full pass to claim a pre-pass stub (values already populated by pre-scan)
        if(existing && existing->isPrePassStub){
            existing->isPrePassStub = false;
            existing->isExternal = isExternal;
            return *existing;
        }
        string loc = existing ? fmtSrc(existing->src) : "unknown location";
        parser.parsingError(format("'{0}' is already defined (originally declared at {1})", name, loc));
    }
    enumDef& newType=*(new enumDef());
    newType.name=name;
    newType.isExternal=isExternal;
    newType.src = parser.file.currentLocation();
    objectTypes.push_back(&newType);
    enumDef& retval=(enumDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
classDef& bglLanguageService::registerClass(string name, bool isExternal){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name)){
        classDef* existing = dynamic_cast<classDef*>(&getType(name));
        // Allow full pass to claim a pre-pass stub and populate its members
        if(existing && existing->isPrePassStub){
            existing->isPrePassStub = false;
            existing->isExternal = isExternal;
            return *existing;
        }
        string loc = existing ? fmtSrc(existing->src) : "unknown location";
        parser.parsingError(format("'{0}' is already defined (originally declared at {1})", name, loc));
    }
    classDef& newType=*(new classDef());
    newType.name=name;
    newType.isExternal=isExternal;
    newType.src = parser.file.currentLocation();
    objectTypes.push_back(&newType);
    classDef& retval=(classDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
objectDef& bglLanguageService::registerObject(string name, bool isExternal){
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    if(isObjectType(name)){
        objectDef* existing = dynamic_cast<objectDef*>(&getType(name));
        // Allow full pass to claim a pre-pass stub and populate its members;
        // the stub is already in globals so don't push again
        if(existing && existing->isPrePassStub){
            existing->isPrePassStub = false;
            existing->isExternal = isExternal;
            existing->src = parser.file.currentLocation();
            if(!isExternal && !existing->isExternal){
                // already in globals from pre-pass — don't push again
            }
            return *existing;
        }
        string loc = existing ? fmtSrc(existing->src) : "unknown location";
        parser.parsingError(format("'{0}' is already defined (originally declared at {1})", name, loc));
    }
    objectDef& newType=*(new objectDef());
    newType.name=name;
    newType.isExternal=isExternal;
    newType.src = parser.file.currentLocation();
    objectTypes.push_back(&newType);
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