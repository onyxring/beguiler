#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include <vector>
#include <map>
#include <memory>
#include <optional>

using namespace std;

enum class eCompileContext {noContext, global, objectDef, codeBlock};

// Source location — Beguile file and line number where an AST node was parsed.
// Used to build the source map for the debugger.
struct sourceLocation {
    string file;
    int line = 0;  // 0 = unknown/not set
};

//base class for all elements in the language.
class abstractObject{
    public:
        string name;
        string displayName; // original-case name for error messages (empty = use 'name')
        string i6name;  // optional I6 alias: if non-empty, emitted under this name instead of 'name'
        string docComment;  // user-authored doc-comment (`///` or `/** */`) preceding the declaration; rendered as Markdown in LSP hover
        bool operator == (abstractObject);
        bool isExternal;
        bool isPrePassStub = false; // true when registered by the pre-scanner; cleared when full pass processes the declaration
        const string& dName() const { return displayName.empty() ? name : displayName; } // display name for error messages
        virtual void dummy(){}  //typeid requires at lease one virtual function in the base class to work
};
//things defined at the global level, such as type definitions, objects, enums, and functions and global variables.
class typeDef:virtual public abstractObject{
    public:         
};
class typeInstance:virtual public abstractObject{
    public:                 
};
//members of types, including functions and variables.
class typeMember:virtual public abstractObject{   
    public:        
};

//class definitions
class classDef:public typeDef{
    public:
        sourceLocation src;
        bool isEmitterClass = false;        // true for 'emitter class': no I6 backing, emitter members only
        bool isGlobalEmitterObject = false; // true for 'emitter Foo { }': singleton emitter namespace; accessed as Foo.method(), no instances
        bool isAlias = false;               // true for 'alias Foo : Parent { }': Beguile type that dissolves to parent for emission
        // Walk the alias chain to find the I6 class name used in emitted output.
        // Alias classes delegate to their first base class; all others use their own name.
        std::string i6Name() const {
            if(isAlias && !baseClasses.empty()) return baseClasses[0]->i6Name();
            return name;
        }
        vector<typeMember*> members;
        vector<classDef*> baseClasses;
        std::string globalDeclarationBody; // raw I6 body of 'emitter void globalDeclaration()'; emitted after each instance
        // Pool size for `class Foo[N] {...}` — reserves N statically-allocated instances at compile time.
        // 0  = not pooled (regular class).
        // >0 = sized pool (emit as `Class Foo(N)`).
        // -1 = extern marker `extern class Foo[]` — I6 owns the size; Beguile treats as pooled for new/delete checks.
        int poolSize = 0;
        // Type parameters declared on this class (e.g., `class array<T>` → ["t"]).
        // Empty for non-generic classes. Stored on the classDef but NOT registered as global
        // types (that would collide with same-named instances like `Temperature t;`).
        // bglLanguageService::isClassType / getType consult parser.currentClass to recognize
        // them inside the class body; method lookup substitutes parameter→concrete at call sites.
        vector<string> typeParameters;
};
//instances of classes, including overrides
class objectDef: public typeDef{
    public:
        sourceLocation src;
        vector<typeDef*> baseClasses;
        vector<typeMember*> members;
        classDef* objectClass = nullptr; // the Beguile class this object is an instance of (if known)
};

//a parameter of a function
class paramDef:public abstractObject{
    public:
        typeDef type;
        string defaultValue;  // "" if no default; otherwise the default expression text
};

//the function body
class codeBlock:public abstractObject{
    public:

};

//a specific type of function body, containing raw I6 code to be emitted directly into the output file, without any processing by Beguile.
class i6Block:public codeBlock{
    public:
        string i6Body;
};  
//a base clase for an individual statement which appears in blocks of code
class statement:virtual public abstractObject{
    public:
        sourceLocation src;
};

