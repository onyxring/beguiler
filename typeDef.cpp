#include "typeDef.h"
#include "parseTreeNode.h"


abstractObject emptyContainer;

bool abstractObject::operator ==(abstractObject other){
    return other.name==name; 
}

// abstractObject::operator parseTreeNode(){
//     parseTreeNode retval;
//     retval.element=*this;
//     return retval;
// }
// void typeDef::registerNewMember(typeMember member){
//     // for(memberDef &m:members){
//     //     if(m.name==func.name) throw runtime_error(format("Member '{0}' is already defined for type '{1}'.", func.name, name));
//     // }
//     // members.push_back(func);
// }