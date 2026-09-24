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

// True for the word-array type whether bare ("array") or templated ("array<int>").
// Member/global receivers often resolve to the bare base while locals/LINQ results
// keep the template parameter; array $self/$prop lowering must treat both the same.
inline bool isWordArrayType(const std::string& t){ return t == "array" || t.rfind("array<", 0) == 0; }

// True when a type is itself a templated array — the element type of an array-of-arrays.
inline bool isArrayOfArraysElement(const std::string& t){ return t.rfind("array<", 0) == 0 || t.rfind("rawarray<", 0) == 0; }
// Extract T from "array<T>" / "rawarray<T>" (the outermost <...>). "" if not templated.
// "array<int>" -> "int";  "array<array<int>>" -> "array<int>".
inline std::string arrayInnerType(const std::string& t){
    size_t lt = t.find('<');
    if(lt == std::string::npos || t.empty() || t.back() != '>') return "";
    return t.substr(lt + 1, t.size() - lt - 2);
}

// Extract the element type T from a parametric array type name — "array<T>" or "rawarray<T>".
// Returns "" if `t` is neither form. Used everywhere the compiler needs an array's element type
// (subscript dispatch, for-in, type compatibility), so `rawArray<T>` shares array<T>'s machinery.
inline std::string arrayElemType(const std::string& t){
    if(!t.empty() && t.back() == '>'){
        if(t.rfind("array<", 0) == 0)    return t.substr(6, t.size() - 7);
        if(t.rfind("rawarray<", 0) == 0) return t.substr(9, t.size() - 10);
    }
    return "";
}

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
    bool isInline   = false;   // `inline` class member: joins the positional inline-construction slots (§6.2.1)
    bool isExtend   = false;
    bool isAlias    = false;
    bool isDefault  = false;
    bool isRef      = false;  // `ref` qualifier on a local variable declaration: opt-in
                              // reference semantics (skip operator= dispatch, skip backing
                              // synthesis, plain pointer-assign). Valid only on locals.
    bool isByVal    = false;  // `byVal` qualifier on a class declaration: instances of
                              // this class get value-semantics when passed as function
                              // parameters (backing + copy-in via operator= at entry).
                              // Mutually exclusive with extern/emitter/extend/: object.
    bool isSuperposed = false; // `superposed` qualifier on a function: the routine materializes
                               // into the story file only if something calls it; unused => elided.
    bool isTypeSealed = false; // `typesealed` qualifier on a base-class member: the slot type is
                               // locked. A subclass/instance that re-declares the member with a
                               // different type keeps the sealed type (and gets a warning). Lets
                               // `object parent = <room>` initialize the inherited `parentProp parent`
                               // without shadowing it to type `object`.
    bool isAdditive = false;   // `additive` qualifier on a file-scope `property` declaration: emits
                               // I6 `Property additive foo;`, so the property slot accumulates values
                               // across the class hierarchy (obj + ancestors) rather than overriding.
                               // Directive-only in I6 — valid only on a non-extern `property`.
    bool anySet() const {      // true if ANY qualifier was consumed before the current token
        return isReplace || isExplicit || isExtern || isEmitter || isConst || isStatic || isInline
            || isExtend || isAlias || isDefault || isRef || isByVal || isSuperposed || isTypeSealed
            || isAdditive;
    }
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

// Whether an expanded body is trimmed, and whether a trailing ';' goes with the whitespace.
// The two differ where the caller re-adds its own terminator, so they are not interchangeable.
enum class emitterTrim { none, ws, wsSemi };

// What each substitution token stands for at one emitter-body use site. A token left unset is
// not substituted, so a caller supplies only what its context actually defines — the receiver
// alone for a method emitter, receiver + property for an array emitter, and so on.
struct emitterBindings {
    optional<string> self;                  // $self
    optional<string> val;                   // $val
    optional<string> host;                  // $host
    optional<string> prop;                  // $prop
    optional<string> cls;                   // $class
    optional<string> selfsub;               // $selfsub — the verb class's I6 action routine
    // The receiver's Beguile type. Text alone is enough for plain substitution, but $i6Expr must
    // TYPE-CHECK an expression mentioning $self/$val/$host, so it needs the type as well.
    string selfType;
    // Supplies the parameter names for $paramName, paired positionally with args. Naming fn WITHOUT
    // args is the deferred case: the caller stages the body for emit-time parameter substitution, and
    // the funnel must then leave a token alone when a parameter of that name will claim it later.
    const functionDef* fn = nullptr;
    vector<string> args;
    // array<T> element type for the $opref pass. Unset skips the pass; set-but-EMPTY still runs it,
    // because an empty element type is how $opref resolves to 0 for a type that publishes no operator.
    optional<string> elemType;
    string elemContext;                     // method name, for the $opref ambiguity diagnostic
    emitterTrim trim = emitterTrim::none;
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
        set<string> includedFilePaths; // canonical paths of every file the main pass parsed (root +
                                       // all includes). The LSP consults this after an entry-point
                                       // context parse to confirm an opened include was covered.
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
        // Names of all declared member variables whose type is a property-class (derived from the
        // BLR via isPropertyClassType, not hardcoded). Used by the post-emission invariant guard in
        // the driver to flag any raw `obj.member` access that skipped the read emitter.
        std::set<string> collectPropertyClassMemberNames();
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
        // Post-parse safety net: find same-name non-emitter method groups on each
        // object/class and assign mangled i6names so each overload emits as a distinct I6
        // property. Without this, two `foo(int)` and `foo(int,int)` on the same object
        // both emit as `with foo [...]` and I6 errors "Property given twice".
        void assignObjectMethodOverloadMangling();
        // Run the `init` emitter (and apply the declared value) for class-typed object members.
        void recordObjectMemberInits();
        // Check every member bound to a property declared in the typed form
        // (`additive property rawArray<T> p;`) against that declared type.
        void checkTypedPropertyMemberTypes();
        // Post-parse validation of `hide` directives (unresolved → warning). See the .cpp.
        void validateHiddenMembers();
        // Reject a length-changing array operation on a RAW member array (rawArray<T> or
        // array<dictionaryWord>), whose extent is fixed by the property that holds it.
        bool rejectRawMemberLengthOp(const std::string& owner, const std::string& prop,
                                     const std::string& methName, functionDef* func,
                                     statementBlock* body);
        // Lazy version: when a call site has just resolved a method, mangle the entire
        // overload set on the receiver's containing type so call text and emission agree
        // on the property name. Called from resolution paths (bindMethodCall + optional
        // chain) so the mangled i6name is set by the time the call site bakes its text.
        void mangleOverloadSetForReceiver(const string& receiverTypeName, const string& methodName);
        // For each param of `funcDef` whose type is a `byVal class` (not extern, not
        // emitter, not on operator= itself — recursion guard), synthesize a per-(function,
        // param) backing global and wire param.i6name to it. The emitter emits copy-in
        // via operator= at routine entry. `classContext` is the enclosing class name when
        // funcDef is a class member (used to disambiguate same-method-name across classes
        // in the backing-global name); empty for top-level functions.
        void synthesizeParamBackings(functionDef& funcDef, const string& classContext = "");
        bool parsingError(string);   //called when there is an error, to output the error message and the place in the code where it appeared
        void parsingWarning(string); //like parsingError but continues parsing
        void applySchemaDefaults(); // apply beguilerSettingsType default values to any unset settings fields
        void defineSymbol(const string& name, const string& value = ""){ definedSymbols[name] = value; }
        // Like defineSymbol but immutable (the `#declare` path): user code cannot #define/#redef it.
        // Used to surface compile-time #beguilerSettings (e.g. generateBlorb) as #if-testable symbols.
        void declareSymbol(const string& name, const string& value = ""){ declaredSymbols[name] = value; definedSymbols[name] = value; }

