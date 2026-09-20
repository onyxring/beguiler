#include "platform.h"
#include "typeDef.h"

abstractObject emptyContainer;

bool abstractObject::operator ==(abstractObject other){
    return other.name==name; 
}


// ---- classDef hierarchy walks ----------------------------------------------------------------
// Shared traversal order for all of these: this class's own `members` in declaration order, then
// each entry of `baseClasses` in order, recursively (depth-first, first-listed base first).

typeMember* classDef::findMember(const std::function<bool(typeMember*)>& pred){
    for(typeMember* m : members)
        if(pred(m)) return m;
    for(classDef* base : baseClasses){
        typeMember* found = base->findMember(pred);
        if(found) return found;
    }
    return nullptr;
}

void classDef::forEachMember(const std::function<void(typeMember*)>& fn){
    for(typeMember* m : members)
        fn(m);
    for(classDef* base : baseClasses)
        base->forEachMember(fn);
}

bool classDef::hasAncestor(const classDef* ancestor) const{
    if(ancestor == nullptr || ancestor == this) return false;
    for(const classDef* base : baseClasses){
        if(base == ancestor) return true;
        if(base != nullptr && base->hasAncestor(ancestor)) return true;
    }
    return false;
}

classDef* classDef::declaringClassOf(const typeMember* m){
    for(typeMember* own : members)
        if(own == m) return this;
    for(classDef* base : baseClasses)
        if(classDef* r = base->declaringClassOf(m)) return r;
    return nullptr;
}
