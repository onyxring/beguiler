#pragma once
#include <string>
#include <sstream>
#include <deque>

#include "types.h"
#include "globals.h"
#include "token.h"
#include "fileLexer.h"
#include "i6Emitter.h"
#include "parseTreeNode.h"
#include "typeDef.h"

using namespace std;

class bglParser {
    public:
        fileLexer file;    //what the parser reads from.  Tokens are produced by the filelexer.
        
        //vector<typeDef*> globals;

        bglParser();        
        bool parseFile(std::string);    //the main entry point: given a file, read it in, parse it, and store it in the parse tree
        bool parsingError(std::string);   //called when there is an error, to output the error message and the place in the code where it appeared
        
        std::string contextToString(eCompileContext);

        //as we are parsing a file, we enter and exit "contexts" which help the parser determine what is and isn't valid.  For example, the global context allow different things than in the context of a routine.
        void openCompileContext(eCompileContext);   //entering a new context
        void closeCompileContext(eCompileContext);  //closing out the current context and returning to the previous
        eCompileContext getCurrentCompileContext(); //what is the the current context?
        
    private:
        std::deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        std::deque<eCompileContext> compileContextStack;     

        bool processNextStatement(abstractObject& =emptyContainer); 
        bool processDataType(token);
        bool processParameterList(functionDef&);

        bool processClassDeclaration(token);
        bool processEnumDeclaration(token);
        
        bool processObjectDeclaration(token, token);

        bool processVariableDeclaration(token, token, token, abstractObject& = emptyContainer);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer);
        bool processStatement(token, abstractObject& = emptyContainer);
        bool processDirective(token);
        
};

extern bglParser parser;