        string contextToString(eCompileContext);

        //as we are parsing a file, we enter and exit "contexts" which help the parser determine what is and isn't valid.  For example, the global context allow different things than in the context of a routine.
        void openCompileContext(eCompileContext, statementBlock* body = nullptr);   //entering a new context
        void closeCompileContext(eCompileContext);  //closing out the current context and returning to the previous
        eCompileContext getCurrentCompileContext(); //what is the the current context?
        string processBglConditionals(const string& text); // evaluates ##ifdef/##ifndef/##else/##endif in raw emitter body text
        // The single entry point for turning a raw emitter body into I6 at a use site: evaluates
        // `##if`, then substitutes the tokens the caller bound. Ordering is load-bearing and matches
        // what the open-coded sites did — parameters before `$self`/`$val`, `$opref` last — so a
        // parameter named `self` still wins, as it always has.
        string expandEmitterBody(const i6Block* blk, const emitterBindings& b);

        // Grammar handler methods — standard GrammarHandler signature, called from the grammar table.
        // Declarations
        bool processEnum(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processClass(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processGrammar(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processArray(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRoutine(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processObject(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processInlineObjectStatement(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processVariable(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processTypedProperty(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processTypedObject(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processAliased(vector<token>& t, Qualifiers& q, abstractObject& c);
        // Statements
        bool processBreak(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processContinue(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRtrue(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRfalse(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRtrueWithMessage(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processRfalseWithMessage(vector<token>& t, Qualifiers& q, abstractObject& c);
        void emitRtrueRfalseWithMessage(abstractObject& ctx, const std::string& which);
        bool processReturnVoid(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processReturnExpr(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processIf(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processWhile(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processDo(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processFor(vector<token>& t, Qualifiers& q, abstractObject& c);
        // --- processFor helpers: one per loop form, each parses its own body and appends the loop ---
        // C-style `for(init; cond; incr)`. loopVarName is the init identifier the caller consumed
        // (seeds the init text, tracked as an in-scope loop var); "" when the init starts otherwise.
        bool processForCStyle(const std::string& loopVarName, const sourceLocation& stmtLoc,
                              class functionDef* func, class statementBlock* body);
        // `for(T x in {a, b, c})` from its opening brace; elemVarType may be "auto" (inferred from
        // the first element). Iterates the shared word-based scratch buffer.
        bool processForInLiteralList(const std::string& elemVarName, std::string elemVarType,
                                     const sourceLocation& stmtLoc, class functionDef* func, class statementBlock* body);
        // `for(T x in expr)`: the `1 to 10` range form, arrays, `obj.children` and <string>
        // containers; elemVarType may be "auto" (inferred from the container's element type).
        bool processForIn(const std::string& elemVarName, std::string elemVarType,
                          const sourceLocation& stmtLoc, class functionDef* func, class statementBlock* body);
        bool processSwitch(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processTry(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processThrow(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processDelete(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processDirectiveDispatch(vector<token>& t, Qualifiers& q, abstractObject& c);
        bool processFunc(vector<token>& t, Qualifiers& q, abstractObject& c);

        // Parser handler methods — called from grammar handlers and processNextStatement.
        bool processClassDeclaration(token, bool isExternal, bool isExtend=false, bool isEmitterClass=false, bool isAlias=false, token nameOverride=token(), bool isByVal=false, bool allowNested=false, bool isSuperposed=false);
        // --- processClassDeclaration / processObjectDeclaration / processObjectExtension helpers ---
        // Parse the optional `<T, ...>` type-parameter clause of a class declaration; no-op if absent.
        void parseClassTypeParameters(classDef& newClass, token nameTok, bool isExtend, bool isAlias);
        // Parse the optional pool-size clause (`[N]`, or the extern marker `[]`). Always consumes one
        // token first; leaves `tok` on the token following the clause.
        void parseClassPoolSize(classDef& newClass, token& tok, token nameTok, bool isExternal, bool isExtend, bool isEmitterClass, bool isAlias);
        // Parse the inheritance clause (`for Parent` on an alias class, `: Base, ...` otherwise),
        // rejecting cycles; leaves `tok` on the token following the clause.
        void parseClassInheritance(classDef& newClass, token& tok, token nameTok, bool isAlias);
        // Decode an `operator ...` member name into `name`; leaves `tok` on the symbol that follows it.
        void parseOperatorMemberName(token& tok, token& name);
        // Parse one method member of a class body (`tok` is its '('); true = member fully handled.
        bool parseClassMethodMember(classDef& newClass, token& tok, token name, token returnType, Qualifiers& q, bool isEmitter, bool isExternal, bool isExtend, const string& i6alias = "");
        // Parse one variable/alias member of a class body (`tok` is its '=' or ';'); true = handled.
        bool parseClassVariableMember(classDef& newClass, token& tok, token name, token returnType, Qualifiers& q, bool isEmitter, bool isExtend, const string& i6alias = "");
        // Parse one member of a class body; on return `tok` is the next member's first token (or '}').
        void parseClassMember(classDef& newClass, token& tok, bool isExternal, bool isExtend);
        // Parse one member of an `extern object` body; on return `tok` is the next member's first token.
        void parseExternObjectMember(objectDef& newObj, token& tok);
        // Parse the whole body of an `extern object` / `extern verb` declaration.
        void parseExternObjectBody(objectDef& newObj, verbObjectDef* vod);
        // Parse one member of an object body; on return `tok` is the next member's first token (or '}').
        void parseObjectMember(objectDef& newObj, token& tok);
        // Parse one member of an `extend <object>` body; the last three arguments are the block-local
        // verb-grammar state. On return `tok` is the next member's first token (or '}').
        void parseExtendMember(objectDef* obj, verbObjectDef* vod, token& tok, token nameTok, bool isExternalObj, int& extendBlockPriority, int verbPriorityDefault, bool& extendHadReplaceGrammar);
        // `grammar += { ... }` in an extend body: append the parsed lines to the object and the verb.
        void extendGrammarRuleListAppend(objectDef& obj, const string& memberNameStr, verbObjectDef* vod, int blockPriority);
        // `grammar -= { ... }` in an extend body: remove matching lines (or evict a whole extern word).
        void extendGrammarRuleListRemove(objectDef& obj, const string& memberNameStr, verbObjectDef* vod, bool isExternalObj);
        // `array<T> += / -= { ... }` in an extend body: edit the array member's baked initializer list.
        void extendArrayCompoundAssignment(objectDef& obj, const string& memberNameStr, const string& op);
        // Parse a `hide <name>[.operator <op>][(operandTypes)];` directive inside a class body / extend
        // class, appending it to `cls.hiddenMembers`. 'hide' is the current token (still unconsumed).
        void parseHideDirective(classDef& cls);
        // Enforcement: report an error if `memberName` (optionally its `operatorName`, with the given
        // operand type names) is hidden on the static class `receiverCls` or any of its ancestors.
        // queriedOp="" for a read or method call; "=" (etc.) for an operator/write. No-op if not hidden.
        void enforceHidden(classDef* receiverCls, const std::string& memberName,
                           const std::string& queriedOp, const std::vector<std::string>& operandTypes,
                           const std::string& receiverText);
        bool processEnumDeclaration(token, bool, token nameOverride=token(), bool isExtend=false);
        // Parse one `emitter <type> name(params){ body }` member inside an enum/bnum body (the
        // `emitter` keyword already consumed) into a functionDef, and attach it to the enum's
        // unregistered companion emitter class (created on first use). Reuses the same leaf parsers
        // (processParameterList / getRawTextThroughClosingBrace) as class emitter methods so emission
        // is identical. Only emitter methods are permitted — operators/static/value-emitter error.
        void parseEnumEmitterMethod(enumDef& en);
        // Get (creating on first call) the unregistered companion emitter class for an enum.
        classDef* enumCompanion(enumDef& en);
        // True if a token at an enum-body entry position is a member-qualifier keyword (static, const,
        // inline, …) — i.e. a misplaced member declaration. Only `emitter` methods may be attached to
        // an enum, so these get a clean diagnostic instead of the generic value-grammar error.
        bool isEnumMemberQualifier(token t);
        bool processObjectDeclaration(token typeTok, token nameTok, bool isExtern, string className = "", string i6alias = "", bool hasBody = true, bool isEmitter = false, bool isSuperposed = false);
        // Bake an object of `cls` from an inline-aggregate body `{ ... }`, registered under `objName`.
        // ASSUMES the opening '{' has already been consumed; parses fields up to the matching '}'.
        // Fields are POSITIONAL values (→ the class's `inline` members, in declaration order, base
        // class first) and/or NAMED `field = value` members (positional first; ',' keeps positional,
        // the first ';' enters the named section, then ';'-only). `typeDisplay` names the type in
        // error messages. Used by inline `Type{…}` expressions, inferred `{…}` in typed slots
        // (array-literal elements, object-backed variable initializers), and `inject Type{…}`.
        void bakeInlineObjectAggregate(class classDef* cls, const string& typeDisplay, const string& objName,
                                       class functionDef* func, class statementBlock* body);
        // Parse a `{ v1, v2, ... }` array initializer (the '{' already consumed) for the given
        // elementType, reading through the matching '}'. Bakes nested aggregates: a `{...}` element
        // of an object-backed array becomes an inline object, and a `{...}` element of an
        // array-of-arrays (elementType == "array<...>") becomes an anonymous inner array. Returns the
        // element list; the caller stores it as the array's declaredExpressionValue.
        class initializerList* parseArrayInitializerList(const std::string& elementType,
                                                         class functionDef* func, class statementBlock* body);
        // Bake an anonymous inner array from a `{...}` literal (the '{' already consumed) whose element
        // type is innerElemType. Registers a file-scope arrayDeclaration named anonName; the caller
        // stores a reference to anonName as the outer element (its I6 address).
        void bakeInlineArrayAggregate(const std::string& innerElemType, const std::string& anonName,
                                      class functionDef* func, class statementBlock* body);
        // Parse an alias member (the `alias` qualifier is already consumed; `aliasName` is the member
        // name). Handles both `alias name for Type;` (type alias) and `alias name = Target;` (compile-
        // time value alias — no runtime property). Shared by object-body and extend-body member loops.
        void parseAliasMember(token aliasName, std::vector<typeMember*>& members, const std::string& context);
        bool processEmitterValueDeclaration(token typeTok, token nameTok);
        bool processRoutineDeclaration(token, token, abstractObject& = emptyContainer, bool = false, bool = false, bool = false, bool = false, bool = false);
        bool processVariableDeclaration(token typeTok, token nameTok, token symbol, abstractObject& = emptyContainer, bool isExtern = false, bool isConst = false, string i6alias = "", bool isRef = false, bool isSuperposed = false, bool isAdditive = false);
        // --- processVariableDeclaration phases, in the order processVariableDeclaration runs them ---
        // Warns when a local's name shadows a global, a class/object member or a capturable outer
        // variable; errors on a duplicate in the same scope. No-op at file scope.
        void checkLocalVariableShadowing(const class variableDeclaration& varDecl, class functionDef* func, class statementBlock* body);
        // File-scope `Type name = {…}` / `= Type{…}` on an object-backed class: bakes the fields into
        // the object `name` and consumes the ';'. True when folded — the object IS the declaration.
        bool foldInlineObjectAggregateDeclaration(token dataType, token variableName, token first,
                                                  class functionDef* func, class statementBlock* body);
        // Resolves an `auto` declaration's type from the initializer (honouring `operator auto()`) and
        // updates dataType for downstream checks. No-op when the declaration is not `auto`.
        void inferAutoVariableType(class variableDeclaration& varDecl, token& dataType, bool isAuto, class expression* rhs);
        // Checks the initializer against the declared class type's operator= signatures, capturing an
        // emitter body (or a synthesized `_opeq` dispatch), else the RHS conversion `operator()`.
        void checkVariableInitializerAssignable(class variableDeclaration& varDecl, token dataType,
                                                class expression* rhs, bool isRef);
        // Parses the `= …` / `:= …` initializer onto varDecl (aggregate fold, `{…}` list, interpolated
        // string, or expression). True when the declaration was folded away and is fully handled.
        bool parseVariableInitializer(class variableDeclaration& varDecl, token& dataType, token variableName,
                                      token symbol, bool isAuto, bool isRef, class functionDef* func, class statementBlock* body);
        // Gives a qualifying class-typed local a global backing instance for value-semantics
        // operator= to dispatch against, pointing varDecl.i6name at it.
        void synthesizeClassLocalBacking(class variableDeclaration& varDecl, bool isExternal, bool isConst,
                                         bool isRef, class functionDef* func, class statementBlock* body);
        // Places the declaration in the enclosing body or the global registry; for a local also injects
        // the type's init emitter ahead of it and registers its deinit emitter as a cleanup.
        void registerVariableDeclaration(class variableDeclaration& varDecl, bool isConst, class functionDef* func, class statementBlock* body);
        // File-scope counterpart: records the type's init emitter — and the operator= emitter applying
        // any declared value — in languageService.globalInits for the bglInit routine.
        void recordGlobalVariableInit(class variableDeclaration& varDecl, bool isConst, class functionDef* func, class statementBlock* body);
        bool processArrayDeclaration(token, token, string, token, abstractObject& = emptyContainer, bool = false, bool isSuperposed = false);
        bool processArrayDeclarationFromGeneric(token arrayTok, Qualifiers& q, abstractObject& ctx);  // reads from after '<'
        bool processGrammarDeclaration(token nameOverride=token());
        bool processObjectExtension(token nameTok);
        // Named union: `union Name = A | B [ { members } ]` and `alias union Name = A | B;`.
        // Registers Name as an emitter class carrying unionMembers; an optional `{ … }` body is
        // parsed via processClassDeclaration so members (emitter/static, esp. `print()`) reuse the
        // normal class machinery. `extend Name { … }` (Name a union) routes to the extend path.
        bool processUnionDeclaration(Qualifiers& q);
        // "A|B…" structural expansion of a named-union type name; "" if not a named union. Used by
        // isTypeCompatible to make a named union transparent for assignment/passing.
        std::string unionExpansionOf(const std::string& typeName);
        // Declarative build-time editing of a previously-declared array's baked initializer:
        // `extend <array> { inject/remove/move ... }`. Edits the initializerList in place; the
        // normal array baking then emits the result. Generic (any array of elements/references).
        bool processArrayExtension(class arrayDeclaration* arr);
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
        int anonObjectCounter = 0;          // counter for unique _bglAnonN inline-object names
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
        // Promote a member array too large for an I6 property into a synthesized global.
        void promoteMemberArrayIfOversized(arrayDeclaration& arrDecl);
        bool processArrayMember(vector<typeMember*>& members, const string& ownerDName, verbObjectDef* vodForGrammarRules,
                                abstractObject* ctx = nullptr, Qualifiers* q = nullptr, bool declIsRaw = false);
        void processTypedMember(objectDef& obj, token typeTok, bool isReplace = false, bool isRef = false);
        void processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace = false, string i6alias = "");
        // Consume an optional `as <i6name>` sitting after a method's parameter list (§3.11) and
        // record it on the method. Ignored on operators, whose i6name the overload mangler owns.
        void consumeMethodI6Alias(functionDef& funcDef);
        // Parse an optional `asI6 <i6name>` / `asBgl <beguileName>` clause (§3.11). The two run in
        // opposite directions, which is why each is tied to one side of `extern`: `asI6` CREATES the
        // name I6 will get, so it belongs on a declaration Beguile defines; `alias` MAPS a symbol I6
        // already defines, so it belongs on an `extern`. For `asBgl` the declared token is the I6
        // symbol and `nameTok` is rewritten to the Beguile name. Returns the I6 name, or "".
        string parseI6NameClause(token& nameTok, bool isExtern);
        // A class NAME reaches only the class's `static` members. Reaching a per-instance member
        // through it emitted a property access on the I6 `Class` directive — legal I6 that changes
        // the class DEFAULT, never any instance — so it is rejected instead.
        void rejectNonStaticOnTypeName(const string& headText, classDef* cls, const string& member);
        // The §3.11 direction rule, shared by the grammar-matched form and the inline one.
        // Returns the I6 name; for `alias` it rewrites nameTok to the Beguile name the clause
        // carries. Reports and returns "" when the clause sits on the wrong side of `extern`.
        string applyI6NameClause(token& nameTok, const token& otherTok, bool isAlias, bool isExtern);
        // Pre-scan counterpart: step over `asI6 <name>` / `asBgl <name>` and, for `asBgl`, register
        // the stub under the BEGUILE name the clause supplies rather than the I6 symbol declared
        // before it. Reports nothing — the main pass owns the diagnostics.
        void preScanI6NameClause(string& nameStr);
        void processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue, bool isReplace = false, string i6alias = "", bool isRef = false);
        void processInheritedMember(objectDef& obj, token nameTok);
        bool processGrammarObjectDeclaration(const string& name);  // grammar object with grammarRule members
        vector<grammarLine> parseGrammarLines();
        grammarLine parseGrammarLineContent();  // parses single grammar line (trigger + pattern tokens); assumes '{' consumed

        string parseFuncType();             // reads <ReturnType,ParamType,...> from stream; returns "func<...>"
        string parseArrayTypeTail(const std::string& base);  // base ("array"/"rawarray") read; consumes <Elem>, returns "array<Elem>" (nests + splits ">>")
        // Union types (A | B | ...): after a complete first type is read at a declaration site,
        // if the next token is '|', consume the '|'-separated members and return the canonical
        // union name ("A|B" — members sorted+deduped; a single member collapses). No-op (returns
        // firstType unchanged) when no '|' follows, so call sites stay cheap and low-risk.
        string maybeParseUnionTail(const std::string& firstType);
        string readUnionMemberType();       // reads one complete member type (base + func<>/array<> tail) after a '|'
        string parseLambdaExpr(functionDef* func, statementBlock* body);  // parses lambda, lifts to global, returns lifted name

        bool processStatement(token, abstractObject& = emptyContainer);
        // Per-statement context shared by the processStatement branch methods.
        struct StatementContext {
            sourceLocation src;          // location recorded on every node this statement creates
            functionDef* func = nullptr; // enclosing function, or nullptr at object/global scope
            statementBlock* body = nullptr; // where nodes are pushed; nullptr when there is no body
            string castType;             // `(TypeName)` prefix on the statement; consumed by the method-call path
            string literalTypeName;      // set when the statement head is a typed literal (intliteral, …)
            string literalSelfText;      // the literal's I6 text, used as $self for emitters on it
        };
        // Resolved left-hand side of an assignment statement.
        struct AssignTarget {
            string variableLeft;            // the emitted name written to
            typeDef* leftType = nullptr;    // declared type of the target, or nullptr when unknown
            bool lhsIsRefLocal = false;     // the target is a `ref` slot (pointer-copy assign)
            bool lhsIsByteArray = false;    // the target is an array<char> (byteArray)
            string emitterSelf;             // $self for the target's operator= emitter
            classDef* classType = nullptr;  // class used for operator= dispatch
        };
        // `++x;` / `--x;` — prefix increment or decrement as a whole statement.
        bool processPrefixIncDec(token op, StatementContext& sc);
        // Completes the statement head (typed literal, dotted / `?.` path into `tok`); returns the symbol after it.
        token parseStatementPath(token& tok, StatementContext& sc);
        // `name;` / `a.b.c;` — a value emitter used as a statement; true when it handled the statement.
        bool processValueEmitterStatement(token tok, StatementContext& sc);
        // `name[i] …` — an element write, a chained subscript, or member access on the element.
        bool processSubscriptStatement(token tok, StatementContext& sc);
        // `a[i].member = v;` / `a[i].method(...);` — member access on the subscript result.
        bool processSubscriptMemberAccess(const string& arrPath, expression* indexExpr, StatementContext& sc);
        // `grid[i][j]...[k] = v;` — chained subscript write into an array of arrays.
        bool processChainedSubscriptWrite(const string& arrPath, expression* indexExpr, StatementContext& sc);
        // `a[i] = v;` — a single-subscript element write.
        bool processSubscriptWrite(string arrPath, expression* indexExpr, StatementContext& sc);
        // `lhs = rhs;` / `lhs := rhs;` — plain assignment and reference binding.
        bool processAssignmentStatement(token tok, token symbol, StatementContext& sc);
        // Resolves an assignment's left-hand side (name, type, flags, $self, dispatch class).
        AssignTarget resolveAssignmentTarget(const string& lhsOriginal, StatementContext& sc);
        // Applies `operator =` dispatch (emitter, method, or conversion) to one assignment node.
        void resolveAssignmentOperator(assignmentStatement& a, expression* val, const AssignTarget& t, bool isBindAssign);
        // `lhs op= rhs;` — compound assignment (+=, -=, *=, /=, %=, |=, &=, ^=, <<=, >>=).
        bool processCompoundAssignment(token tok, token symbol, StatementContext& sc);
        // `x.children += { a, b };` — world-model child placement at runtime.
        bool processChildrenPlacement(token tok, token symbol, StatementContext& sc);
        // `arr += { a, b };` — per-element compound op over a brace list; true when it handled the statement.
        bool processArrayBracedCompound(token tok, token symbol, const string& lhs, StatementContext& sc);
        // `x++;` / `x--;` — postfix increment or decrement as a whole statement.
        bool processPostfixIncDec(token tok, token symbol, StatementContext& sc);
        // `name(args);` / `recv.method(args);` — a call used as a statement, with method chaining.
        bool processCallStatement(token tok, StatementContext& sc);
        // Computes the called name of a call statement (`replaced()` rewrite, bare-name qualification).
        string qualifyCallName(token tok, StatementContext& sc);
        // Parses a call statement's argument list, with brace-argument hints from the callee.
        void parseCallArgsWithHints(functionCallStatement& callStmt, StatementContext& sc);
        // `recv.method(args);` — binds a method call statement; true when it emitted the statement itself.
        bool bindMethodCallStatement(functionCallStatement& callStmt, token tok, string& chainReturnType, StatementContext& sc);
        // `name(args);` — binds a global function call statement.
        void bindGlobalCallStatement(functionCallStatement& callStmt, token tok, string& chainReturnType, StatementContext& sc);
        // `…().m1().m2();` — folds chained `.method()` suffixes into the call statement, up to `;`.
        void parseMethodChain(functionCallStatement& callStmt, string& chainReturnType, StatementContext& sc);
        bool processDirective(token, abstractObject& = emptyContainer);
        // One handler per directive; processDirective's switch is the dispatch table over these.
        bool processDirectiveUnrecognized(token directive);                          // shared tail: no case matched, or a case bailed out
        bool directiveInclude(token directive, abstractObject& contextObj);          // #include "file" / #include <libName>
        bool directiveStartup(token directive, abstractObject& contextObj);          // #startup { raw I6 for bglInit() }
        bool directiveUsing(token directive, abstractObject& contextObj);            // #using Class / object / dotted path
        bool directiveEmitFirst(token directive, abstractObject& contextObj);        // #emitfirst { raw I6 }
        bool directiveEmitLast(token directive, abstractObject& contextObj);         // #emitlast { raw I6 }
        bool directiveStoredEmitFirst(token directive, abstractObject& contextObj);  // #storedemitfirst name { raw I6 }
        bool directiveStoredEmitLast(token directive, abstractObject& contextObj);   // #storedemitlast name { raw I6 }
        bool directiveIncludeI6(token directive, abstractObject& contextObj);        // #includei6 "file" / @"file"
        bool directiveI6(token directive, abstractObject& contextObj);               // #i6 — dispatches to the single-line or block form
        bool directiveI6SingleLine(token t, statementBlock* body, const sourceLocation& i6DirLoc);                 // #i6 <rest of line>
        bool directiveI6Block(statementBlock* body, abstractObject& contextObj, const sourceLocation& i6DirLoc);   // #i6 { raw I6 interleaved with #bgl{} }
        void installI6Node(i6RawNode* node, statementBlock* body, const sourceLocation& i6DirLoc);                 // places an #i6 node in the body, or claims its global placeholder
        bool directiveDefine(token directive, abstractObject& contextObj);           // #define and #redef
        bool directiveDeclare(token directive, abstractObject& contextObj);          // #declare — immutable, pre-scan-hoisted define
        bool directiveIf(token directive, abstractObject& contextObj);               // #if cond
        bool directiveElif(token directive, abstractObject& contextObj);             // #elif reached on a taken branch — skip to #endif
        bool directiveElse(token directive, abstractObject& contextObj);             // #else reached on a taken branch — skip to #endif
        bool directiveBglElse(token directive, abstractObject& contextObj);          // ##else reached on a taken branch — skip to ##endif

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
        // parseExpression sub-functions
        void parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body, int parenDepth = 0);
        void parseExprNullCoalescing(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body);
        bool parseExprFunctionCall(expression* expr, const string& callName, bool isSelfCall, functionDef* func, statementBlock* body);
        bool parseExprPrefixNot(expression* expr, token operand, optional<token>& prefetched, functionDef* func, statementBlock* body);
        // State of one parseExpression activation, shared by its branch methods.
        struct ExprParseState {
            expression* expr = nullptr;
            int parenDepth = 0;
            int startParenDepth = 0;
            token cur;                          // token being processed this iteration
            optional<token> prefetched;         // a token a sub-parse produced that the loop must see next
            string castType;  // set when a (TypeName) cast prefix is detected
            // When a cast prefix is followed by '(', the cast applies to the result of the
            // parenthesized expression, not to the first identifier inside. Push castType
            // onto this stack on parenOpen and pop/apply on the matching parenClose. The
            // stack entry pairs the saved type with the parenDepth at which the cast was
            // queued, so nested casts like `(int)((float)f + (float)g)` resolve correctly.
            struct PendingParenCast { string castType; int parenDepthAtPush; };
            vector<PendingParenCast> parenCastStack;
            // Pending ternary state: when '?' is encountered, the condition and true branch are
            // captured, then the false branch is collected by continuing the main loop. This avoids
            // sub-parsing the false branch, which would lose the caller's paren tracking state.
            struct PendingTernary {
                string condText;
                string trueText;
                string trueType;
                string tempName;
                int parenDepthAtQuestion;  // paren depth when '?' was encountered
                vector<string> prefixParens;  // structural '(' tokens to restore after assembly
            };
            vector<PendingTernary> pendingTernaries;
            const vector<string>* terminators = nullptr;
            functionDef* func = nullptr;
            statementBlock* body = nullptr;
        };
        // What the main loop does once a branch method returns: fall through to the loop's own
        // `cur = exprNext(st)` (Advance), skip it because the branch already advanced (Continue),
        // or leave the loop (Break).
        enum class ExprStep { Advance, Continue, Break };
        // Shared helpers of one parseExpression activation.
        bool exprIsTerminator(const ExprParseState& st, const token& t);
        token exprNext(ExprParseState& st);
        void exprEmitRawBinaryOp(ExprParseState& st, const string& opTok);
        void exprAssembleTernary(ExprParseState& st);
        string applyCastConversion(const string& srcText, const string& srcType, const string& targetType);
        // One per branch of parseExpression's main loop, dispatched on the current token's kind.
        ExprStep parseExprOperatorRef(ExprParseState& st);
        ExprStep parseExprParenOpen(ExprParseState& st);
        ExprStep parseExprParenClose(ExprParseState& st);
        ExprStep parseExprIntLiteral(ExprParseState& st);
        ExprStep parseExprFloatLiteral(ExprParseState& st);
        ExprStep parseExprNew(ExprParseState& st);
        ExprStep parseExprInlineObject(ExprParseState& st);
        ExprStep parseExprIdentifier(ExprParseState& st);
        ExprStep parseExprOperator(ExprParseState& st);
        ExprStep parseExprDictionaryWord(ExprParseState& st);
        ExprStep parseExprDotChain(ExprParseState& st);
        ExprStep parseExprTernaryStart(ExprParseState& st);
        ExprStep parseExprDirective(ExprParseState& st);
        // Sub-branches of parseExprIdentifier, dispatched on the token AFTER the identifier.
        ExprStep parseExprSubscript(ExprParseState& st, token& next);
        ExprStep parseExprCall(ExprParseState& st, token& next);
        ExprStep parseExprOptionalChain(ExprParseState& st, token& next);
        ExprStep parseExprMemberAccess(ExprParseState& st, token& next);
        ExprStep parseExprPostfixQuery(ExprParseState& st, token& next);
        ExprStep parseExprBareIdentifier(ExprParseState& st, token& next);
        // Sub-branches of parseExprMemberAccess, dispatched on the token AFTER the member name.
        ExprStep parseExprSelfMember(ExprParseState& st, token& member, token& afterMember);
        ExprStep parseExprMemberCall(ExprParseState& st, token& member, token& afterMember);
        ExprStep parseExprMemberSubscript(ExprParseState& st, token& member, token& afterMember);
        ExprStep parseExprMemberRead(ExprParseState& st, token& member, token& afterMember);
        ExprStep parseExprMemberPropertyRead(ExprParseState& st, token& member);
        // Sub-branches of parseExprDotChain.
        ExprStep parseExprDotChainRead(ExprParseState& st, token& member, token& afterMember);
        ExprStep parseExprDotChainCall(ExprParseState& st, token& member, token& afterMember);

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

        // True iff `name` resolves as an inherited variableDeclaration member of currentObject's
        // class hierarchy AND is NOT shadowed by a local, param, or own member of the current
        // function/object. Used at emitter-substitution sites to rewrite `$self` from the bare
        // receiver name to `self` — so `attributes.hasnt(light)` inside a room's method body
        // emits `(self hasnt light)` rather than the broken `(attributes hasnt light)`.
        bool isInheritedObjectMember(const string& name, functionDef* func, statementBlock* body);

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
        // For a class that exposes a concrete operator[] (e.g. string's `char operator[](int)`)
        // but has no declared array element type, derive the element type from that operator's
        // return type so subscript resolves. Returns "" if the class has no such operator (or
        // only a type-parameter return, which the array<T> path handles instead).
        string inferSubscriptElementType(classDef* cls);
        // memberHint: forwarded to the underlying resolveIdentifierType for the path's head.
        string resolvePathType(string path, functionDef* func, statementBlock* body, const string& memberHint = "");
        // True if `typeName` resolves to a class (incl. base hierarchy) or objectDef that exposes
        // a member (field/method) named `memberName`. Returns false for primitives, enums, and
        // unknown types. Used by the resolver for member-aware tie-breaking.
        bool typeHasMember(const string& typeName, const string& memberName);
        // memberHint: same semantics as resolveIdentifierType — prefer candidates whose type
        // exposes the named member, falling back to first-match if none satisfy.
        string qualifyIdentifier(string name, functionDef* func, statementBlock* body, const string& memberHint = "");
        // ── Tiered identifier resolution ──────────────────────────────────────────
        // qualifyIdentifier and resolveIdentifierType are ordered scope searches. Each tier
        // below examines exactly ONE scope; the parent calls them in declaration order and the
        // order is load-bearing. Convention for both functions: a tier returns optional<string>
        // — engaged means "resolved here, use this value" (an engaged but EMPTY string is a
        // real resolution, not a miss); disengaged means "not in this scope, try the next
        // tier". A tier that contributes to the file-scope candidate set instead appends to
        // the caller's vector and returns void.

        // One file-scope match for resolveIdentifierType: the type plus where it came from.
        struct TypeCandidate { string type; string origin; bool isEnum; bool isObject = false; bool isFunction = false; };
        // One file-scope match for qualifyIdentifier: also carries the I6 emission to use.
        struct QualifyCandidate { string qualified; string type; string origin; bool isEnum; bool isObject = false; bool isFunction = false; };
        // A dotted name split for the qualifyDotted* tiers. head/tail keep the user's case;
        // firstSeg/rest are the lowercased first segment of the tail and the remainder.
        struct DottedPath { string head; string tail; string qualifiedHead; string firstSeg; string rest; };

        // resolveIdentifierType tiers, in call order.
        optional<string> resolveTypeFromParams(const string& name, functionDef* func);
        optional<string> resolveTypeFromBodyLocals(const string& name, statementBlock* body);
        optional<string> resolveTypeFromAncestorBlocks(const string& name, statementBlock* body);
        optional<string> resolveTypeFromCurrentObject(const string& name);
        optional<string> resolveTypeFromCurrentClass(const string& name);
        optional<string> resolveTypeFromCaptures(const string& name);
        // File scope: append every global (variable/function/object/class/enum) named `name`.
        void collectTypeCandidatesFromGlobals(const string& name, vector<TypeCandidate>& candidates);
        // Dedupe, apply the precedence rules, and pick one type — or raise the ambiguity error.
        optional<string> selectTypeCandidate(const string& name, const string& memberHint,
                                             vector<TypeCandidate>& candidates);

        // qualifyIdentifier's dotted-path branch. Terminal, not a fall-through tier: it always
        // yields the qualified path, so it returns the string directly ("" = head unresolved).
        string qualifyDottedPath(const string& name, size_t dot, functionDef* func,
                                 statementBlock* body, bool forceGlobalScope);
        // Dotted-path tiers, in call order: bare #using namespace alias head, then class head,
        // then object head.
        optional<string> qualifyDottedHeadViaUsingAlias(const DottedPath& p, functionDef* func,
                                                        statementBlock* body, bool forceGlobalScope);
        optional<string> qualifyDottedViaClassHead(const DottedPath& p, functionDef* func,
                                                   statementBlock* body);
        optional<string> qualifyDottedViaObjectHead(const DottedPath& p, functionDef* func,
                                                    statementBlock* body);
        // qualifyIdentifier's bare-name tiers, in call order.
        optional<string> qualifyFromParams(const string& name, functionDef* func);
        optional<string> qualifyFromBodyLocals(const string& name, statementBlock* body);
        optional<string> qualifyFromAncestorBlocks(const string& name, statementBlock* body);
        optional<string> qualifyFromCurrentObject(const string& name);
        optional<string> qualifyFromCurrentClass(const string& name);
        optional<string> qualifyFromCaptures(const string& name);
        // File scope: append every global / #using-imported match named `name`.
        void collectQualifyCandidatesFromGlobals(const string& name, vector<QualifyCandidate>& candidates);
        void collectQualifyCandidatesFromUsingClassImports(const string& name, functionDef* func,
                                                           statementBlock* body,
                                                           vector<QualifyCandidate>& candidates);
        void collectQualifyCandidatesFromUsingObjectImports(const string& name, functionDef* func,
                                                            statementBlock* body,
                                                            vector<QualifyCandidate>& candidates);
        // Dedupe, apply the precedence rules and the shadow warning, and pick one emission —
        // or raise the ambiguity error.
        optional<string> selectQualifiedCandidate(const string& name, const string& memberHint,
                                                  bool forceGlobalScope,
                                                  vector<QualifyCandidate>& candidates);
        // Seams lifted out of isTypeCompatible / bindMethodCall.
        // True when paramType's class hierarchy has an `operator =` that accepts argType —
        // exact name match, base-class upcast, then the `var` wildcard, in that order.
        bool compatibleViaAssignmentOperator(const string& argType, const string& paramType);
        // `obj.member()` where `member` is a `func<...>` property: a synthesized functionDef
        // carrying the func's return type, so the call emits verbatim. nullptr when absent.
        functionDef* synthesizeFuncPropertyCall(const string& objType, const string& methodName);
        // Raise the "no overload accepts these argument types" error, listing each same-arity
        // candidate and its first mismatching parameter.
        void reportMethodOverloadMismatch(const string& objType, const string& methodName,
                                          vector<expression*>& args);
        // ──────────────────────────────────────────────────────────────────────────
        // If `name` resolves to an object-property (member), split its qualified form
        // ("owner.prop") into owner + prop and return true; globals/locals return false.
        // Drives member-array access through the orLibrary property-array convention.
        // Emitted I6 property name for a member, honouring `Type member as <i6name>;`.
        std::string memberI6Name(const std::string& recvTypeName, const std::string& memberName);
        // Is a member array declared `ref`? It then holds a pointer, addressed as a value.
        bool memberArrayIsRef(const std::string& ownerName, const std::string& propName,
                              functionDef* func, statementBlock* body);
        // Does a member array carry the trailing length slot? Must match the emitter's rule.
        bool memberArrayIsTracked(const std::string& ownerName, const std::string& propName,
                                  functionDef* func, statementBlock* body);
        bool splitQualifiedMember(const string& name, functionDef* func, statementBlock* body,
                                  string& ownerOut, string& propOut);
        // `$elemop(<op>)` — the element type's implementation of one operator, as either a
        // free-routine name (static) or a property name (instance), or "0". Callers tell the
        // two apart at runtime via metaclass()==Routine.
        // `$opref(<op>[, <operandType>])` — a callable reference to one of a type's operators:
        // a free-routine name (static), a property name (instance), or "" when none is
        // referenceable. Emitters are never referenceable — they inline and have no address.
        // `isStaticOut`, when given, reports whether the match was a `static` operator (a real
        // routine address) rather than an instance one (a property name). The user-facing
        // `Type::operator <op>` form requires an address and rejects the property form.
        // Narrow scope test for computed property access — see the definition for why
        // resolveIdentifierType is too broad to use here.
        bool isPropertyValuedLocal(const string& name, functionDef* func, statementBlock* body);
        string operatorRef(const string& typeName, const string& opName,
                           const string& preferOperand = "", bool* isStaticOut = nullptr);
        // Replace every `$elemop(<op>)` in an emitter body with that operator's callable form
        // for `elemType`. One substitution covers every operation, so a site cannot be
        // partially wired the way four separate tokens could.
        string substituteElemOps(const string& body, const string& elemType,
                                 const string& contextName = "");
        // Replace every `$i6Name(<path>[(<types>)])` in an emitter body with the identifier Inform 6
        // knows that declaration by — the `as` alias, the `_bgl_<class>_<method>` static mangling, or
        // the plain name. Lets a raw body reach a Beguile declaration without hard-coding how the
        // compiler spells it. Unresolvable or non-referenceable paths are compile-time errors.
        string substituteI6Names(const string& body);
        string resolveI6Name(const string& spec);   // one `$i6Name(...)` payload → its I6 identifier
        // Replace every `$i6Expr(<beguile expression>)` with the I6 that expression emits. The
        // expression is parsed and type-checked with the body's tokens bound as typed values, so
        // overload resolution and the callee's own `##if` gating apply as at any other use site.
        // `func`/`enclosing` are supplied when the text is an #i6 island inside a routine, so the
        // payload can name that routine's locals; emitter bodies pass neither, since an emitter's
        // only visible names are its own tokens.
        string substituteI6Exprs(const string& body, const emitterBindings& b,
                                 functionDef* func = nullptr, statementBlock* enclosing = nullptr);
        // Resolve `$i6Name(...)` / `$i6Expr(...)` in a raw-I6 island (§15.2). An island has no
        // receiver and no parameters, so these two are the only tokens it can carry.
        string resolveIslandTokens(const string& raw, functionDef* func, statementBlock* enclosing);
        // Emitter bodies currently being expanded, outermost first. A body reached twice is a cycle;
        // a chain past kMaxEmitterDepth is a runaway. Both are errors rather than a stack overflow.
        vector<const i6Block*> emitterExpansionChain;
        static constexpr size_t kMaxEmitterDepth = 8;
        bool isTypeCompatible(string argType, string paramType);
        static bool isUnionType(const std::string& t);              // true if t has a top-level '|' (a union type name)
        static std::vector<std::string> splitUnionType(const std::string& t);  // split a union name into its member type names
        // Canonical name for a union of these members: flattened, sorted and deduped, so the same
        // set always names the same type and a single member collapses back to itself.
        static std::string canonicalUnionOf(std::vector<std::string> members);
        // Element-type compatibility for array/list initializers: isTypeCompatible plus an
        // initializer-only relaxation letting a byte array (`array<char>`) accept integer literals.
        bool isArrayElementCompatible(string argType, string elementType);
        // For a byte array (`array<char>`) element, if `elem` is an integer literal outside 0..255,
        // raise a compile error. No-op for non-char element types and non-literal values.
        void checkByteElementRange(expression* elem, const string& elementType);
        // Returns the classDef whose member/operator hierarchy applies to `typeName`-typed
        // values. For a classDef-typed name, returns that classDef. For an objectDef-typed
        // name, returns the objectDef's `objectClass`. nullptr if `typeName` isn't a class
        // or instance type. Use anywhere dispatch needs to walk methods/operators by type.
        classDef* getDispatchClass(const string& typeName);
        // Property-class discriminator (generic; the specifics live in the BLR). A "property-class"
        // is an emitter class whose no-arg, non-explicit `operator()` read emitter reads through the
        // OWNER and transforms it (body references `$self` and isn't a bare `$self`/`$val` identity —
        // e.g. parentProp's `operator(){ parent($self) }`). Such a member must never be emitted as a
        // raw `obj.member` property read; it must dispatch through the emitter.
        bool isPropertyClassReadEmitter(typeMember* m);
        bool isPropertyClassType(const string& typeName);
        // Substitute a property-class member's `operator()` read emitter with $self = objText,
        // returning the emitted text (e.g. `parent(<objText>)`) and, via outRetType, the emitter's
        // return type. Returns "" when memberType isn't a property-class. Generic — no member name.
        string applyPropertyClassRead(const string& objText, const string& memberType, string& outRetType);
        // Fluent property-class read chain: IDENT.m1.m2… (head + >=2 members, ALL property-classes of
        // the running type) → nested reads, e.g. `obj.parent.parent` → `parent(parent(obj))`. Peek-only
        // until it commits; returns false (consuming nothing) if the path isn't a pure property-class
        // read chain, letting other resolution handle it. Emission + result type via out-params.
        bool tryConsumePropertyClassReadChain(token first, functionDef* func, statementBlock* body,
                                              string& outEmission, string& outType);
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
        // Per-argument type hints for inferring a BARE inline-object aggregate `foo({ … })`: the
        // object-backed class each argument position (or named param) resolves to, when all viable
        // overloads agree. `positional[i]` / `named[name]` == nullptr means "cannot infer here"
        // (no agreement, not object-backed, or out of range) → a bare `{` there is an error.
        struct BraceArgHints {
            vector<classDef*> positional;
            map<string, classDef*> named;
        };
        // Gather candidate callees by name (globals) or by receiver type + name (methods), mirroring
        // the candidate-gathering in resolveGlobalCall / resolveMethod (collection only, no scoring).
        vector<functionDef*> collectGlobalCandidates(const string& name);
        vector<functionDef*> collectMethodCandidates(const string& typeName, const string& methodName);
        // Compute the per-position / per-name agreed object-backed class from candidate callees.
        BraceArgHints braceArgHints(const vector<functionDef*>& candidates);

        ParsedArgList parseCallArgList(functionDef* func, statementBlock* body, const BraceArgHints& braceHints = {});

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
        // Enclosing object while synthesizing an inline accessor's class body (`auto name = { … }`);
        // `outer` in the accessor's operator bodies resolves to it (compile-time host capture).
        objectDef* accessorOuter = nullptr;
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
        // Snapshot of definedSymbols taken at the top-level pre-scan entry — captures only the
        // programmatically-seeded symbols (version + target defines set before pre-scan), NOT the
        // source `#define`s the pre-scan then accumulates. Restored at the top-level main-pass
        // entry so the main pass re-derives source defines linearly (position-dependent), instead
        // of inheriting the pre-scan's end-of-file state (which would let a late `#define`
        // retroactively enable an earlier `#if`). See preScanFile / parseFile.
        map<string,string> definedSymbolsPreScanSeed;
        bool hasPreScanSeed = false;
        // Symbols declared via #declare: like #define but ORDER-INDEPENDENT and IMMUTABLE. Collected
        // across the whole include graph during the pre-scan and hoisted into definedSymbols at the
        // start of the main pass, so `#if NAME` is true everywhere regardless of where (or in which
        // later-included file) the #declare appears. Cannot be #undef'd or redefined. Lets an early
        // core file detect an optional binding/library that is #included later (e.g. the standard
        // library advertising a capability the core file conditionally uses).
        map<string,string> declaredSymbols;
        bool evaluateCondition(const string& expr);   // evaluates a #if boolean expression
        void skipConditionalBlock(abstractObject& ctx);    // skips tokens until #elif/#else/#endif at depth 0
        void skipBglConditionalBlock(abstractObject& ctx); // skips tokens until ##else/##endif at depth 0

        // Pre-scanner state
        set<string> preScanOnceFiles;
        int preScanDepth = 0;
        void preScanDirective(token tok);
        void preScanGlobalLoop();         // walks tokens in the currently-open file, registering type/global stubs until EOF
        // One handler per declaration shape preScanGlobalLoop recognizes; each consumes the whole declaration.
        void preScanExtend(token& tok);                                             // extend enum / extend <object> / extend [extern] class
        bool preScanGlobalEmitterObject(token& tok);                                // `emitter Foo { … }`; false (nothing consumed) if no '{' follows
        void preScanClassHead(bool isExtern, bool isEmitter, bool isAliasClass);    // class declaration + its member stubs
        void preScanEnum(token& tok, bool isExtern);                                // enum / bnum declaration
        void preScanObject(token& tok, bool isExtern);                              // `object Name {…}` / `ClassName Name {…}` / `ClassName Name;`
        void preScanProperty(bool isExtern);                                        // `property [type] name;`
        void preScanAttribute(bool isExtern);                                       // `attribute name;`
        void preScanUnion();                                                        // `union Name = A | B [ {…} | ; ]`
        void preScanTypedDecl(token& tok, bool isExtern, bool isEmitter);           // typed head: function, emitter value, type-named object, or global variable
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
        // Pre-scan one `emitter <type> name(params){body}` member inside an enum body (the `emitter`
        // keyword already consumed) into a companion functionDef stub — capturing params and the
        // emitter body so a forward call before the enum's main-pass declaration still expands
        // (order-independence, mirroring class emitter members). The main pass replaces the stub.
        void preScanEnumEmitterMember(enumDef& en);
        // Drain one malformed enum-body member entry: skip to its `{ … }` body (consuming it) or, if
        // there is none, up to the next enum-level separator/close — without erroring, so the pre-scan
        // survives a bad form and the main pass emits the precise diagnostic.
        void preScanSkipEnumMemberBody();

        // Order-independent `extend`: an `extend <obj>` whose target object isn't declared yet
        // when pre-scan reaches it (e.g. platform-core `extend bgl {...}` included before the
        // `object bgl` declaration) is captured here — object name + raw body text — and replayed
        // after the whole include tree has been pre-scanned (drainDeferredObjectExtends), so the
        // target is registered by then. This makes declarative `extend` order-free; only `#define`
        // remains order-dependent.
        struct DeferredObjectExtend { std::string objName; std::string body; std::string virtualName; int startLine; };
        vector<DeferredObjectExtend> deferredObjectExtends;
        // Register the members added by an `extend <obj>` body onto obj during pre-scan. Assumes the
        // stream is positioned right after the `extend <name>` (scans to '{', processes through '}').
        // Registers function stubs and auto/alias namespace-redirect members (`emitter auto
        // X = Class;`, `auto X = obj;`, `alias X = ...;`) as isExternal/isPrePassStub variable stubs
        // so dotted resolution (bgl.asm, bgl.util.*) works regardless of source order. The main pass
        // reconciles these stubs (processObjectExtension drops a stub once its real member lands).
        void preScanExtendObjectMembers(objectDef* obj);
        void drainDeferredObjectExtends();  // replay queued forward-extends onto now-registered targets

        vector<statement*> pendingInjections;  // pre-statements to emit before next main statement (e.g. from ternary lowering)
        vector<statement*> postInjections;     // post-statements to emit after next main statement (e.g. closing braces for ?. guards)

};

extern bglParser parser;