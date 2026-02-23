#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include <vector>
//#include "1eNode.h"

using namespace std;

//base class for all elements in the language.
class abstractObject{
    public:
        string name;
        bool operator == (abstractObject);
        virtual void dummy() {} //TODO: this was an experiment and might be able to be removed 
        
};
//things defined at the global level, such as type definitions, objects, enums, and functions and global variables.
class globalDef:virtual public abstractObject{    
};
//members of types, including functions and variables.
class typeMember:virtual public abstractObject{   
};

//types definitions 
class typeDef:public globalDef{
    public:
};
//class definitions 
class classDef:public typeDef{
    public:
        //vector<unique_ptr<typeDef>> baseClasses;
        //vector<unique_ptr<typeMember>> members;
        
        vector<typeDef> baseClasses;
        vector<typeMember> members;
        //void registerNewMember(typeMember);
};
//instances of classes, including overrides
class objectDef: public typeDef{
    public:
        vector<typeDef> baseClasses;
        vector<typeMember> members;
};

//a parameter of a function
class paramDef:public abstractObject{
    public:
        globalDef type;
        //todo: add default values here
        
};

//the function body
class codeBlock:public abstractObject{
    public:

};

//a specific type of function body, containing raw I6 code to be emitted directly into the output file, without any processing by Beguile.
class i6Block:public codeBlock{
    public:
        string i6Body;
};  
//a base clase for an individual statement which appears in blocks of code
class statement:virtual public abstractObject{
};

//a block of statements, essentiall the body of a function
class statementBlock:public codeBlock{
    public:
        vector<statement> statements;
};
//a type of statement which assigns a value to a variable
class assignmentStatement:public statement{
    public:
        string variableLeft; //todo: probably should have a variable table for scope
        string assignedExpression;
};  
//a type of statement which returns a value from a function
class returnStatement:public statement{
    public:
        string returnExpression;
};  
//the declaration of a variable.  This may be a global variable, an object member, or a local variable within a function.
class variableDeclaration:public typeMember, public statement, public globalDef{
    public:
        globalDef type;    
        string declaredExpressionValue;
};  
//a type of statement which represents a function call. 
class functionCall:public statement{
    public:
        vector<paramDef> params;
};  
//the declaration of a function.  This may be a global function, or an object member
class functionDef:public typeMember, public typeDef{
    public:
        bool isEmitter;
        globalDef returnType;
        vector<paramDef> params;
        codeBlock body;
    
    using abstractObject::name;
        
};
//an individual value within an enum definition
class enumValueDef:public abstractObject{
    public:
    int value;
};
//the declaration of an enum type
class enumDef:public typeDef{
    public:
        vector<enumValueDef> namedValues;        
};

extern typeDef emptyTDef;
extern abstractObject emptyContainer;
