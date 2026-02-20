#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <vector>

#include "bglLanguageService.h"
#include "typeDef.h"

using namespace std;

bool bglLanguageService::isObjectType(string name){
    if(getType(name)==_objectTypeNotFound) return false;
    return true;
}
typeDef bglLanguageService::getType(string name){
    for (typeDef &ot : objectTypes) {
        if(ot.name==name) return ot;
    }
    return _objectTypeNotFound;
}
typeDef bglLanguageService::addObjectType(string name){
    if(isObjectType(name)) throw runtime_error(format("Declared type '{0}' already exists.", name));
    typeDef objType;
    objType.name=name;    
    objectTypes.push_back(objType);
    return objType;
}
void bglLanguageService::registerEnum(enumDef& enumVal){
    if(isObjectType(enumVal.name)) throw runtime_error(format("Declared type '{0}' already exists.", enumVal.name));
    
    objectTypes.push_back(enumVal);
}
