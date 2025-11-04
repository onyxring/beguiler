#pragma once
#include <string>
#include <sstream>
#include <deque>

#include "token.h"
#include "fileLexer.h"
#include "emitter.h"
#include "parseNode.h"

using namespace std;

struct resultsStruct{
    std::stringstream tempText;
    std::stringstream bodyText;
};

enum class eCompileLanguage {beguile, i6};
enum class eCompileScope {root, classDef, codeBlock, languageBlock};


class parser {
    public:
        emitter emit;
        resultsStruct results;
        fileLexer file;

        std::vector<std::string> objects;
        std::vector<std::string> routines;
 
        parser();
        bool parseFile(std::string);
        bool parseError(std::string);
        void processFunctionBody(token);
        eCompileScope resolveCurrentCompileScope(); 
        
        void openCompileScope(eCompileScope); 
        void closeCompileScope(); 
        int getScopeNestingDepth();
        
        parseNode parseTree;
        parseNode& getCurrentNode();

    private:
        std::deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        std::deque<eCompileScope> compileScopeStack; 
        
        void emitTo(std::ostream&);
        eCompileLanguage getCurrentLanguage(); 

        //old...
        //bool processNextStatement();
        void processFunctionParams();
        void processEnumOrFlags(token, bool);
        void processFunctionCall(token, token=_nullToken);
        bool getArgumentExpression(std::string&);
        void processI6();
        void registerNewObjectType(std::string);
        void registerNewRoutine(std::string);
        void emitVariable(token, token, token= _nullToken);

        //new...
        std::deque<parseNode*> currentNodeStack;
        void pushCurrentNode(parseNode&);
        void popCurrentNode();
        void commitNode(parseNode); //not a reference; commit a copy of the node
        
        bool processNextStatement();
        bool processDataType(token);
        bool processVariableDeclaration(token, token, token);
        bool processRoutineDeclaration(token, token);
        bool processObjectDeclaration(token, token);
        bool processStatement(token);
        bool processObjectType(token);
        bool processDirective(token);
        
};
