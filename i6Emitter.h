#pragma once
#include <string>
#include <sstream>
#include <stack>
#include "token.h"
#include "typeDef.h"

using namespace std;

class i6Emitter{
    public:
        stringstream out; 
        void to(ostream&);

        // //void globalFunction(token, token);
        // //void functionParams();
        // void functionCall(token, token=_nullToken);
        // void enumOrFlags(token,bool);
        // void directive(parseTreeNode&);
        // //void globalVariable(token, token, token= _nullToken);
        // void variable(token, token, token= _nullToken);
        // void put(std::string);
        // void put(token);
        // void indent(parseTreeNode&, int=0);
        // void indent(int=0);
        // void newLine();
        // void endStatement();
        // void executableStatement(parseTreeNode&);
        // void objectDeclaration(parseTreeNode&);
        
        // void emit(vector<globalDef>&);
        // void emitEnum(enumDef&);
        // void emitClass(classDef&);
        // void emitObject(objectDef&);
        // void emitFunction(functionDef&);
        void emit(vector<typeDef*>&);
        void generateI6(typeDef&);

        // void rootNode(parseTreeNode&);
};

extern i6Emitter emitter;