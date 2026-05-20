#pragma once
#include <string>
#include <sstream>
#include <deque>
#include <set>
#include <map>
#include <functional>
#include <optional>

#include "helpers.h"
#include "token.h"
#include "fileLexer.h"
#include "i6Emitter.h"
#include "typeDef.h"

using namespace std;

struct exitFileSignal {};   // thrown by #exit to unwind to the enclosing parseFile loop

//=============================================================================
// Grammar-driven pattern matching types (used by processNextStatement)
//=============================================================================

// One position in a syntax pattern.
// Implicit constructors allow concise grammar tables:
//   "("              → Literal match
//   eTokenType::dataType → TokenType match
//   pName            → Semantic predicate (pre-defined constant)
//   anyOf({"a","b"}) → AnyOf match
struct PatternElement {
    enum class Kind { Literal, TokenType, AnyOf, Semantic, Wildcard };
    Kind kind = Kind::Literal;
    string literal;
    eTokenType tokenType = eTokenType::unknown;
    vector<string> anyOfValues;
    function<bool(token&)> predicate;
    string tag;  // for error messages; auto-derived if empty

    // Implicit conversions — these make the grammar table concise
    PatternElement() = default;
    PatternElement(const char* s)       : kind(Kind::Literal), literal(s) {}   // "(" → Literal
    PatternElement(const string& s)     : kind(Kind::Literal), literal(s) {}
    PatternElement(eTokenType t)        : kind(Kind::TokenType), tokenType(t) {} // eTokenType::dataType → TokenType

    // Named constructor for AnyOf
    static PatternElement anyOf(vector<string> vs, string tag = "") {
        PatternElement e; e.kind = Kind::AnyOf; e.anyOfValues = vs; e.tag = tag; return e;
    }
    // Named constructor for Semantic predicates
    static PatternElement semantic(function<bool(token&)> pred, string tag = "") {
        PatternElement e; e.kind = Kind::Semantic; e.predicate = pred; e.tag = tag; return e;
    }
};

// Pre-defined pattern elements for grammar token positions
// TYPE_NAME: a registered class/enum/base type (lexer: dataType). Object instances are NOT types.
inline PatternElement TYPE_NAME(eTokenType::dataType);
// NEW_NAME: an identifier being declared (variable, function, parameter, object instance name).
// Also used for references to any non-type name. Accepts both identifier and dataType tokens so
// that user-chosen names colliding with a registered class/enum produce a clean shadow error from
// the downstream handler rather than a raw "Unrecognized statement" from the pattern matcher.
inline PatternElement NEW_NAME(PatternElement::semantic(
    [](token& t){ return t.is(eTokenType::identifier) || t.is(eTokenType::dataType); }, "identifier"));

// Qualifier flags parsed from declaration prefixes (replace, explicit, extern, emitter, const, static, extend, alias, default).
// Defined here (before GrammarRule) so grammar handlers can reference it.
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

// Forward-declare bglParser so handler signature can reference it
class bglParser;

// Handler: member function pointer with standard signature.
// Receives matched tokens, qualifiers, and context object.
// Returns true if end-of-block reached, false otherwise.
using GrammarHandler = bool (bglParser::*)(vector<token>&, Qualifiers&, abstractObject&);

// One syntax rule: a pattern + handler method
struct GrammarRule {
    string name;                                        // human-readable name for error messages
    vector<PatternElement> pattern;
    GrammarHandler handler = nullptr;                   // called when this rule wins
};

// Result of matching against the grammar table
struct GrammarMatch {
    bool success = false;
    string ruleName;
    vector<token> matchedTokens;
    GrammarHandler handler = nullptr;                   // handler from the winning rule
    // Error info: what came closest before failing
    struct FailedCandidate { string ruleName; int matchedUpTo; };
    vector<FailedCandidate> failedCandidates;
};
struct lspRecoverySignal : public std::runtime_error {  // thrown by parsingError in LSP mode — caught by parseFile to continue
    using runtime_error::runtime_error;
};

