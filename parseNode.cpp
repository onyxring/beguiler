#include <string>
#include "bglParser.h"

using namespace std;

void parseNode::addChild(parseNode node){
    node.parent=this;
    children.push_back(node);
}
eCompileContext parseNode::resolveContext(){
    if(parent->type==eNodeType::root) return eCompileContext::global; 
    if(parent->type==eNodeType::classDeclaration || parent->type==eNodeType::objectDeclaration) return eCompileContext::objectDef; 
    if(parent->type==eNodeType::routine) return eCompileContext::codeBlock; 
    
    return eCompileContext::codeBlock; //TODO: really should throw an error here, instead of assuming code.

}
void parseNode::mapParents(parseNode* parent){
    if(this->type==eNodeType::root)
        this->parent=NULL;
    else
        this->parent=parent;

    for (auto& prop : properties) {
        prop.second.mapParents(this);
    }
    for(parseNode& child : children){
        child.mapParents(this);
    }
}
int parseNode::resolveNestingDepth(){
    int depth=0;
    parseNode* current=this;
    while(current !=0 && current->type!=eNodeType::root){
        depth++;
        current=current->parent;
    }
    return max(depth-1,0); //don't count the root node
}
parseNode::operator token(){
    return keyToken;
}
parseNode::operator string(){
    return (string)keyToken;
}
parseNode& parseNode::operator[](string key){
    return properties[key];
    //parseNode& retval=properties[key];   //did nothing
    //retval.parent=this;
    //return retval;
}