//a block of statements, essentiall the body of a function
class statementBlock:public codeBlock{
    public:
        vector<statement*> statements;
};
// a parsed expression: a flat list of tokens with a resolved type.
// emitted verbatim to I6; type is used for operator/emitter dispatch.
class expression {
    public:
        virtual ~expression() = default;
        vector<string> tokens;     // raw token strings making up the expression
        string resolvedType;       // inferred Beguile type name, or "" if unknown
        string terminator;         // which terminator token ended this expression
        string emitterSelf;        // host text for $self when this expr is a property access
                                   // (e.g. for `o.parent` → "o"); empty for non-property-access exprs

        string text() const {
            string result;
            for(const string& t : tokens){
                // Translate Beguile/C operators to I6 equivalents.
                // In I6, '!' is the line-comment character, so '!=' must become '~=' (not-equal).
                if(t == "!=") result += "~=";
                else result += t;
            }
            return result;
        }
};

// an initializer list: { expr, expr, ... }
// stored as declaredExpressionValue on a variableDeclaration; emitted type-dependently.
// Supports nested lists: { {a, b}, {c, d} } — inner lists are initializerList* in elements.
class initializerList : public expression {
    public:
        vector<expression*> elements;  // may include nested initializerList* nodes
};
// A single segment of an interpolated string: either a literal text fragment or a parsed expression.
// Used by assignment, variable declaration, and function call statements for $"..." handling.
struct interpolatedSegment {
    bool isExpr = false;
    string text;           // string segments: I6-ready literal text (no outer quotes)
    expression* expr = nullptr; // expression segments: fully parsed Beguile expression
    vector<statement*> injections; // ternary lowerings to emit before this expression segment
};

//a type of statement which assigns a value to a variable
class assignmentStatement:public statement{
    public:
        string variableLeft; //todo: probably should have a variable table for scope
        expression* assignedExpression = nullptr;
        string emitterBody;   // raw i6 body text if operator is an emitter, else ""
        string emitterParam;  // parameter name to substitute in the body
        string emitterSelf;   // value to substitute for $self; defaults to variableLeft if empty
        vector<interpolatedSegment> interpSegments; // non-empty when RHS is $"..."
};
//a type of statement which returns a value from a function
class returnStatement:public statement{
    public:
        string returnExpression;
};
//the declaration of a variable.  This may be a global variable, an object member, or a local variable within a function.
class variableDeclaration:public typeMember, public statement, public typeDef, public typeInstance{
    public:
        typeDef type;
        bool isConst = false;
        // isConst: for globals, emits as I6 Constant; for class members, prevents reassignment (property still has runtime storage)
        bool isStatic = false;   // static members: class-level state, emitted as mangled I6 global
        bool isAlias = false;    // type alias member: `alias name for Type;` — compile-time type reference, no I6 backing
        expression* declaredExpressionValue = nullptr;
        string initEmitterBody;  // raw i6 body if operator= is an emitter, else ""
        string initEmitterParam; // parameter name to substitute in the body
        vector<interpolatedSegment> interpSegments; // non-empty when initializer is $"..."
    //virtual std::unique_ptr<typeMember> clone() const override { return std::make_unique<variableDeclaration>(*this); }
};
//a type of statement which represents a function call.
class functionCall:public statement{
    public:
        vector<paramDef*> params;
};
//a function call statement with resolved arguments, optionally backed by an emitter body
class functionCallStatement:public statement{
    public:
        string functionName;
        vector<expression*> args;      // parsed argument expressions (type in args[i]->resolvedType)
        vector<string> namedArgNames;  // parallel to args: non-empty when arg was passed as name:value
        string emitterBody;            // raw i6 body if an emitter was resolved, else ""
        vector<string> emitterParams;  // parameter names to substitute in the body
        vector<vector<interpolatedSegment>> interpSegmentsPerArg; // per-argument interpolated segments; interpSegmentsPerArg[i] is non-empty when args[i] is $"..."
};
//the declaration of a function.  This may be a global function, or an object member
class functionDef:public typeMember, public typeDef{
    public:
        sourceLocation src;
        bool isEmitter;
        bool isExplicit = false;   // true for 'explicit emitter': conversion operator only fires at explicit cast sites
        bool isDefault = false;    // true for 'default': expected to be overridden without requiring 'replace'
        bool isValueEmitter = false; // true for emitter values (no parens): expands inline as expression or statement
        typeDef returnType;
        vector<paramDef*> params;
        codeBlock* body = nullptr;
        // deinit cleanup entries: {varName, deinitBody} — emitted before every return and at end of function
        vector<pair<string,string>> cleanups;
        // replace chaining: when this function replaces a previous definition,
        // replaced() calls in the body resolve to replacedTarget (the mangled name).
        string replacedTarget;               // mangled name of predecessor function (empty if not a replacement)
        functionDef* replacedFunc = nullptr;  // pointer to predecessor functionDef
        bool replacedWasCalled = false;       // set when replaced() is encountered during body parsing
        bool isReplacedDead = false;          // true when this replaced version is unreachable (no successor calls replaced())
        // Closure captures: variables from the enclosing scope referenced by a lambda.
        // Each entry maps: {outer variable name, capture global name, type name}
        struct Capture { string outerName; string globalName; string typeName; };
        vector<Capture> captures;

