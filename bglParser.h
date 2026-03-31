#pragma once
#include <string>
#include <sstream>
#include <deque>
#include <set>
#include <map>
#include <functional>

#include "helpers.h"
#include "token.h"
#include "fileLexer.h"
#include "i6Emitter.h"
#include "typeDef.h"

using namespace std;

struct exitFileSignal {};   // thrown by #exit to unwind to the enclosing parseFile loop

class bglParser {
    public:
        fileLexer file;    //what the parser reads from.  Tokens are produced by the filelexer.
        bglParser();
        void preScanFile(string filename);  // pass 1: register type/object stubs for forward-reference resolution
        bool parseFile(string);    //the main entry point: given a file, read it in, parse it, and store it in the parse tree
        bool parsingError(string);   //called when there is an error, to output the error message and the place in the code where it appeared
        void applySchemaDefaults(); // apply beguilerSettingsType default values to any unset settings fields
        
        string contextToString(eCompileContext);

        //as we are parsing a file, we enter and exit "contexts" which help the parser determine what is and isn't valid.  For example, the global context allow different things than in the context of a routine.
        void openCompileContext(eCompileContext);   //entering a new context
        void closeCompileContext(eCompileContext);  //closing out the current context and returning to the previous
        eCompileContext getCurrentCompileContext(); //what is the the current context?
        string processBglConditionals(const string& text); // evaluates ##ifdef/##ifndef/##else/##endif in raw emitter body text

    private:
        deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        deque<eCompileContext> compileContextStack;
        set<string> onceFiles;        // absolute paths of files that declared #once
        set<string> startupFiles;     // absolute paths of files whose #startup blocks have been registered
        int includeDepth = 0;               // current include nesting depth
        static constexpr int maxIncludeDepth = 255;
        int forInCounter = 0;               // counter for unique _bglfiN variable names
        int lambdaCounter = 0;              // counter for unique _bglLambdaN function names
        int loopDepth = 0;                  // nesting depth of for/while/do loops (for continue validation)

        bool processNextStatement(abstractObject& =emptyContainer);
        bool processParameterList(functionDef&);

        bool processClassDeclaration(token, bool isExternal, bool isExtend=false, bool isEmitterClass=false, bool isAlias=false, token nameOverride=token());
        bool processEnumDeclaration(token, bool);
        bool processBeguilerSettings();
        
        bool processObjectDeclaration(token, token, bool, string className = "", string i6alias = "");
        void parsePropertyValue(variableDeclaration& prop, string typeName);
        void processI6InlineMember(objectDef& obj);
        void processArrayMember(objectDef& obj);
        void processTypedMember(objectDef& obj, token typeTok);
        void processMemberMethod(objectDef& obj, token returnType, token name);
        void processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue);
        void processInheritedMember(objectDef& obj, token nameTok);
        bool processVerbDeclaration(bool isExtern=false);
        bool processGrammarDeclaration();
        vector<grammarLine> parseGrammarLines();

        string parseFuncType();             // reads <ReturnType,ParamType,...> from stream; returns "func<...>"
        string parseLambdaExpr(functionDef* func, statementBlock* body);  // parses lambda, lifts to global, returns lifted name

        bool processVariableDeclaration(token, token, token, abstractObject& = emptyContainer, bool = false, bool = false, string i6alias = "");
        bool processArrayDeclaration(token, token, string, token, abstractObject& = emptyContainer, bool = false);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer, bool = false, bool = false, bool = false);
        bool processStatement(token, abstractObject& = emptyContainer);
        bool processDirective(token, abstractObject& = emptyContainer);

        expression* parseExpression(token firstToken, vector<string> terminators, functionDef* func, statementBlock* body);

        // Binary operator resolution in expression context: reads RHS, finds matching operator emitter,
        // applies conversion fallbacks, inlines emitter body into expr->tokens. Uses getNext/prefetched lambdas from parseExpression.
        // Returns true if the operator was handled (expr modified); false if not (caller should push raw).
        bool applyBinaryOperator(expression* expr, const string& opName, classDef* cls,
            const vector<string>& terminators, int parenDepth,
            function<token()> getNext, optional<token>& prefetched,
            functionDef* func, statementBlock* body);
        vector<interpolatedSegment> parseInterpolatedSegments(functionDef* func, statementBlock* body); // parses $"..." segments from the live stream (consumes $ and string)
        typeMember* findMemberInHierarchy(classDef* cls, function<bool(typeMember*)> pred);
        string resolveIdentifierType(string name, functionDef* func, statementBlock* body);
        string resolvePathType(string path, functionDef* func, statementBlock* body);
        string qualifyIdentifier(string name, functionDef* func, statementBlock* body);
        bool isTypeCompatible(string argType, string paramType);
        void applyArgConversions(vector<expression*>& args, functionDef* fd);

        // Unified method resolution: searches class hierarchy, then objectDef members (with self→currentObject),
        // handles pre-scan stubs, default params, var fallback. Returns nullptr if not found.
        struct MethodMatch {
            functionDef* method = nullptr;      // best matching method, or nullptr
            functionDef* nameMatch = nullptr;    // first method with matching name (for error context)
            functionDef* arityMatch = nullptr;   // first method with matching arity (for error context)
            bool nameFound = false;              // true if any method with the name exists
        };
        MethodMatch resolveMethod(const string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args);

        // Replace a pre-scan stub in a member list with the real definition. Returns true if replaced.
        static bool replaceStubMember(vector<typeMember*>& members, functionDef& newDef);

        objectDef* currentObject = nullptr;
        classDef* currentClass = nullptr;    // set when parsing inside a class declaration
        functionDef* currentFunc = nullptr;  // outermost function being parsed (not changed for nested if/while blocks)
        sourceLocation currentStatementSrc;  // location of the first token of the current statement

        map<string,string> definedSymbols;  // symbols defined via #define; value is "" for boolean flags, else the literal value
        bool evaluateCondition(const string& expr);   // evaluates a #if boolean expression
        void skipConditionalBlock(abstractObject& ctx);    // skips tokens until #elif/#else/#endif at depth 0
        void skipBglConditionalBlock(abstractObject& ctx); // skips tokens until ##else/##endif at depth 0

        // Pre-scanner state
        set<string> preScanOnceFiles;
        int preScanDepth = 0;
        void preScanDirective(token tok);
        void preScanSkipBody();           // consumes opening '{' and everything through matching '}'
        void preScanSkipBodyContents();   // assumes '{' already consumed; skips to matching '}'
        void preScanSkipToSemicolon();    // consumes tokens up to and including ';'
        void preScanSkipParens();         // assumes '(' already consumed; skips to matching ')'

        vector<statement*> pendingInjections;  // pre-statements to emit before next main statement (e.g. from ternary lowering)
        vector<statement*> postInjections;     // post-statements to emit after next main statement (e.g. closing braces for ?. guards)

};

extern bglParser parser;