#include "platform.h"
#include "typeDef.h"

abstractObject emptyContainer;

bool abstractObject::operator ==(abstractObject other){
    return other.name==name; 
}