    using abstractObject::name;

};
//an individual value within an enum definition
class enumValueDef:public abstractObject{
    public:
    int value;
};
//the declaration of an enum type
class enumDef:public typeDef{
    public:
        sourceLocation src;
        vector<enumValueDef*> namedValues;
        bool isBnum = false;          // bnum (bitwise int) vs plain enum
        enumDef* baseBnum = nullptr;  // shared-base grouping: only valid when isBnum
};

// an if statement, with a condition expression and a then-block, and an optional else-block
class ifStatement : public statement {
    public:
        expression* condition = nullptr;
        statementBlock* thenBlock = nullptr;
        statementBlock* elseBlock = nullptr;
};

// one entry in a switch case: a single value, a range (low to high), or a comparison guard
struct caseEntry {
    expression* value = nullptr;       // single value match (null if range or guard)
    expression* rangeLow = nullptr;    // range: low bound (null if not a range)
    expression* rangeHigh = nullptr;   // range: high bound (null if not a range)
    string guardCondition;             // comparison guard condition string using _bgl_sw (empty if not a guard)
};

// one arm of a switch statement; empty entries means default:
class switchCase : public abstractObject {
    public:
        vector<caseEntry> entries;
        statementBlock* body = nullptr;
};

// a switch statement
class switchStatement : public statement {
    public:
        expression* condition = nullptr;
        vector<switchCase*> cases;
        bool needsIfChain = false; // true if any case uses comparison guards or operator switch(); emit as if/else if
        // operator switch() emitter bodies, keyed by parameter type name; used for if-chain comparisons
        map<string, string> switchEmitters; // paramType → emitter body text (with $self and param name unsubstituted)
};

// a do-until / do-while loop: do { body } until(cond) or do { body } while(cond)
// isWhile=true means the condition is negated before emitting as I6 do...until
class doStatement : public statement {
    public:
        expression* condition = nullptr;
        statementBlock* body = nullptr;
        bool isWhile = false;  // true → negate condition (do-while semantics)
};

// a while loop: while(condition) { body }
class whileStatement : public statement {
    public:
        expression* condition = nullptr;
        statementBlock* body = nullptr;
};

// a for loop: for(init; condition; increment) { body }
class forStatement : public statement {
    public:
        string initText;               // raw token text for init (before first ;)
        expression* condition = nullptr;
        string incrementText;          // raw token text for increment (before ))
        statementBlock* body = nullptr;
};

// a for-in loop: for(item in array) or for(TYPE item in array)
class forInStatement : public statement {
    public:
        string elementVar;   // iteration variable name, e.g. "item"
        string arrayVar;     // array name, e.g. "scores"
        string counterVar;   // unique index variable, e.g. "_bglfi0"
        bool isByteArray = false; // true when iterating array<char> — use byte for-in template
        statementBlock* body = nullptr;
        vector<expression*> inlineElements; // non-empty when source is {a, b, c} initializer list
};


