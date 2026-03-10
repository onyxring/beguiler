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
        vector<pair<string,string>>* currentCleanups = nullptr; // set during emitFunction; used by emitStatement for return
        vector<tuple<int,string,int>> sourceMap;  // (i6Line, bglFile, bglLine)
        void to(ostream&);

        void emit(vector<typeDef*>&);
        void generateI6(typeDef*);
        void emitICL(beguilerSettingsDef*);
        void emitSettingsConstants(beguilerSettingsDef*);
        void emitEnum(enumDef*);
        void emitClass(classDef*);
        void emitObject(objectDef*);
        void emitMember(typeMember*);
        void emitGlobal(variableDeclaration*);
        void emitFunction(functionDef*);
        void emitStatement(statement*, string indent);
        int currentLine();
        void writeSourceMap(const string& path);

};

extern i6Emitter emitter;