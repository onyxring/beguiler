#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include <vector>

using namespace std;

class typeMember{
    public:
        string name;        
};
class typeDef{
    public:
        string name;
        bool operator == (typeDef); 
        vector<typeMember> members;
        void registerNewMember(typeMember);
};
class classDef: public typeDef{
    public:
        vector<classDef> baseClasses;
};
class objectDef: public typeDef{
    public:
        vector<classDef> baseClasses;
       
};
class memberVariable: public typeMember{
    public:
        typeDef type;
};
class paramDef{
    public:
        string name;
        typeDef type;
        //todo: add default values here
        
};


class statement{

};
class codeBlock{
    public:

};

class i6Block:public codeBlock{
    public:
        string i6Body;
};  
class statementBlock:public codeBlock{
    public:
        vector<statement> statements;
};
class assignmentStatement:public statement{
    public:
        string variableLeft; //todo: probably should have a variable table for scope
        string assignedExpression;
};  
class returnStatement:public statement{
    public:
        string returnExpression;
};  
class variableDeclaration:public statement{
    public:
    typeDef type;    
    string name; 
    string declaredExpressionValue;
};  
class functionCall:public statement{
    public:
        string functionName; 
        vector<paramDef> params;
};  
class memberFunction: public typeMember{
    public:
        bool isEmitter;
        typeDef returnType;
        vector<paramDef> params;
        codeBlock body;
        
};
class enumValueDef{
    public:
    string name;
    int value;
};
class enumDef:public typeDef{
    public:
        vector<enumValueDef> namedValues;        
};

extern typeDef _objectTypeNotFound;