// a raw block of I6 text emitted directly, usable at global scope, within a function body, or as an object member
class i6RawNode : public typeDef, public statement, public typeMember {
    public:
        string text;
        // If non-empty, the node contains a sequence of raw I6 text fragments interleaved with
        // Beguile statements (each entry is either a string fragment or a pointer to a parsed
        // statement). Used by `#i6{}` blocks that contain `#bgl{}` regions at global scope —
        // the parsed Beguile statements emit inline among the raw text at emit time. When this
        // vector is empty, the emitter falls back to the plain `text` field.
        // Each entry: <textFragment, optional statement, sourceLocation for textFragment>.
        // Emitted as text first (with per-line source-map entries anchored from textSrc),
        // then statement if present. textSrc lets I6 diagnostics inside multi-line raw
        // fragments map back to specific .bgl source lines.
        struct rawPart { string text; statement* stmt; sourceLocation textSrc; };
        vector<rawPart> parts;
};

// try/catch statement: structured exception handling using Z-machine @catch/@throw opcodes
class tryCatchStatement : public statement {
    public:
        int id = 0;                         // unique ID for label generation
        statementBlock* tryBody = nullptr;
        string catchVarName;                // catch variable name (e.g. "e")
        string catchVarType;                // catch variable type (e.g. "int")
        statementBlock* catchBody = nullptr;
};

// throw statement: throws a value to the nearest enclosing catch
class throwStatement : public statement {
    public:
        expression* value = nullptr;        // the thrown value expression
};

// An array<T> declaration — global emits as I6 Array directive; property emits inline values
class arrayDeclaration : public variableDeclaration {
    public:
        string elementType;          // the T in array<T>
        int arraySize = 0;           // N in array<T> name[N]; 0 if list-initialized
        bool isByteArray = false;    // true for array<char> — emit with -> instead of -->
        string stringInitializer;    // non-empty when initialized from "string" (I6: Array name string "...")
};

// a single grammar line: verbWord is the player's trigger word; patternTokens are I6-ready strings.
// additionalVerbWords holds any extra triggers from |-alternation in the first position
// (e.g. `{.type|.enter|.put, number, noun}` → verbWord="type", additionalVerbWords=["enter","put"]).
// When all triggers are first-occurrence in emitGrammarLines, they emit as one combined I6 Verb
// directive: `Verb 'type' 'enter' 'put' * pat -> Action;`. If any trigger is already declared (or
// the mode is Extend/Replace, which I6 only accepts one trigger word for), the emitter fans out
// to per-trigger directives.
struct grammarLine {
    string verbWord;                // raw word without quotes, e.g. "put" (emitter adds them)
    vector<string> additionalVerbWords;  // extra triggers from |-alternation in first position; empty for single-trigger lines
    vector<string> patternTokens;   // I6-ready: "'on'", "noun", "'up/p'", etc.
    string targetVerb;              // per-line verb override (multi-verb grammar objects); empty = use parent context
    int priority = 0;               // sort priority. The canonical default lives on `class verb` in BLR (_verb.bgl);
                                    // own-block lines get the verb's anchor in emitVerbObject; extend-block lines get
                                    // their block's priority during parse. 0 here is a sentinel meaning "not yet set".
    bool isOwnLine = true;          // false for extend-block and grammar-object contributions
    bool isReplaceMode = false;     // true → emit as I6 `Extend 'w' replace …` (drops existing rules for that
                                    // trigger word). Set when an extend body uses `grammar = { … }` (not `+=`).
                                    // Priority is meaningless under replace.
    bool isReverse = false;         // true → emit as I6 `* pattern -> Action reverse;` (swaps noun/second_noun
                                    // when the action receives the parsed args). Set by the trailing `reverse`
                                    // pseudo-token in the line literal: `{.give, creature, held, reverse}`.
};

