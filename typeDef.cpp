#include "typeDef.h"


bool typeDef::operator ==(typeDef other){
    return other.name==name; 
}
void typeDef::registerNewMember(typeMember member){
    // for(memberDef &m:members){
    //     if(m.name==func.name) throw runtime_error(format("Member '{0}' is already defined for type '{1}'.", func.name, name));
    // }
    // members.push_back(func);
}