class bglParser {
    public:
        fileLexer file;    //what the parser reads from.  Tokens are produced by the filelexer.
        bool lspMode = false;                  // when true, parsingError collects errors instead of halting
        std::vector<std::string> lspErrors;    // errors collected during LSP-mode parsing

        // Inactive source-line ranges — lines skipped because an enclosing #if/#elif/#else was
        // false. Populated during pre-scan and main parse. Keyed by canonical file path so the
        // LSP can report only ranges for the document being parsed. Half-open: [startLine0, endLine0Exclusive).
        struct InactiveRegion { int startLine0; int endLine0Exclusive; };
        std::map<std::string, std::vector<InactiveRegion>> inactiveRegions;
        // Record a dead-code range from a 1-based inclusive [startLine1, endLine1]. Resolves the
        // current file from the lexer stack. Silently ignores empty or inverted ranges.
        void recordInactiveRange(int startLine1, int endLine1Inclusive);
        bglParser();
        void reset();  // clear all accumulated state for LSP re-parse
        // pass 1: register type/object stubs for forward-reference resolution.
        // When contentOverride is non-null, the lexer reads from the in-memory string instead of
        // the disk file (used by the LSP to parse the editor buffer without writing a temp file).
        // The filename is still used for error messages, source tracking, and include resolution.
        void preScanFile(string filename, const std::string* contentOverride = nullptr);
        // The main entry point: given a file, read it in, parse it, and store it in the parse tree.
        // contentOverride applies the same in-memory-buffer semantics as preScanFile.
        bool parseFile(string filename, const std::string* contentOverride = nullptr);
        // .inf-as-input mode: the entry file is treated as a single implicit raw-I6 region with
        // `#bgl{...}` re-entry. Authors can add Beguile features to existing I6 source incrementally
        // without converting the whole file. The Beguile Language Runtime is auto-loaded so types
        // and emitters are available inside #bgl blocks, but no Beguile declarations may appear
        // at the file's top level. Called by parseFile when the entry file ends in `.inf`.
        bool parseInfFileBody(abstractObject& contextObj);
        // Find the first top-level `end;` directive (word-boundary token followed by `;`)
        // in `text`, masking I6 single-line comments (`!` to EOL), string literals (`"..."`),
        // and char/dictionary literals (`'...'`). Returns the byte offset at the start of the
        // matched `end` keyword, or string::npos if no match. Used by parseInfFileBody to
        // extract the .inf trailer (everything from the first `end;` through EOF).
        size_t findInfEndDirective(const std::string& text);
        // Cross-language collision check (.inf-mode). Walks raw I6 text in compositeNodes
        // for top-level I6 declarations (Object/Class/Constant/Global/Array/Attribute/
        // Property/[name]) and warns when a name collides with a Beguile-declared global
        // in the same compilation. Same-file scope only — included files aren't scanned.
        void detectInfModeI6Collisions();
        bool parsingError(string);   //called when there is an error, to output the error message and the place in the code where it appeared
        void parsingWarning(string); //like parsingError but continues parsing
        void applySchemaDefaults(); // apply beguilerSettingsType default values to any unset settings fields
        void defineSymbol(const string& name, const string& value = ""){ definedSymbols[name] = value; }

        string contextToString(eCompileContext);

        //as we are parsing a file, we enter and exit "contexts" which help the parser determine what is and isn't valid.  For example, the global context allow different things than in the context of a routine.
        void openCompileContext(eCompileContext, statementBlock* body = nullptr);   //entering a new context
        void closeCompileContext(eCompileContext);  //closing out the current context and returning to the previous
        eCompileContext getCurrentCompileContext(); //what is the the current context?
        string processBglConditionals(const string& text); // evaluates ##ifdef/##ifndef/##else/##endif in raw emitter body text

