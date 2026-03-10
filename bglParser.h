#pragma once
#include <string>
#include <sstream>
#include <deque>
#include <set>
#include <map>

#include "types.h"
#include "helpers.h"
#include "token.h"
#include "fileLexer.h"
#include "i6Emitter.h"
#include "typeDef.h"

using namespace std;

class bglParser {
    public:
        fileLexer file;    //what the parser reads from.  Tokens are produced by the filelexer.
        
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
        std::set<std::string> loadedFiles;  // tracks absolute paths of already-loaded files (include guard)

        bool processNextStatement(abstractObject& =emptyContainer); 
        //bool processDataType(token);
        bool processParameterList(functionDef&);

        bool processClassDeclaration(token, bool, bool isExtend=false);
        bool processEnumDeclaration(token, bool);
        bool processBeguilerSettings();
        
        bool processObjectDeclaration(token, token, bool);

        bool processVariableDeclaration(token, token, token, abstractObject& = emptyContainer, bool = false, bool = false);
        bool processArrayDeclaration(token, token, std::string, token, abstractObject& = emptyContainer, bool = false);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer, bool = false, bool = false);
        bool processStatement(token, abstractObject& = emptyContainer);
        bool processDirective(token, abstractObject& = emptyContainer);

        expression* parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body);
        std::string resolveIdentifierType(std::string name, functionDef* func, statementBlock* body);
        std::string resolvePathType(std::string path, functionDef* func, statementBlock* body);
        std::string qualifyIdentifier(std::string name, functionDef* func, statementBlock* body);
        bool isTypeCompatible(std::string argType, std::string paramType);
        void applyArgConversions(std::vector<expression*>& args, functionDef* fd);

        objectDef* currentObject = nullptr;
        classDef* currentClass = nullptr;    // set when parsing inside a class declaration
        functionDef* currentFunc = nullptr;  // outermost function being parsed (not changed for nested if/while blocks)

        std::map<std::string,std::string> definedSymbols;  // symbols defined via #define; value is "" for boolean flags, else the literal value
        bool evaluateCondition(const std::string& expr);  // evaluates a #if boolean expression
        void skipConditionalBlock(abstractObject& ctx);   // skips tokens until #elif/#else/#endif at depth 0

};

extern bglParser parser;