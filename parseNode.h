#pragma once

#include <string>
#include <map>
#include <variant>

#include "token.h"

using namespace std;
enum class eNodeType{
    root,
    directive,
    integer,
    symbol,
    quote,
    variableDeclaration,
    constantDeclaration,
    objectDeclaration,
    classDeclaration,
    parameterListDeclaration,
    routine,
    executableStatement,
    //expression,
};

class parseNode {
    public:
        eNodeType type;
        token keyToken;

        parseNode* parent;
        std::vector<parseNode> children;
        
        std::map<std::string, parseNode> properties;
        parseNode& operator[](std::string);
        int getNodeNestingDepth();

        operator token(); 
        operator std::string(); 
        void mapParents(parseNode* parent=NULL);
        void addChild(parseNode);

};

