#pragma once
#include <string>
#include <sstream>
#include <stack>
#include <set>
#include <map>
#include <vector>
#include "token.h"
#include "typeDef.h"

using namespace std;

class i6Emitter{
    public:
        stringstream out;
        vector<pair<string,string>>* currentCleanups = nullptr; // set during emitFunction; used by emitStatement for return
        vector<tuple<int,string,int>> sourceMap;  // (i6Line, bglFile, bglLine)
        set<string> declaredVerbWords;             // tracks which I6 trigger words have been Verb-declared
        // built-in I6 templates loaded from beguilib/_builtins.i6b
        map<string, pair<vector<string>, string>> builtinTemplates;
        void loadBuiltinTemplates(string path);
        void applyTemplate(string name, map<string,string> args, string indent);
        void to(ostream&);

        // Z-machine local variable spill state — active during a single function's emission
        string currentTarget;                              // lowercase: "glulx","z3","z5","z8"
        map<string,string> currentSpillAliases;            // varName → "_bglFrm-->N" or "_bglXPn"
        int currentSpillCount = 0;
        bool frameAllocEmitted = false;
        int xpGlobalsNeeded = 0;                           // how many _bglXPn globals were emitted
        int framePoolSize = 64;                            // configurable via beguilerSettings framePoolSize

        static string replaceWord(string str, const string& from, const string& to);
        void buildSpillMap(functionDef* fd);
        void clearSpillMap();
        string exprText(expression* expr);
        string spillName(const string& name);
        string spillWord(const string& text);
        bool funcNeedsSpill(functionDef* fd);

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
        void emitVerbObject(verbObjectDef*);
        void emitGrammarBlock(grammarBlock*);
        void emitGrammarLines(const string& verbName, const vector<grammarLine>& lines);
        int currentLine();
        void writeSourceMap(const string& path);
        void writeSymbolTable(const string& path);
        void writeTypesFile(const string& path);
        void writeDebugBundle(const string& path);

};

extern i6Emitter emitter;