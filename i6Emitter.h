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
        // When non-null, sourceMap pushes are redirected here instead of `sourceMap`. Set while
        // capturing a `superposed` body so its entries are recorded with capture-relative i6Lines,
        // then re-based onto the real output line when resolvedOutput() splices the block in.
        vector<tuple<int,string,int>>* sourceMapTarget = nullptr;
        void pushSourceMap(int i6Line, const string& bglFile, int bglLine){
            (sourceMapTarget ? *sourceMapTarget : sourceMap).push_back({i6Line, bglFile, bglLine});
        }
        // `superposed` routines: captured out-of-band during emitFunction (name → full routine text),
        // then appended by resolvedOutput() only if referenced (called) in the final output. The
        // reentrancy flag lets emitFunction re-emit the body into a side buffer without recursing
        // into the capture branch. `superposedBlockMaps` holds each block's capture-relative sourceMap
        // entries; resolvedOutput() re-bases them onto the real output position at splice time so the
        // debug map anchors on the routine's actual `.inf` lines (not the splice-region line).
        // `static` class methods emit as free routines under this name; the same mangling is
        // used by the parser side so a lookup can name the routine without re-deriving it.
        static std::string staticRoutineName(classDef* cls, functionDef* fd){
            // Operators carry a mangled i6name ("==" → "_opeqeq"); a bare operator name is
            // not a legal I6 identifier, so prefer i6name whenever one was assigned.
            const std::string& n = fd->i6name.empty() ? fd->dName() : fd->i6name;
            std::string base = "_bgl_" + cls->dName() + "_" + n;
            // Overloaded statics would otherwise all collapse onto `base` and emit duplicate
            // I6 routines. Only when the class declares more than one static under that name
            // is a parameter-type discriminator appended, so single statics keep the short
            // name (and their existing emission) unchanged.
            int sameName = 0;
            for(typeMember* m : cls->members){
                auto* o = dynamic_cast<functionDef*>(m);
                if(o == nullptr || !o->isStatic) continue;
                if((o->i6name.empty() ? o->dName() : o->i6name) == n) sameName++;
            }
            if(sameName < 2) return base;
            for(paramDef* p : fd->params){
                std::string t = p->type.name;
                for(char& c : t) c = isalnum((unsigned char)c) ? tolower((unsigned char)c) : '_';
                base += "_" + t;
            }
            return base;
        }
        void emitStaticClassRoutines(classDef* classNode);
        void emitAllStaticClassRoutines();
        map<string,string> superposedBlocks;
        map<string,vector<tuple<int,string,int>>> superposedBlockMaps;
        // For superposed CLASS blocks (keyed like superposedBlocks): the I6 names of their superposed
        // base classes. Unlike objects, an I6 `Class` directive must physically precede any class that
        // derives from it, so resolvedOutput() revives a referenced class's superposed bases FIRST.
        map<string,vector<string>> superposedClassBaseNames;
        bool emittingSuperposedBody = false;
        set<classDef*> emittedClasses;             // classes whose I6 `Class` directive has been written (shared by Pass-3 + create+populate)
        // Globals already declared ahead of bglInit (byte-array pointers, and class-typed
        // globals whose value is applied in bglInit through operator=); pass 3 must not
        // re-declare them.
        set<string> earlyDeclaredGlobals;
        // Storage name for a member array promoted out of its property (see
        // promoteMemberArrayIfOversized). One per owning instance.
        static std::string promotedArrayName(const std::string& owner, const std::string& prop){
            return "_bglPromoted_" + owner + "_" + prop;
        }
        void emitDeferredBackingClass(classDef*);  // emit a superposed accessor class before its baked backing instance (once)
        set<string> declaredVerbWords;             // tracks which I6 trigger words have been Verb-declared
        set<string> evictedEmitted;                // evicted library words already emitted as `Extend only 'w' replace`
        void emitEvictions();                      // emit empty `Extend only 'w' replace;` for evicted words no verb reclaimed
        set<string> splitEmitted;                  // `only`-split library words already peeled off via `Extend only 'w'`
        bool isGroupedExternWord(const string& w); // true if w is claimed by an extern verb with >1 trigger word (has synonyms)
        // built-in I6 templates loaded from beguilib/_builtins.i6b
        map<string, pair<vector<string>, string>> builtinTemplates;
        // Per-template ##triggerEmitter <names> annotations, lowercased. When a template
        // is applied, each listed name is inserted into languageService.firedStoredNames,
        // gating emission of #storedEmitFirst/#storedEmitLast blocks of the same name.
        map<string, vector<string>> builtinTemplateTriggers;
        void loadBuiltinTemplates(string path);
        void applyTemplate(string name, map<string,string> args, string indent);
        void to(ostream&);
        // Resolve the buffered output: substitute the stored-emit-first/last placeholders
        // with the concatenated bodies of stored blocks whose names appear in firedStoredNames.
        // Called by beguiler.cpp immediately before writing the .inf file.
        string resolvedOutput();

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
        // Per-routine snapshots of the spill map, persisted from currentSpillAliases at emit time so
        // writeDebugBundle can record each local's storage location (the transient maps are cleared
        // between routines). Keyed by the routine's I6 name. `routineSpillCounts[name] > 0` means the
        // routine has the synthetic `_bglFrm` frame pointer, which the debugger should hide.
        map<string, map<string,string>> routineSpillAliases;   // routineI6Name → localName → "_bglFrm-->N"/"_bglXPn"
        map<string, int>                routineSpillCounts;    // routineI6Name → frame-pool slot count
        int xpGlobalsNeeded = 0;                           // how many _bglXPn globals were emitted
        int framePoolSize = 64;                            // configurable via beguilerSettings framePoolSize

        // Names of sized-uninitialized Beguile-declared word arrays. The compiler emits these
        // with the compact `Array foo table N+2;` form (all zeros — far smaller in .inf source
        // than an explicit zero list for large arrays) and registers the name here so bglInit
        // can write the $9084 magic at startup. List-initialized arrays bake the magic into
        // the initializer values and don't need to be registered.
        // True when bglInit() has work to do — a length header to stamp, a `#startup` block, or a
        // deferred global initializer. Drives the unreachable-init warning in resolvedOutput().
        bool bglInitHasWork = false;
        vector<string> trackedArraysNeedingMagicInit;
        // Byte-array analog of trackedArraysNeedingMagicInit. When `<buf>` is included, sized-
        // uninitialized `array<char>` declarations get 4 trailing bytes (length-hi, length-lo,
        // magic-hi $90, magic-lo $84) so `_bglBuf` can detect tracked bufs and read length.
        // Names registered here get their magic+length stamped at startup.
        vector<string> trackedByteArraysNeedingMagicInit;

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
        // True if any local in the function body is a Beguile-declared word array
        // (sized, not byte). Such locals get framePool-backed allocation at function
        // entry and free at every return path (managed via cleanups). Used to drive
        // the framePool emission on both Z and Glulx targets.
        bool funcHasLocalArrays(functionDef* fd);
        // Recursively collect variableDeclarations from a function body, walking into the
        // sub-blocks of control-flow statements (if/for/while/do/switch/try-catch). Deduped by name
        // so the first occurrence wins — matches I6's single-declaration-per-header requirement.
        void collectBodyLocals(statementBlock* body, vector<variableDeclaration*>& out, set<string>& seen);
        // Emit copy-in for each byVal-class param of the function — `backing._opeq(paramSlot)`
        // — at routine entry. Called from both emitFunction (top-level routines) and emitClass
        // (class member methods) with their respective body-indentation strings.
        void emitParamCopyIns(functionDef* fd, const string& indent);
        // Emit framePool-backed allocation for each local array in `locals`, registering the matching
        // free in `fn->cleanups`. Shared by top-level functions AND class/object member methods so a
        // method-local `array<T>`/`rawArray<T>`/`array<char>` gets a real buffer, not a null slot.
        void emitLocalArrayAllocs(functionDef* fn, const vector<variableDeclaration*>& locals,
                                  statementBlock* body, const string& indent);

        void emit(vector<typeDef*>&);
        // Pure-I6 .inf-mode fast path: emits only the user's own header/raw body/trailer; true when it handled everything.
        bool emitPhaseInfModeRawOnly(vector<typeDef*>& nodeList);
        // The leading ICL block (user's `!%` in .inf-mode, else synthesised) plus target/framePool state.
        void emitPhaseIclAndTarget();
        // Scratch globals (ternary temps, switch temp, try/catch cookie + per-block save slots), each only when used.
        void emitPhaseScratchGlobals();
        // Scans every function/method for framePool need and Z-machine excess-param count, then emits both.
        void emitPhaseRuntimeNeedsScan(vector<typeDef*>& nodeList);
        // #emitfirst blocks, plus the placeholder resolvedOutput() replaces with the fired #storedEmitFirst blocks.
        void emitPhaseEmitFirstBlocks();
        // Registers sized-uninitialised tracked word arrays so bglInit can stamp their $9084 magic. Emits nothing.
        void collectTrackedWordArrays();
        // Registers sized-uninitialised tracked byte arrays and declares the globals bglInit must assign to.
        void emitPhaseTrackedByteArrayGlobals();
        // The synthesised bglInit routine: array magic stamps, sized-buffer setup, startup blocks, global inits.
        void emitPhaseBglInit();
        // The main source-order walk, emitting each class ahead of its first instance and capturing superposed ones.
        void emitPhaseSourceOrder(vector<typeDef*>& nodeList);
        // #emitlast blocks, plus the placeholder resolvedOutput() replaces with the fired #storedEmitLast blocks.
        void emitPhaseEmitLastBlocks();
        // .inf-mode trailer: the user's `end;` directive and everything after it, spliced in last.
        void emitPhaseInfTrailer();
        void generateI6(typeDef*);
        void emitICL(beguilerSettingsDef*);
        void emitSettingsConstants(beguilerSettingsDef*);
        void emitEnum(enumDef*);
        void emitClass(classDef*);
        // The class's `static` members, emitted as mangled `_bgl_<Class>_<member>` globals ahead of the directive.
        void emitClassStaticGlobals(classDef* classNode);
        // Standalone I6 buffers for the class's `array<char>` members; records each mangled global name.
        void emitClassByteArrayMembers(classDef* classNode, map<string, string>& externalArrayNames);
        // The `class <Name>[(N)]` directive line and its I6 inheritance list (emitter bases filtered out).
        void emitClassHeader(classDef* classNode);
        // Selects the members that reach the `with` list, dropping emitters, statics, attributes and grammar.
        void collectEmittableMembers(classDef* classNode, vector<typeMember*>& emittable);
        // The class's whole `with` clause: property members, member arrays and method routine bodies.
        void emitClassWithClause(vector<typeMember*>& emittable, map<string, string>& externalArrayNames);
        // The class's `has` clause, assembled from its attributeList members.
        void emitClassAttributes(classDef* classNode);
        void emitObject(objectDef*);
        // Standalone tracked globals backing this instance's promoted member arrays (own and inherited).
        void emitObjectPromotedArrays(objectDef* obj);
        // Standalone I6 buffers for this instance's `array<char>` members; records each mangled global name.
        void emitObjectByteArrayMembers(objectDef* obj, map<string, string>& externalArrayNames);
        // Bakes one backing instance per owned value-helper member (own and inherited), recording name + decl.
        void emitObjectOwnedMemberInstances(objectDef* obj, map<string, string>& ownedInstanceNames, map<string, variableDeclaration*>& ownedMemberDecl);
        // Collects `with` wirings for owned/promoted members inherited from the class chain, not redeclared here.
        void collectInheritedOwnedMembers(objectDef* obj, map<string, string>& ownedInstanceNames, map<string, variableDeclaration*>& ownedMemberDecl, vector<pair<string,string>>& inheritedOwned);
        // The instance's whole `with` clause: property members, methods, raw blocks and inherited owned wirings.
        void emitObjectWithClause(objectDef* obj, map<string, string>& externalArrayNames, map<string, string>& ownedInstanceNames, vector<pair<string,string>>& inheritedOwned, bool isVerbInstance);
        // The instance's `has` clause, assembled from its attributeList members.
        void emitObjectAttributes(objectDef* obj);
        // The owning class's globalDeclaration emitter body, with $self/$selfsub/$val bound to this instance.
        void emitObjectGlobalDeclarationEmitter(objectDef* obj, const string& objI6Name);
        void emitMember(typeMember*);
        void emitGlobal(variableDeclaration*);
        // One standalone `array<char>` declaration: I6 hybrid buffer, or the `<buf>` SizedBuffer raw allocation.
        void emitGlobalByteArray(arrayDeclaration* arr);
        // One standalone word array: tracked `table` layout, plain `table`, or a flat `-->` rawArray.
        void emitGlobalWordArray(arrayDeclaration* arr);
        // True when a global of this declared type emits as an I6 `Object` directive rather than a `Global`.
        bool globalEmitsAsObjectInstance(variableDeclaration* varNode);
        // The `<Class> <name>` head of an object-instance global, plus any synthesized field backings.
        void emitGlobalObjectInstanceHead(variableDeclaration* varNode, const string& varI6Name);
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
        // A local `array<T>` declaration: alias-assign, seed a list initializer, or nothing.
        void emitLocalArrayDeclaration(arrayDeclaration*, const string& indent);
        // A local variable declaration's initializer assignment (plain, emitter-bodied, or interpolated).
        void emitLocalDeclaration(variableDeclaration*, const string& indent);
        // An assignment statement: emitter-bodied, interpolated, `$target`-rewritten, or plain `lhs = rhs`.
        void emitAssignment(assignmentStatement*, const string& indent);
        // A `return`: deinit cleanups and frame-free first, then the matching I6 return form.
        void emitReturn(returnStatement*, const string& indent);
        // A call in statement position: emitter-body inlining, or a direct call with Z-target arg spilling.
        void emitFunctionCallStatement(functionCallStatement*, const string& indent);
        // An `if` / `else` statement and its blocks.
        void emitIfStatement(ifStatement*, const string& indent);
        // A `do ... while`/`until` loop, emitted as I6 `do { } until (...)`.
        void emitDoStatement(doStatement*, const string& indent);
        // A `while` loop.
        void emitWhileStatement(whileStatement*, const string& indent);
        // A C-style `for` loop.
        void emitForStatement(forStatement*, const string& indent);
        // A `for(x in c)` loop over a string, world-tree children, a member array, or a word/byte array.
        void emitForInStatement(forInStatement*, const string& indent);
        // A `switch`: an if/else chain when any case needs guards, otherwise a native I6 `switch`.
        void emitSwitchStatement(switchStatement*, const string& indent);
        // A `try`/`catch`, built on @catch/@throw cookies (separate Glulx and Z-machine shapes).
        void emitTryCatch(tryCatchStatement*, const string& indent);
        // A `throw`: unhandled-exception guard, then @throw against the active catch cookie.
        void emitThrow(throwStatement*, const string& indent);
        // A raw I6 island (`#i6{}` verbatim, or a cooked node whose local names get spill/rename applied).
        void emitRawNode(i6RawNode*, const string& indent);
        // Emit a raw I6 text block while pushing per-line entries into the source map so
        // I6 diagnostics inside the block remap to the correct .bgl line. Used for `#i6{}`
        // raw blocks (multi-line and single-line) and emitter-body inlinings.
        void emitRawTextWithSourceMap(const string& text, const sourceLocation& srcStart);
        void emitInterpolatedSegments(const vector<interpolatedSegment>& segments, string indent);
        void emitInterpolatedEmitterBody(const string& body, const string& paramName, const vector<interpolatedSegment>& segments, string indent);
        void emitVerbObject(verbObjectDef*);
        void emitGrammarRuleListDecl(grammarRuleListDecl*);
        void emitVerbSynonym(verbSynonymDecl*);
        // Lift compile-time-only fields (`meta`, `priority`) from all verb instances into
        // `verbObjectDef.isMeta` / `verbObjectDef.priority`. Idempotent. Called once at the
        // start of `emit()` so the lifted values are available to ALL emission paths
        // regardless of source order (grammar objects can precede their target verb decls).
        void liftAllVerbCompileTimeFields();
        // Pre-emit pass: for each pooled class (`class Foo[N]`) that owns a value-helper member
        // (a non-`object` class with stored fields, no initializer), synthesize a preallocated
        // pool of N backing instances per member + a compile-time free-list, and inject pop/reset
        // into create() and push into destroy() so every `new`/`delete` gets an independent,
        // reset-to-defaults backing. The static analogue is create+populate in emitObject.
        void synthesizePooledOwnedMembers();
        // Pre-emit pass: desugar each object's `children = { a, b, c }` placement list into a `parent`
        // set on the listed objects, so world-tree population written on the container reuses the
        // compile-time positional-parent tree. The inverse of writing `parent` on each child.
        void synthesizeChildrenPlacement();
        // Partition a verb's grammar contributions against the verb's anchor and emit them
        // as the right mix of Verb / Extend first / Extend directives. Shared by
        // emitVerbObject (own + extends) and emitGrammarRuleListDecl (grammar-object rules).
        void emitVerbGrammar(const string& verbName, int anchor, bool isMeta, const vector<grammarLine>& lines);
        // Resolve a FULLY-Beguile-owned verb's grammar (own block + all its extend/replace blocks)
        // at compile time and emit PURE `Verb` declarations — no I6 `Extend`. I6 `Extend` is reserved
        // for extern/library verbs Beguile doesn't own. Returns false (emitting nothing) when the verb
        // can't be cleanly resolved (a trigger word is extern-owned, or an extend introduces alternate
        // trigger words / ambiguous routing); the caller then falls back to the Extend-based path.
        bool emitVerbGrammarResolved(const string& verbName, int anchor, bool isMeta, const vector<grammarLine>& lines);
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
        // Emit one `.bgldbg [types]` `routine <name>` block + its typed params/locals. `routineName`
        // must match the .dbg identifier (top-level fn → its i6 name; object/class member method →
        // `<owner>.<method>`, e.g. `_bglUi.waitforkey`). Shared so method locals get typed too.
        void emitRoutineLocalTypes(std::ostream& f, functionDef* fd, const std::string& routineName);

};

extern i6Emitter emitter;