// a verb declaration — an objectDef of class 'verb'; holds optional action body and inline grammar
class verbObjectDef : public objectDef {
    public:
        bool isExternal = false;
        bool isMeta = false;                // I6 meta verb (declared via `meta = true;` or `extern meta verb …`)
        int priority = 0;                   // anchor priority. Resolved in emitVerbObject from BLR's `class verb`
                                            // default, then overridden by any `priority = N;` in the verb body.
        vector<string> verbWords;           // dict words this verb claims. For extern verbs, declared via
                                            // `extern verb V { verbWords = {.w1, .w2}; }` body block; bare
                                            // `extern verb V;` defaults to {lowercased-name}. For non-extern
                                            // verbs, auto-populated from grammar lines if not explicitly set.
                                            // First entry is the primary trigger word.
        functionDef* doFunc = nullptr;      // action routine; I6 name = verbName + "sub"
        vector<grammarLine> grammarLines;   // inline grammar (from verb { grammar { } })
};

class grammarRuleDecl;  // forward declaration

// grammarRuleList typed member or standalone grammar object.
// As a verb member: name = "grammar", verbName = verb's action name.
// As a standalone object: verbName = target verb's action name.
class grammarRuleListDecl : public variableDeclaration {
    public:
        string verbName;                    // default verb action name (for old-style single-verb form)
        vector<grammarRuleDecl*> rules;     // grammarRule entries (each carries its own verb reference)
        vector<grammarLine> grammarLines;   // derived from rules for emitter consumption
};

// A single grammarRule member — wraps a verb reference + grammar line pair.
// Parsed from: grammarRule name = {VerbRef, {.word, TOKEN, ...}};
class grammarRuleDecl : public variableDeclaration {
    public:
        string targetVerb;       // resolved verb action name (lowercase)
        grammarLine line;        // the grammar pattern
};

// compile-time settings declared in source via a beguilerSettings { } block.
// Fields split into three categories:
//   - beguiler paths: applied immediately at parse time, not emitted to I6
//   - ICL directives: emitted as !% lines at the very top of the I6 output
class beguilerSettingsDef : public typeDef {
    public:
        // beguiler paths (not emitted)
        string beguiLibPath;           // overrides BEGUILE_LIB / binary-adjacent lookup
        string informBinaryPath;       // full path override for the I6 binary (from informPath property)
        string informName;             // binary filename only (from informName property); empty = use settingsStruct default
        string outputPath;             // overrides the output directory (CLI -o wins)

        // ICL directives (!% lines)
        string target;                 // !% -G (Glulx), !% -v3, !% -v5, !% -v8
        int release = 0;               // !% Release N;  (0 = not set)
        string serial;                 // Serial "YYMMDD"; (empty = not set; must be exactly 6 digits)
        string errorFormat;            // !% -EN  (e.g. "1" → -E1)
        vector<string> includePaths;   // unified search paths for both #include and #includeI6 resolution

        // runtime options (affect generated I6, not ICL)
        int framePoolSize = -1;        // Z-machine frame pool slot count (-1 = unset; default 64 from schema)
        int linqScratchSize = -1;      // LINQ chain scratch buffer capacity per buffer (-1 = unset; default 32 from schema)
        int worldBufSize    = -1;      // bglWorld scratch buffer capacity (-1 = unset; default 128 from schema)
        optional<bool> rewritePaths;   // path sep rewriting (unset = true; false only if explicitly disabled)

        // blorb packaging
        bool   blorbEnabled  = false;  // true = run asset scan + blorb build
        string blorbAssetPath;         // directory to scan; default "assets" applied by schema
        string author;                 // iFiction: game author
        string title;                  // iFiction: game title
        string headline;               // iFiction: subtitle
        string genre;                  // iFiction: genre
        string description;            // iFiction: blurb text
        string language;               // iFiction: ISO-639 language code
        string series;                 // iFiction: series name
        int    seriesNumber = 0;       // iFiction: position in series
        string firstPublished;         // iFiction: publication date (YYYY or YYYY-MM-DD)
        string forgiveness;            // iFiction: difficulty rating
        string ifid;                   // Treaty of Babel IFID (UUID format); auto-generated if empty
};

extern typeDef emptyTDef;
extern abstractObject emptyContainer;
