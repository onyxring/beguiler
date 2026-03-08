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

bglLanguageService::bglLanguageService(){
    registerType("void");
    registerType("var");
    registerType("intLiteral");
    registerType("stringLiteral");
}
bool bglLanguageService::isObjectType(string name){ 
    if(getType(name)==emptyTDef) return false;
    return true;
}
typeDef& bglLanguageService::getType(string name){
    for (typeDef* ot : objectTypes) {
        if(ot->name ==name) return *ot;
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
typeDef& bglLanguageService::registerType(string name){
    if(isObjectType(name)) throw runtime_error(format("Declared type '{0}' already exists.", name));
    typeDef& baseType=*(new typeDef());
    baseType.name=name;
    objectTypes.push_back(&baseType); //copy the passed in type into the vector of registered types    
    typeDef& retval=(typeDef&)getType(name); 
    if(parser.getCurrentCompileContext()==eCompileContext::global && baseType.isExternal==false) globals.push_back(&retval);
    return retval; 
}
enumDef& bglLanguageService::registerEnum(string name, bool isExternal){
    if(isObjectType(name)) throw runtime_error(format("Declared type '{0}' already exists.", name));
    enumDef& newType=*(new enumDef());
    newType.name=name;
    newType.isExternal=isExternal;
    objectTypes.push_back(&newType);
    enumDef& retval=(enumDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
classDef& bglLanguageService::registerClass(string name, bool isExternal){
    if(isObjectType(name)) throw runtime_error(format("Declared type '{0}' already exists.", name));
    classDef& newType=*(new classDef());
    newType.name=name;
    newType.isExternal=isExternal;
    objectTypes.push_back(&newType);
    classDef& retval=(classDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
objectDef& bglLanguageService::registerObject(string name, bool isExternal){
    if(isObjectType(name)) throw runtime_error(format("Declared type '{0}' already exists.", name));
    objectDef& newType=*(new objectDef());
    newType.name=name;
    newType.isExternal=isExternal;
    objectTypes.push_back(&newType);
    objectDef& retval=(objectDef&)getType(name);
    if(parser.getCurrentCompileContext()==eCompileContext::global && !isExternal) globals.push_back(&retval);
    return retval;
}
variableDeclaration& bglLanguageService::registerInstance(variableDeclaration& varDef){
    //if(isObjectType(varDef.name)) throw runtime_error(format("Declared type '{0}' already exists.", varDef.name));
    //objectTypes.push_back(&varDef); //copy the passed in type into the vector of registered types    
    //variableDeclaration& retval=(variableDeclaration&)getType(varDef.name); 
    //if(parser.getCurrentCompileContext()==eCompileContext::global&& variableDeclaration.isExternal==false) 
    globals.push_back(&varDef);
    return varDef;
}

string bglLanguageService::getEnumType(string valueName){
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