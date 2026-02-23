#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

#include "globals.h"
#include "bglParser.h"
#include "bglLanguageService.h"
#include "typeDef.h"

using namespace std;

typeDef emptyTDef;

bglLanguageService::bglLanguageService(){
    registerType("void");
    registerType("var");
}
bool bglLanguageService::isObjectType(string name){
    if(getType(name)==emptyTDef) return false;
    return true;
}
typeDef& bglLanguageService::getType(string name){
    for (auto& ot : objectTypes) {
        if(ot->name==name) return *ot;
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
    typeDef baseType;
    baseType.name=name;
    objectTypes.push_back(make_unique<typeDef>(baseType)); //copy the passed in type into the vector of registered types    
    typeDef& retval=(typeDef&)getType(name); 
    if(parser.getCurrentCompileContext()==eCompileContext::global) globals.push_back(&retval);
    return retval; 
}
classDef& bglLanguageService::registerType(classDef& typeVal){
    if(isObjectType(typeVal.name)) throw runtime_error(format("Declared type '{0}' already exists.", typeVal.name));
    objectTypes.push_back(make_unique<classDef>(typeVal)); //copy the passed in type into the vector of registered types    
    classDef& retval=(classDef&)getType(typeVal.name); 
    if(parser.getCurrentCompileContext()==eCompileContext::global) globals.push_back(&retval);
    return retval; 
}
enumDef& bglLanguageService::registerType(enumDef& typeVal){
    if(isObjectType(typeVal.name)) throw runtime_error(format("Declared type '{0}' already exists.", typeVal.name));
    objectTypes.push_back(make_unique<enumDef>(typeVal)); //copy the passed in type into the vector of registered types        
    enumDef& retval=(enumDef&)getType(typeVal.name); 
    if(parser.getCurrentCompileContext()==eCompileContext::global) globals.push_back(&retval);
    return retval; 
}
objectDef& bglLanguageService::registerType(objectDef& typeVal){
    if(isObjectType(typeVal.name)) throw runtime_error(format("Declared type '{0}' already exists.", typeVal.name));
    objectTypes.push_back(make_unique<objectDef>(typeVal)); //copy the passed in type into the vector of registered types    
    objectDef& retval=(objectDef&)getType(typeVal.name); 
    if(parser.getCurrentCompileContext()==eCompileContext::global) globals.push_back(&retval);
    return retval; 
}
bglLanguageService languageService;