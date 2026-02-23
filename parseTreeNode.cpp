// #include <string>
// #include "bglParser.h"

// using namespace std;

// void parseTreeNode::addChild(parseTreeNode node){
//      node.parent=this;
//      children.push_back(node);
// }
// // eCompileContext parseTreeNode::resolveContext(){
// //     if(parent->type==eNodeType::root) return eCompileContext::global; 
// //     if(parent->type==eNodeType::classDeclaration || parent->type==eNodeType::objectDeclaration) return eCompileContext::objectDef; 
// //     if(parent->type==eNodeType::routine) return eCompileContext::codeBlock; 
    
// //     return eCompileContext::codeBlock; //TODO: really should throw an error here, instead of assuming code.

// // }
// // void parseTreeNode::mapParents(parseTreeNode* parent){
// //     if(this->type==eNodeType::root)
// //         this->parent=NULL;
// //     else
// //         this->parent=parent;

// //     for (auto& prop : properties) {
// //         prop.second.mapParents(this);
// //     }
// //     for(parseTreeNode& child : children){
// //         child.mapParents(this);
// //     }
// // }
// // int parseTreeNode::resolveNestingDepth(){
// //     int depth=0;
// //     parseTreeNode* current=this;
// //     while(current !=0 && current->type!=eNodeType::root){
// //         depth++;
// //         current=current->parent;
// //     }
// //     return max(depth-1,0); //don't count the root node
// // }
// // parseTreeNode::operator token(){
// //     return keyToken;
// // }
// // parseTreeNode::operator string(){
// //     return (string)keyToken;
// // }
// // parseTreeNode& parseTreeNode::operator[](string key){
// //     return properties[key];
// //     //parseTreeNode& retval=properties[key];   //did nothing
// //     //retval.parent=this;
// //     //return retval;
// // }
