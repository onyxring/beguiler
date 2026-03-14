#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include <vector>
#include <memory>

using namespace std;

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
        bool operator == (abstractObject);
        bool isExternal;
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
        std::string alias;           // I6 name used in emitted output; empty = use `name`
        bool isEmitterClass = false; // true for 'emitter class': no I6 backing, emitter members only
        std::string i6Name() const { return alias.empty() ? name : alias; }
        vector<typeMember*> members;
        vector<classDef*> baseClasses;
};
//instances of classes, including overrides
class objectDef: public typeDef{
    public:
        sourceLocation src;
        vector<typeDef*> baseClasses;
        vector<typeMember*> members;
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
class initializerList : public expression {
    public:
        vector<expression*> elements;
};
//a type of statement which assigns a value to a variable
class assignmentStatement:public statement{
    public:
        string variableLeft; //todo: probably should have a variable table for scope
        expression* assignedExpression = nullptr;
        string emitterBody;   // raw i6 body text if operator is an emitter, else ""
        string emitterParam;  // parameter name to substitute in the body
        string emitterSelf;   // value to substitute for $self; defaults to variableLeft if empty
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
        expression* declaredExpressionValue = nullptr;
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
        string emitterBody;            // raw i6 body if an emitter was resolved, else ""
        vector<string> emitterParams;  // parameter names to substitute in the body
};
//the declaration of a function.  This may be a global function, or an object member
class functionDef:public typeMember, public typeDef{
    public:
        sourceLocation src;
        bool isEmitter;
        typeDef returnType;
        vector<paramDef*> params;
        codeBlock* body = nullptr;
        // deinit cleanup entries: {varName, deinitBody} — emitted before every return and at end of function
        vector<pair<string,string>> cleanups;

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
};

// an if statement, with a condition expression and a then-block, and an optional else-block
class ifStatement : public statement {
    public:
        expression* condition = nullptr;
        statementBlock* thenBlock = nullptr;
        statementBlock* elseBlock = nullptr;
};

// one arm of a switch statement; empty values means default:
class switchCase : public abstractObject {
    public:
        vector<expression*> values;    // empty for default:
        statementBlock* body = nullptr;
};

// a switch statement
class switchStatement : public statement {
    public:
        expression* condition = nullptr;
        vector<switchCase*> cases;
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
        statementBlock* body = nullptr;
};

// a raw block of I6 text emitted directly, usable at global scope, within a function body, or as an object member
class i6RawNode : public typeDef, public statement, public typeMember {
    public:
        string text;
};

// An array<T> declaration — global emits as I6 Array directive; property emits inline values
class arrayDeclaration : public variableDeclaration {
    public:
        string elementType;  // the T in array<T>
        int arraySize = 0;   // N in array<T> name[N]; 0 if list-initialized
};

// a single grammar line: verbWord is the player's trigger word; patternTokens are I6-ready strings
struct grammarLine {
    string verbWord;                // raw word without quotes, e.g. "put" (emitter adds them)
    vector<string> patternTokens;   // I6-ready: "'on'", "noun", "'up/p'", etc.
};

// a verb declaration — holds optional action body and optional inline grammar
class verbDef : public typeDef {
    public:
        sourceLocation src;
        bool isExternal = false;
        functionDef* doFunc = nullptr;      // action routine; I6 name = verbName + "sub"
        vector<grammarLine> grammarLines;   // inline grammar (from verb { ... })
};

// a standalone grammar block — adds grammar lines to an already-declared verb
class grammarBlock : public typeDef {
    public:
        string verbName;                    // the verb action name (lowercase)
        vector<grammarLine> grammarLines;
};

// compile-time settings declared in source via a beguilerSettings { } block.
// Fields split into three categories:
//   - beguiler paths: applied immediately at parse time, not emitted to I6
//   - ICL directives: emitted as !% lines at the very top of the I6 output
class beguilerSettingsDef : public typeDef {
    public:
        // beguiler paths (not emitted)
        string beguiLibPath;        // overrides BEGUILE_LIB / binary-adjacent lookup
        string informBinaryPath;    // overrides the inform6 binary path

        // ICL directives (!% lines)
        string target;              // !% -G (Glulx), !% -v3, !% -v5, !% -v8
        int release = 0;            // !% Release N;  (0 = not set)
        string errorFormat;         // !% -EN  (e.g. "1" → -E1)
        vector<string> includePaths;// !% +include_path=...  (one line per path)

        // runtime options (affect generated I6, not ICL)
        int framePoolSize = 64;     // Z-machine frame pool slot count (default 64)
};

extern typeDef emptyTDef;
extern abstractObject emptyContainer;
