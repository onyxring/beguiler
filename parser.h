#pragma once
#include <string>
#include <sstream>
#include <deque>

#include "token.h"
#include "fileLexer.h"
#include "emitter.h"
#include "parseNode.h"

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
 
        parser();
        bool parseFile(std::string);
        bool parseError(std::string);
        void processFunctionBody(token);
        eCompileScope resolveCurrentCompileScope(); 
        
        void openCompileScope(eCompileScope); 
        void closeCompileScope(); 
        int getScopeNestingDepth();
        
        parseNode parseTree;
        parseNode& currentParseTreeNode;

    private:
        std::deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        std::deque<eCompileScope> compileScopeStack; 
        
        void emitTo(std::ostream&);
        eCompileLanguage getCurrentLanguage(); 

        //old...
        bool processNextStatement();
        void processFunctionParams();
        void processEnumOrFlags(token, bool);
        void processFunctionCall(token, token=_nullToken);
        bool getArgumentExpression(std::string&);
        void processI6();
        void registerNewObjectType(std::string);
        
        //new...
        bool serializeNextStatement();
        bool processObjectType(token);
        bool processDataType(token);
        bool processDirective(token);
        bool processRoutine(token, token);

        void emitVariable(token, token, token= _nullToken);
        void dumpTree(parseNode);
        
};