        // Grammar handler methods — standard GrammarHandler signature, called from the grammar table.
        // Declarations
        bool processEnum(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processClass(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processGrammar(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processArray(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRoutine(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processObject(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processVariable(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processTypedObject(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processAliased(vector<token>& t, Qualifiers& q, abstractObject& c);
        // Statements
        bool processBreak(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processContinue(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRtrue(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRfalse(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processReturnVoid(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processReturnExpr(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processIf(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processWhile(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processDo(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processFor(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processSwitch(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processTry(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processThrow(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processDelete(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processDirectiveDispatch(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processFunc(vector<token>& t, Qualifiers& q, abstractObject& c);

        // Parser handler methods — called from grammar handlers and processNextStatement.
        bool processClassDeclaration(token, bool isExternal, bool isExtend=false, bool isEmitterClass=false, bool isAlias=false, token nameOverride=token());
        bool processEnumDeclaration(token, bool, token nameOverride=token());
        bool processObjectDeclaration(token typeTok, token nameTok, bool isExtern, string className = "", string i6alias = "", bool hasBody = true, bool isEmitter = false);
        bool processEmitterValueDeclaration(token typeTok, token nameTok);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer, bool = false, bool = false, bool = false);
        bool processVariableDeclaration(token typeTok, token nameTok, token symbol, abstractObject& = emptyContainer, bool isExtern = false, bool isConst = false, string i6alias = "");
        bool processArrayDeclaration(token, token, string, token, abstractObject& = emptyContainer, bool = false);
        bool processArrayDeclarationFromGeneric(token arrayTok, Qualifiers& q, abstractObject& ctx);  // reads from after '<'
        bool processGrammarDeclaration(token nameOverride=token());
        bool processObjectExtension(token nameTok);
        bool processTypedObjectDeclaration(token typeTok, token nameTok, token classNameTok, Qualifiers& q, abstractObject& ctx);  // Type name : ClassName ...
        bool processAliasedDeclaration(token typeTok, token nameTok, token aliasTok, Qualifiers& q, abstractObject& ctx);  // Type name as alias ...

    private:
        deque<eCompileContext> compileContextStack;
        // Stack of statement blocks currently being parsed, from outermost (function body) to
        // innermost (current if/for/while body). Used by Tier 1c identifier resolution to find
        // locals declared in ancestor blocks that haven't been added to the AST yet.
        vector<statementBlock*> activeBlockStack;
        set<string> onceFiles;        // absolute paths of files that declared #once
        set<string> startupFiles;     // absolute paths of files whose #startup blocks have been registered
        vector<classDef*>  usingImports;         // imported class scopes from #using directives (file-scoped)
        vector<objectDef*> usingObjectImports;   // imported object scopes from #using directives (file-scoped)
        int includeDepth = 0;               // current include nesting depth
        static constexpr int maxIncludeDepth = 255;
        int forInCounter = 0;               // counter for unique _bglfiN variable names
        int lambdaCounter = 0;              // counter for unique _bglLambdaN function names
        int loopDepth = 0;                  // nesting depth of for/while/do loops (for continue validation)
        set<string> currentLoopVars;        // names of active for-loop init variables (for capture warnings)
        int ternaryDepth = 0;               // nesting depth of ternary expressions (max 1)

        bool processNextStatement(abstractObject& =emptyContainer);  // grammar-driven dispatcher
        bool processStatementDispatch(token tok, abstractObject& ctx);  // grammar dispatch for a pre-read token
        bool processParameterList(functionDef&);

        // Grammar-driven pattern matching
        vector<GrammarRule> grammarRules;
        bool grammarInitialized = false;
        void initGrammarTable();
        GrammarMatch matchGrammar(token& firstToken);
        bool matchElement(const PatternElement& elem, token& tok);
        string describeExpected(const PatternElement& elem);

        bool processBeguilerSettings();
        void processExtendCompoundAssignment(objectDef& obj, token memberName, const string& op, verbObjectDef* vod, int blockPriority = 0);
        // After parseGrammarLineContent has consumed the inner `}` of `{Verb, {pattern}}`, peek for
        // a `, priority` third positional element. Returns parsed int priority, or `defaultPriority`
        // when no third element is present.
        int parseOptionalGrammarRulePriority(int defaultPriority);
        void parsePropertyValue(variableDeclaration& prop, string typeName);
        void processI6InlineMember(objectDef& obj);
        // Routine dispatch: if `ctx` and `q` are supplied AND the parsed member shape is
        // `array<T> name(...)`, this dispatches to processRoutineDeclaration and returns true
        // (the caller should NOT continue member processing). Otherwise parses a variable
        // declaration into `members` and returns false.
        bool processArrayMember(vector<typeMember*>& members, const string& ownerDName, verbObjectDef* vodForGrammarRules,
                                abstractObject* ctx = nullptr, Qualifiers* q = nullptr);
        void processTypedMember(objectDef& obj, token typeTok, bool isReplace = false);
        void processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace = false);
        void processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue, bool isReplace = false);
        void processInheritedMember(objectDef& obj, token nameTok);
        bool processGrammarObjectDeclaration(const string& name);  // grammar object with grammarRule members
        vector<grammarLine> parseGrammarLines();
        grammarLine parseGrammarLineContent();  // parses single grammar line (trigger + pattern tokens); assumes '{' consumed

        string parseFuncType();             // reads <ReturnType,ParamType,...> from stream; returns "func<...>"
        string parseLambdaExpr(functionDef* func, statementBlock* body);  // parses lambda, lifts to global, returns lifted name

        bool processStatement(token, abstractObject& = emptyContainer);
        bool processDirective(token, abstractObject& = emptyContainer);

        // Namespace-scoped type resolution helpers
        string resolveNamespacedType(const string& dottedPath); // walks namespace objects to resolve dotted type path
        bool isNamespacedTypePath(token firstTok);               // peeks ahead to check if token starts a valid namespace type path
        token consumeTypeToken(token first);                     // consumes dotted type tokens, returns synthetic dataType token
        // Try to resolve `A.B.C.valueName` where `A.B.C` namespace-resolves to an enum/bnum type
        // and `valueName` is a named value of that type. On success: consumes the dotted tokens
        // past `first`, fills outFlatEmission (e.g. "_bglulxwindowplacement_above") and outEnumType
        // (the enum's flat name), and returns true. On failure: no tokens consumed, returns false.
        bool tryConsumeNamespacedEnumValue(token first, string& outFlatEmission, string& outEnumType);

        expression* parseExpression(token firstToken, vector<string> terminators, functionDef* func, statementBlock* body, int startParenDepth = 0);
        // parseExpression sub-functions (extracted for readability)
        void parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body, int parenDepth = 0);
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

        // Parse qualifier keywords from the token stream in any order. Consumes qualifying tokens,
        // leaves tok pointing at the first non-qualifier. Validates invalid combinations.
        Qualifiers parseQualifiers(token& tok);
        // memberHint: when non-empty, the resolver prefers candidates whose type has a member of
        // that name. Used to disambiguate name collisions across tiers — e.g. an enum value and a
        // class instance with the same name. If no candidate satisfies the hint, falls back to the
        // normal first-match (so dispatch errors still surface against the natural type).
        string resolveIdentifierType(string name, functionDef* func, statementBlock* body, const string& memberHint = "");

        // Return the declared element type of an array variable, or "" if `name` isn't an
        // arrayDeclaration in any reachable scope. Walks locals, class/object members, globals.
        string resolveArrayElementType(const string& name, functionDef* func, statementBlock* body);
        // Same for dotted access — objName.propName where propName is an arrayDeclaration member.
        string resolveArrayElementTypeDotted(const string& objName, const string& propName,
                                              functionDef* func, statementBlock* body);
        // Look up operator[] / operator[]= on an array class hierarchy matching a specific element
        // type. For read (`[]`), matches by return type. For write (`[]=`), matches by second
        // parameter type. If no explicit overload exists and the element type is a registered
        // class (user-defined), synthesizes one using the `object`-typed overload as a template.
        // Returns nullptr if no match and no synthesis possible.
        functionDef* findArraySubscriptOp(classDef* arrCls, const string& elemType, bool isWrite);
        // memberHint: forwarded to the underlying resolveIdentifierType for the path's head.
        string resolvePathType(string path, functionDef* func, statementBlock* body, const string& memberHint = "");
        // True if `typeName` resolves to a class (incl. base hierarchy) or objectDef that exposes
        // a member (field/method) named `memberName`. Returns false for primitives, enums, and
        // unknown types. Used by the resolver for member-aware tie-breaking.
        bool typeHasMember(const string& typeName, const string& memberName);
        // memberHint: same semantics as resolveIdentifierType — prefer candidates whose type
        // exposes the named member, falling back to first-match if none satisfy.
        string qualifyIdentifier(string name, functionDef* func, statementBlock* body, const string& memberHint = "");
        bool isTypeCompatible(string argType, string paramType);
        // Returns the classDef whose member/operator hierarchy applies to `typeName`-typed
        // values. For a classDef-typed name, returns that classDef. For an objectDef-typed
        // name, returns the objectDef's `objectClass`. nullptr if `typeName` isn't a class
        // or instance type. Use anywhere dispatch needs to walk methods/operators by type.
        classDef* getDispatchClass(const string& typeName);
        void applyArgConversions(vector<expression*>& args, functionDef* fd);
        // Canonicalize a parsed argument list against a resolved function signature. Performs:
        //   (1) named-argument reordering, (2) default-value fill for trailing unspecified params,
        //   (3) source-type conversion via operator() on argument classes. Mutates all three
        //   vectors in place. Call site must have already validated arity. Any namedArgNames /
        //   interpSegmentsPerArg vector may be empty if the caller doesn't track that information.
        void finalizeCallArgs(vector<expression*>& args, vector<string>& namedArgNames,
                              vector<vector<interpolatedSegment>>& interpSegmentsPerArg, functionDef* fd);

        // Unified dotted method call binding. Performs (1) resolveMethodWithConversion (with LHS
        // conversion-operator fallback), (2) nameFound/arity/overload validation with detailed
        // error messages, (3) finalizeCallArgs. Updates objType in-place if conversion succeeded.
        // Always returns non-null (errors throw via parsingError). Every dotted method call site
        // must route through this so the five steps can't drift apart.
        functionDef* bindMethodCall(string& objType, const string& objPath, const string& methodName,
                                     vector<expression*>& args, vector<string>& namedArgNames,
                                     vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                     const string& elementType = "");

        // Unified global function call binding. Performs resolveGlobalCall + validateGlobalCall +
        // finalizeCallArgs. For calls through a func<> variable, funcVarReturnType is set and
        // method is nullptr (no signature to bind against).
        struct GlobalCallBinding {
            functionDef* method = nullptr;
            string funcVarReturnType;   // non-empty for func<> variable calls
        };
        GlobalCallBinding bindGlobalCall(const string& name, vector<expression*>& args,
                                          vector<string>& namedArgNames,
                                          vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                          functionDef* func, statementBlock* body);

        // Unified method resolution: searches class hierarchy, then objectDef members (with self→currentObject),
        // handles pre-scan stubs, default params, var fallback. Returns nullptr if not found.
        struct MethodMatch {
            functionDef* method = nullptr;      // best matching method, or nullptr
            functionDef* nameMatch = nullptr;    // first method with matching name (for error context)
            functionDef* arityMatch = nullptr;   // first method with matching arity (for error context)
            bool nameFound = false;              // true if any method with the name exists
        };
        MethodMatch resolveMethod(const string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args, const string& elementType = "");
        // Same as resolveMethod, but on failure walks the LHS class's non-explicit operator() conversion
        // emitters and retries on each converted type. If a conversion succeeds, typeName is updated
        // in-place to the converted type name so the caller can substitute it.
        MethodMatch resolveMethodWithConversion(string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args, const string& elementType = "");

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
        // One-token stash consulted by parseExpression's getNext() before reading from the lexer.
        // Used to put back a terminator token that an inner construct (e.g. an arrow-body lambda
        // in function-argument position) consumed but the enclosing parser still needs to see.
        std::optional<token> stashedToken;
        // Expected-type hint for the expression currently being parsed. Set by call sites that know
        // the type they need (e.g. variable initializer RHS, operator RHS). Used by name resolution
        // as a final tie-breaker when multiple candidates remain after memberHint filtering.
        // Always saved/restored at sub-context boundaries (function args, etc.) so it doesn't leak.
        string currentExpectedType;
        // True while parsing the operand of `return ...;` inside a void function. Permits a
        // void function call to appear as the return expression — the C/I6 idiom
        // `return f();` ≡ `f(); return;`. Cleared at every other expression-context boundary.
        bool allowVoidReturnExpr = false;
    public:
        // True while parsing the contents of an `#bgl{}` block embedded in `#i6{}`. Unknown
        // identifiers are passed through (qualifyIdentifier returns the name; resolveIdentifierType
        // returns "var") so I6 names referenced from Beguile resolve at the I6 compile step.
        // Public so fileLexer can consult it when deciding whether to treat EOF as benign
        // (sub-parses of in-memory bgl content terminate at EOF, regardless of compile context).
        bool looseIdentifierMode = false;
        // Public read accessor for the class currently being parsed. Used by
        // bglLanguageService::isClassType to recognize the class's type parameters
        // (e.g. T in `class array<T>`) as data types within the class body.
        classDef* getCurrentClass() const { return currentClass; }
    private:
        statementBlock* lambdaOuterBody = nullptr; // outer function body during lambda parsing
        // Stack of enclosing functions for nested lambda capture resolution. Each entry is an
        // outer function scope that may contain capturable variables. Innermost (most recent) first.
        vector<functionDef*> lambdaOuterFuncStack;
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
        void preScanGlobalLoop();         // walks tokens in the currently-open file, registering type/global stubs until EOF
        void preScanInfFileBodyForDecls(); // .inf-mode pass-1: scans .inf body for #bglDecl{} / #bgl(decl-mode){} islands and pre-scans their content as declarations
        // Walk a string of bgl content and pre-scan declarations from it. Used for #bgl islands
        // discovered inside an .inf file during Pass 1. The content is opened as a virtual file
        // for the duration of the call.
        void preScanBglIslandContent(const std::string& content, const std::string& virtualName, int startLine);
        void preScanSkipConditionalBlock(); // skips tokens in a false #if branch until #elif/#else/#endif
        // Skip a `{ ... }` body during pre-scan. The `isI6Content` flag controls
        // `!` handling: false (default) treats `!` as a Beguile operator (correct
        // for object/class/function bodies); true treats it as I6 line-comment
        // start (correct for #i6, emitter bodies, #emitfirst/last/startup).
        void preScanSkipBody(bool isI6Content = false);           // consumes opening '{' and everything through matching '}'
        void preScanSkipBodyContents(bool isI6Content = false);   // assumes '{' already consumed; skips to matching '}'
        void preScanSkipToSemicolon();    // consumes tokens up to and including ';'
        void preScanSkipParens();         // assumes '(' already consumed; skips to matching ')'
        void preScanCaptureParams(vector<paramDef*>& out); // like preScanSkipParens but captures type/name of each parameter
        // If typeTok is a generic base type (array/func) and the next token is '<', consume
        // the generic parameter list ('<T>' for array, balanced '<...>' for func) so the caller
        // can continue reading the identifier name. Used by pre-scan member header recognition.
        void preScanConsumeGenericSuffix(const token& typeTok);

        vector<statement*> pendingInjections;  // pre-statements to emit before next main statement (e.g. from ternary lowering)
        vector<statement*> postInjections;     // post-statements to emit after next main statement (e.g. closing braces for ?. guards)

};

extern bglParser parser;