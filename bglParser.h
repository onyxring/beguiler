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
        void parsingWarning(string); //like parsingError but continues parsing
        void applySchemaDefaults(); // apply beguilerSettingsType default values to any unset settings fields
        void defineSymbol(const string& name, const string& value = ""){ definedSymbols[name] = value; }

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
        int ternaryDepth = 0;               // nesting depth of ternary expressions (max 1)

        bool processNextStatement(abstractObject& =emptyContainer);
        bool processParameterList(functionDef&);

        bool processClassDeclaration(token, bool isExternal, bool isExtend=false, bool isEmitterClass=false, bool isAlias=false, token nameOverride=token());
        bool processEnumDeclaration(token, bool);
        bool processBeguilerSettings();
        
        bool processObjectDeclaration(token, token, bool, string className = "", string i6alias = "");
        bool processObjectExtension(token nameTok);  // extend <objectName> { }
        void processExtendCompoundAssignment(objectDef& obj, token memberName, const string& op, verbObjectDef* vod);
        void parsePropertyValue(variableDeclaration& prop, string typeName);
        void processI6InlineMember(objectDef& obj);
        void processArrayMember(objectDef& obj);
        void processTypedMember(objectDef& obj, token typeTok, bool isReplace = false);
        void processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace = false);
        void processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue, bool isReplace = false);
        void processInheritedMember(objectDef& obj, token nameTok);
        bool processGrammarDeclaration();
        bool processGrammarObjectDeclaration(const string& name);  // grammar object with grammarRule members
        vector<grammarLine> parseGrammarLines();
        grammarLine parseGrammarLineContent();  // parses single grammar line (trigger + pattern tokens); assumes '{' consumed

        string parseFuncType();             // reads <ReturnType,ParamType,...> from stream; returns "func<...>"
        string parseLambdaExpr(functionDef* func, statementBlock* body);  // parses lambda, lifts to global, returns lifted name

        bool processVariableDeclaration(token, token, token, abstractObject& = emptyContainer, bool = false, bool = false, string i6alias = "");
        bool processArrayDeclaration(token, token, string, token, abstractObject& = emptyContainer, bool = false);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer, bool = false, bool = false, bool = false);
        bool processStatement(token, abstractObject& = emptyContainer);
        bool processDirective(token, abstractObject& = emptyContainer);

        expression* parseExpression(token firstToken, vector<string> terminators, functionDef* func, statementBlock* body);
        // parseExpression sub-functions (extracted for readability)
        void parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body);
        void parseExprNullCoalescing(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body);
        bool parseExprFunctionCall(expression* expr, const string& callName, bool isSelfCall, functionDef* func, statementBlock* body);
        bool parseExprPrefixNot(expression* expr, token operand, optional<token>& prefetched, functionDef* func, statementBlock* body);

        // Binary operator resolution in expression context: reads RHS, finds matching operator emitter,
        // applies conversion fallbacks, inlines emitter body into expr->tokens. Uses getNext/prefetched lambdas from parseExpression.
        // Returns true if the operator was handled (expr modified); false if not (caller should push raw).
        bool applyBinaryOperator(expression* expr, const string& opName, classDef* cls,
            const vector<string>& terminators, int parenDepth,
            function<token()> getNext, optional<token>& prefetched,
            functionDef* func, statementBlock* body);
        vector<interpolatedSegment> parseInterpolatedSegments(functionDef* func, statementBlock* body); // parses $"..." segments from the live stream (consumes $ and string)
        typeMember* findMemberInHierarchy(classDef* cls, function<bool(typeMember*)> pred);

        // Qualifier flags parsed from declaration prefixes (replace, explicit, extern, emitter, const, static, extend, alias, default).
        // Parsed in any order via parseQualifiers(); validated for nonsensical combinations.
        struct Qualifiers {
            bool isReplace  = false;
            bool isExplicit = false;
            bool isExtern   = false;
            bool isEmitter  = false;
            bool isConst    = false;
            bool isStatic   = false;
            bool isExtend   = false;
            bool isAlias    = false;
            bool isDefault  = false;
        };
        // Parse qualifier keywords from the token stream in any order. Consumes qualifying tokens,
        // leaves tok pointing at the first non-qualifier. Validates invalid combinations.
        Qualifiers parseQualifiers(token& tok);
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

        // Shared argument list parsing: reads comma-separated expressions from stream (assumes '(' already consumed).
        // Returns parsed args, named arg names, and per-arg interpolated segments.
        struct ParsedArgList {
            vector<expression*> args;
            vector<string> namedArgNames;
            vector<vector<interpolatedSegment>> interpSegmentsPerArg;
        };
        ParsedArgList parseCallArgList(functionDef* func, statementBlock* body);

        // Global function resolution: finds the best matching global function for a call.
        // Resolution priority: exact type match > conversion match > var fallback.
        struct GlobalCallMatch {
            functionDef* match = nullptr;            // best matching function
            functionDef* nameMatch = nullptr;        // first function with matching name (for error messages)
            functionDef* arityMatch = nullptr;       // first function with matching arity
            string funcVarReturnType;                // non-empty if this is a func<> variable call
        };
        GlobalCallMatch resolveGlobalCall(const string& name, const vector<expression*>& args, functionDef* func, statementBlock* body);
        string validateGlobalCall(GlobalCallMatch& gcm, const string& funcName, size_t argCount);

        // Replace a pre-scan stub in a member list with the real definition. Returns true if replaced.
        static bool replaceStubMember(vector<typeMember*>& members, functionDef& newDef);

        objectDef* currentObject = nullptr;
        classDef* currentClass = nullptr;    // set when parsing inside a class declaration
        functionDef* currentFunc = nullptr;  // outermost function being parsed (not changed for nested if/while blocks)
        functionDef* lambdaOuterFunc = nullptr;    // set during lambda parsing to enable capture detection
        statementBlock* lambdaOuterBody = nullptr; // outer function body during lambda parsing
        string addCapture(const string& outerName, const string& typeName); // register a closure capture, return global name
        sourceLocation currentStatementSrc;  // location of the first token of the current statement

        map<string,string> definedSymbols;  // symbols defined via #define; value is "" for boolean flags, else the literal value
        bool evaluateCondition(const string& expr);   // evaluates a #if boolean expression
        void skipConditionalBlock(abstractObject& ctx);    // skips tokens until #elif/#else/#endif at depth 0
        void skipBglConditionalBlock(abstractObject& ctx); // skips tokens until ##else/##endif at depth 0

        // Pre-scanner state
        set<string> preScanOnceFiles;
        int preScanDepth = 0;
        void preScanDirective(token tok);
        void preScanSkipConditionalBlock(); // skips tokens in a false #if branch until #elif/#else/#endif
        void preScanSkipBody();           // consumes opening '{' and everything through matching '}'
        void preScanSkipBodyContents();   // assumes '{' already consumed; skips to matching '}'
        void preScanSkipToSemicolon();    // consumes tokens up to and including ';'
        void preScanSkipParens();         // assumes '(' already consumed; skips to matching ')'

        vector<statement*> pendingInjections;  // pre-statements to emit before next main statement (e.g. from ternary lowering)
        vector<statement*> postInjections;     // post-statements to emit after next main statement (e.g. closing braces for ?. guards)

};

extern bglParser parser;