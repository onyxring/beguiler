#pragma once
#include <string>
#include <sstream>
#include <deque>
#include <set>
#include <map>
#include <functional>

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
        void preScanFile(std::string filename);  // pass 1: register type/object stubs for forward-reference resolution
        bool parseFile(std::string);    //the main entry point: given a file, read it in, parse it, and store it in the parse tree
        bool parsingError(std::string);   //called when there is an error, to output the error message and the place in the code where it appeared
        
        std::string contextToString(eCompileContext);

        //as we are parsing a file, we enter and exit "contexts" which help the parser determine what is and isn't valid.  For example, the global context allow different things than in the context of a routine.
        void openCompileContext(eCompileContext);   //entering a new context
        void closeCompileContext(eCompileContext);  //closing out the current context and returning to the previous
        eCompileContext getCurrentCompileContext(); //what is the the current context?
        std::string processBglConditionals(const std::string& text); // evaluates ##ifdef/##ifndef/##else/##endif in raw emitter body text

    private:
        std::deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        std::deque<eCompileContext> compileContextStack;
        std::set<std::string> onceFiles;    // absolute paths of files that declared #once
        int includeDepth = 0;               // current include nesting depth
        static constexpr int maxIncludeDepth = 255;
        int forInCounter = 0;               // counter for unique _bglfiN variable names
        int lambdaCounter = 0;              // counter for unique _bglLambdaN function names

        bool processNextStatement(abstractObject& =emptyContainer);
        //bool processDataType(token);
        bool processParameterList(functionDef&);

        bool processClassDeclaration(token, bool isExternal, bool isExtend=false, bool isEmitterClass=false, bool isAlias=false);
        bool processEnumDeclaration(token, bool);
        bool processBeguilerSettings();
        
        bool processObjectDeclaration(token, token, bool, std::string className = "");
        void parsePropertyValue(variableDeclaration& prop, std::string typeName);
        void processI6InlineMember(objectDef& obj);
        void processArrayMember(objectDef& obj);
        void processTypedMember(objectDef& obj, token typeTok);
        void processMemberMethod(objectDef& obj, token returnType, token name);
        void processMemberVariable(objectDef& obj, std::string typeName, std::string name, bool hasValue);
        void processInheritedMember(objectDef& obj, token nameTok);
        bool processVerbDeclaration(bool isExtern=false);
        bool processGrammarDeclaration();
        std::vector<grammarLine> parseGrammarLines();

        string parseFuncType();             // reads <ReturnType,ParamType,...> from stream; returns "func<...>"
        string parseLambdaExpr(functionDef* func, statementBlock* body);  // parses lambda, lifts to global, returns lifted name

        bool processVariableDeclaration(token, token, token, abstractObject& = emptyContainer, bool = false, bool = false);
        bool processArrayDeclaration(token, token, std::string, token, abstractObject& = emptyContainer, bool = false);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer, bool = false, bool = false, bool = false);
        bool processStatement(token, abstractObject& = emptyContainer);
        bool processDirective(token, abstractObject& = emptyContainer);

        expression* parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body);
        typeMember* findMemberInHierarchy(classDef* cls, std::function<bool(typeMember*)> pred);
        std::string resolveIdentifierType(std::string name, functionDef* func, statementBlock* body);
        std::string resolvePathType(std::string path, functionDef* func, statementBlock* body);
        std::string qualifyIdentifier(std::string name, functionDef* func, statementBlock* body);
        bool isTypeCompatible(std::string argType, std::string paramType);
        void applyArgConversions(std::vector<expression*>& args, functionDef* fd);

        objectDef* currentObject = nullptr;
        classDef* currentClass = nullptr;    // set when parsing inside a class declaration
        functionDef* currentFunc = nullptr;  // outermost function being parsed (not changed for nested if/while blocks)

        std::map<std::string,std::string> definedSymbols;  // symbols defined via #define; value is "" for boolean flags, else the literal value
        bool evaluateCondition(const std::string& expr);   // evaluates a #if boolean expression
        void skipConditionalBlock(abstractObject& ctx);    // skips tokens until #elif/#else/#endif at depth 0
        void skipBglConditionalBlock(abstractObject& ctx); // skips tokens until ##else/##endif at depth 0

        // Pre-scanner state
        std::set<std::string> preScanOnceFiles;
        int preScanDepth = 0;
        void preScanDirective(token tok);
        void preScanSkipBody();           // consumes opening '{' and everything through matching '}'
        void preScanSkipBodyContents();   // assumes '{' already consumed; skips to matching '}'
        void preScanSkipToSemicolon();    // consumes tokens up to and including ';'
        void preScanSkipParens();         // assumes '(' already consumed; skips to matching ')'

        std::vector<statement*> pendingInjections;  // pre-statements to emit before next main statement (e.g. from ternary lowering)

};

extern bglParser parser;