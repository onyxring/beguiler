#pragma once

#include <string>
#include <map>
#include <variant>

#include "globals.h"
#include "token.h"

using namespace std;

class parseNode {
    public:
        eNodeType type;
        token keyToken;

        parseNode* parent;
        std::vector<parseNode> children;
        
        std::map<std::string, parseNode> properties;
        parseNode& operator[](std::string);
        
        operator token(); 
        operator std::string(); 
        void mapParents(parseNode* parent=NULL);
        void addChild(parseNode);
        
        
        eCompileContext resolveContext();
        int resolveNestingDepth();

};

