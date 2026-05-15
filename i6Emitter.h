#pragma once
#include <string>
#include <sstream>
#include <stack>
#include <set>
#include <map>
#include <vector>
#include "token.h"
#include "typeDef.h"

using namespace std;

class i6Emitter{
    public:
        stringstream out;
        vector<pair<string,string>>* currentCleanups = nullptr; // set during emitFunction; used by emitStatement for return
        vector<tuple<int,string,int>> sourceMap;  // (i6Line, bglFile, bglLine)
        set<string> declaredVerbWords;             // tracks which I6 trigger words have been Verb-declared
        // built-in I6 templates loaded from beguilib/_builtins.i6b
        map<string, pair<vector<string>, string>> builtinTemplates;
        void loadBuiltinTemplates(string path);
        void applyTemplate(string name, map<string,string> args, string indent);
        void to(ostream&);

        // Z-machine local variable spill state — active during a single function's emission
        string currentTarget;                              // lowercase: "glulx","z3","z5","z8"
        map<string,string> currentSpillAliases;            // varName → "_bglFrm-->N" or "_bglXPn"
        // Original-case display names for params/locals of the function being emitted, keyed by
        // canonical (lowercased) name. Populated by buildSpillMap, consulted by spillName.
        // When a name has no spill alias, the display form is used so user case is preserved.
        map<string,string> currentDisplayNames;
        // Local/param renames for I6 property-name shadowing avoidance. When a function-local or
        // parameter has the same name as something accessed as a property in the function body
        // (`obj.NAME`), I6 may resolve `obj.NAME` as indirect property access through the local
        // instead of the named property. We mangle the local to `_l_<name>` so the property
        // access stays direct. Populated by buildLocalRenameMap, consulted by spillName.
        map<string,string> currentLocalRenames;
        int currentSpillCount = 0;
        bool frameAllocEmitted = false;
        int xpGlobalsNeeded = 0;                           // how many _bglXPn globals were emitted
        int framePoolSize = 64;                            // configurable via beguilerSettings framePoolSize

        static string replaceWord(string str, const string& from, const string& to);
        void buildSpillMap(functionDef* fd);
        void clearSpillMap();
        // Build the local-rename map for property-shadow avoidance. Walks the function body
        // collecting every name accessed as a property (`obj.NAME` in any expression or raw
        // text), then for any param/local whose name appears in that set, registers a mangled
        // alias `_l_<displayName>`. Called from buildSpillMap so renames are in place before
        // any signature or body emission.
        void buildLocalRenameMap(functionDef* fd);
        string exprText(expression* expr);
        string spillName(const string& name);
        string spillWord(const string& text);
        bool funcNeedsSpill(functionDef* fd);
        // Recursively collect variableDeclarations from a function body, walking into the
        // sub-blocks of control-flow statements (if/for/while/do/switch/try-catch). Deduped by name
        // so the first occurrence wins — matches I6's single-declaration-per-header requirement.
        void collectBodyLocals(statementBlock* body, vector<variableDeclaration*>& out, set<string>& seen);

        void emit(vector<typeDef*>&);
        void generateI6(typeDef*);
        void emitICL(beguilerSettingsDef*);
        void emitSettingsConstants(beguilerSettingsDef*);
        void emitEnum(enumDef*);
        void emitClass(classDef*);
        void emitObject(objectDef*);
        void emitMember(typeMember*);
        void emitGlobal(variableDeclaration*);
        // Walks `cls`'s stored fields. For each field whose type is a different statically-
        // instantiable class (no init emitter), emits a hidden backing global of that field's
        // type to `out` and returns a `with field _backing, ...` clause string suitable for
        // attaching to the instance declaration. Recurses into the backing's own fields.
        // `visited` carries the class-instantiation path to break cycles (including the
        // owner-class itself, so self-typed fields are left at default — these are
        // intended as references managed elsewhere).
        string synthesizeFieldBackings(classDef* cls, const string& instanceName, set<classDef*>& visited);
        void emitFunction(functionDef*);
        void emitStatement(statement*, string indent);
        // Emit a raw I6 text block while pushing per-line entries into the source map so
        // I6 diagnostics inside the block remap to the correct .bgl line. Used for `#i6{}`
        // raw blocks (multi-line and single-line) and emitter-body inlinings.
        void emitRawTextWithSourceMap(const string& text, const sourceLocation& srcStart);
        void emitInterpolatedSegments(const vector<interpolatedSegment>& segments, string indent);
        void emitInterpolatedEmitterBody(const string& body, const string& paramName, const vector<interpolatedSegment>& segments, string indent);
        void emitVerbObject(verbObjectDef*);
        void emitGrammarRuleListDecl(grammarRuleListDecl*);
        // Lift compile-time-only fields (`meta`, `priority`) from all verb instances into
        // `verbObjectDef.isMeta` / `verbObjectDef.priority`. Idempotent. Called once at the
        // start of `emit()` so the lifted values are available to ALL emission paths
        // regardless of source order (grammar objects can precede their target verb decls).
        void liftAllVerbCompileTimeFields();
        // Partition a verb's grammar contributions against the verb's anchor and emit them
        // as the right mix of Verb / Extend first / Extend directives. Shared by
        // emitVerbObject (own + extends) and emitGrammarRuleListDecl (grammar-object rules).
        void emitVerbGrammar(const string& verbName, int anchor, bool isMeta, const vector<grammarLine>& lines);
        // I6 directive form used when a trigger word is encountered after its first declaration:
        //   First   → `Extend 'w' first` (insert before existing rules — higher matching priority)
        //   Last    → `Extend 'w'`       (append after existing — default last)
        //   Replace → `Extend 'w' replace` (wipe existing rules for 'w', substitute these)
        // First-occurrence of a trigger word always emits as `Verb 'w'` regardless of mode.
        enum class extendDirective { First, Last, Replace };
        void emitGrammarLines(const string& verbName, const vector<grammarLine>& lines, bool isMeta = false, extendDirective mode = extendDirective::First);
        int currentLine();
        void writeSourceMap(const string& path);
        void writeSymbolTable(const string& path);
        void writeTypesFile(const string& path);
        void writeDebugBundle(const string& path);

};

extern i6Emitter emitter;