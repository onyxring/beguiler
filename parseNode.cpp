#include <string>
#include "parser.h"

using namespace std;

void parseNode::addChild(parseNode node){
    //node.parent=this;
    children.push_back(node);
}

int parseNode::getNodeNestingDepth(){
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
