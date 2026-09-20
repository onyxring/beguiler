#include "platform.h"
// ═══════════════════════════════════════════════════════════════════════════════
// bglParserHelpers.cpp — free-function helpers shared across bglParser*.cpp.
// See bglParserHelpers.h for usage notes.
// ═══════════════════════════════════════════════════════════════════════════════
#include "bglParserHelpers.h"
#include "bglLanguageService.h"
#include <cctype>

using namespace std;

string replaceWord(string str, const string& from, const string& to){
    auto findCI = [](const string& hay, const string& nd, size_t pos) -> size_t {
        if(nd.empty() || hay.size() < nd.size()) return string::npos;
        size_t end = hay.size() - nd.size();
        for(size_t i = pos; i <= end; i++){
            bool m = true;
            for(size_t j = 0; j < nd.size(); j++)
                if(tolower((unsigned char)hay[i+j]) != tolower((unsigned char)nd[j])){ m = false; break; }
            if(m) return i;
        }
        return string::npos;
    };
    size_t pos = 0;
    while((pos = findCI(str, from, pos)) != string::npos){
        bool leftOk  = pos == 0 || !(isalnum(str[pos-1]) || str[pos-1] == '_' || str[pos-1] == '$');
        bool rightOk = pos + from.size() >= str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()] == '_');
        if(leftOk && rightOk){ str.replace(pos, from.size(), to); pos += to.size(); }
        else pos += from.size();
    }
    return str;
}

string typeDisplayName(const string& typeName){
    typeDef& td = languageService.getType(typeName);
    if(&td != &emptyTDef && !td.displayName.empty()) return td.displayName;
    return typeName;
}

string mangleOperatorName(const string& opName){
    // The conversion operator's name is the whole token "operator()" (not a bare symbol like
    // "="), and it contains parentheses that aren't valid in an I6 identifier. Give it a fixed
    // mangled name so a non-emitter (regular-method) conversion operator emits a callable routine.
    if(opName == "operator()") return "_opconv";
    string safe = "_op";
    for(char ch : opName){
        if     (ch == '=') safe += "eq";
        else if(ch == '~') safe += "tilde";
        else if(ch == '<') safe += "lt";
        else if(ch == '>') safe += "gt";
        else if(ch == '!') safe += "ne";
        else if(ch == '+') safe += "add";
        else if(ch == '-') safe += "sub";
        else if(ch == '*') safe += "mul";
        else if(ch == '/') safe += "div";
        else if(ch == '%') safe += "mod";
        else if(ch == '&') safe += "and";
        else if(ch == '|') safe += "or";
        else if(ch == '^') safe += "xor";
        else if(ch == '?') safe += "qry";
        else if(ch != ' ') safe += ch;
    }
    return safe;
}

bool hasReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* is = dynamic_cast<ifStatement*>(s))
            if(hasReturn(is->thenBlock) || hasReturn(is->elseBlock)) return true;
        if(auto* ws = dynamic_cast<whileStatement*>(s))  if(hasReturn(ws->body))  return true;
        if(auto* ds = dynamic_cast<doStatement*>(s))     if(hasReturn(ds->body))  return true;
        if(auto* fs = dynamic_cast<forStatement*>(s))    if(hasReturn(fs->body))  return true;
        if(auto* sw = dynamic_cast<switchStatement*>(s))
            for(switchCase* c : sw->cases) if(hasReturn(c->body)) return true;
    }
    return false;
}

// A `break;` lowers to an i6RawNode "break;". A break escapes the CURRENT loop only when it sits at
// the loop's own level or inside an `if` within it — a break inside a NESTED loop targets that inner
// loop, and switch-case breaks are stripped at parse. So descend into if/else but NOT nested loops.
static bool loopBodyHasEscapingBreak(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(auto* raw = dynamic_cast<i6RawNode*>(s)){
            const string& t = raw->text;
            size_t a = t.find_first_not_of(" \t\r\n");
            size_t b = t.find_last_not_of(" \t\r\n;");
            string core = (a == string::npos) ? "" : t.substr(a, (b == string::npos ? t.size() : b + 1) - a);
            if(core == "break") return true;
        }
        if(auto* is = dynamic_cast<ifStatement*>(s))
            if(loopBodyHasEscapingBreak(is->thenBlock) || loopBodyHasEscapingBreak(is->elseBlock)) return true;
    }
    return false;
}

// Trim leading/trailing whitespace from a condition's emitted text.
static string trimmedCondText(expression* c){
    if(c == nullptr) return "";
    string t = c->text();
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    return (a == string::npos) ? "" : t.substr(a, (b == string::npos ? t.size() : b + 1) - a);
}

// True when `c` is a compile-time-constant true loop condition — `while(true)`, `for(;;)` (null/
// empty condition), or `do…while(true)`. Such a loop never terminates on its own; only a `break`
// can leave it.
static bool isConstTrueCondition(expression* c){
    if(c == nullptr) return true;               // for(;;) — no condition
    string core = trimmedCondText(c);
    return core.empty() || core == "true" || core == "1";
}

// True when `c` is a compile-time-constant false condition — the `do…until(false)` infinite form
// (a do-until loops UNTIL its condition is true, so a false condition never lets it exit).
static bool isConstFalseCondition(expression* c){
    if(c == nullptr) return false;
    string core = trimmedCondText(c);
    return core == "false" || core == "0";
}

// A genuine user `#i6{}` island whose raw text contains a return-like statement. The return-path
// analysis can't parse raw I6, but when the author has dropped to an opaque island that returns,
// requiring a *further* trailing Beguile `return` only produces dead code that Inform 6 then flags
// as unreachable. Trust a return-bearing island as a terminating path. (A synthesized i6RawNode —
// loop increment, destroy call, spilled assignment — is NOT an island and never matches.)
static bool i6IslandMayReturn(i6RawNode* raw){
    if(raw == nullptr || !raw->isI6Island) return false;
    auto scan = [](const string& t) -> bool {
        return t.find("return") != string::npos || t.find("rtrue") != string::npos
            || t.find("rfalse") != string::npos || t.find("@ret")  != string::npos;
    };
    if(scan(raw->text)) return true;
    for(auto& p : raw->parts) if(scan(p.text)) return true;
    return false;
}

bool allPathsReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* raw = dynamic_cast<i6RawNode*>(s)) if(i6IslandMayReturn(raw)) return true;
        // An infinite loop (`while(true)` / `for(;;)`) with no `break` that escapes it never falls
        // through — control leaves only via a `return` inside, so the code after it (and a trailing
        // return) is unreachable. Treat such a loop as a terminating path so routines that loop
        // forever and return from within don't demand a dead trailing return.
        if(auto* ws = dynamic_cast<whileStatement*>(s))
            if(isConstTrueCondition(ws->condition) && !loopBodyHasEscapingBreak(ws->body)) return true;
        if(auto* fs = dynamic_cast<forStatement*>(s))
            if(isConstTrueCondition(fs->condition) && !loopBodyHasEscapingBreak(fs->body)) return true;
        // do-loops run the body at least once; they're non-terminating when the guard can never let
        // them exit: `do…while(true)` (isWhile: loop while condition true) or `do…until(false)`
        // (loop until condition true) — again, only an escaping `break` can leave them.
        if(auto* ds = dynamic_cast<doStatement*>(s)){
            bool infinite = ds->isWhile ? isConstTrueCondition(ds->condition)
                                        : isConstFalseCondition(ds->condition);
            if(infinite && !loopBodyHasEscapingBreak(ds->body)) return true;
        }
        if(auto* is = dynamic_cast<ifStatement*>(s)){
            if(is->elseBlock != nullptr &&
               allPathsReturn(is->thenBlock) && allPathsReturn(is->elseBlock))
                return true;
        }
        if(auto* sw = dynamic_cast<switchStatement*>(s)){
            bool hasDefault = false;
            bool allReturn = true;
            for(switchCase* c : sw->cases){
                if(c->entries.empty()) hasDefault = true;
                if(!allPathsReturn(c->body)) allReturn = false;
            }
            if(hasDefault && allReturn) return true;
        }
    }
    return false;
}

bool classHasStoredFields(classDef* cls){
    if(!cls) return false;
    for(typeMember* m : cls->members){
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        if(!vd || vd->isStatic) continue;
        if(vd->type.name == "attributelist") continue;
        if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue;
        return true;
    }
    return false;
}

bool inheritsFromObject(classDef* cls){
    if(!cls) return false;
    if(cls->name == "object") return true;
    // Alias classes dissolve to their parent for I6 emission — fields live on the
    // resolved parent (typically object). Treat them as tree citizens for the
    // value-semantics check; they have no separate backing storage to copy.
    if(cls->isAlias) return true;
    for(classDef* base : cls->baseClasses)
        if(inheritsFromObject(base)) return true;
    return false;
}

// True when `cls` is a REFERENCE type — one that carries object identity/backing rather than
// value-copy semantics for LOCALS. This roots at the compiler's own base type `_bglObject` (the
// empty root of all Beguile objects), NOT the library's world-tree `object`: a reference type need
// not be a world-tree citizen (e.g. a Glulx `window` declared `: _bglObject`). Bare value structs
// (`bglSize`, a value-helper proxy) do NOT derive `_bglObject`, so they keep value semantics.
// `object : _bglObject`, so world-tree objects still qualify via the base chain.
//
// This is DISTINCT from inheritsFromObject(): that one gates inline-OBJECT `{…}` inference and must
// stay keyed on the world-tree `object`, because collection/value types (array<T>, char, …) also
// derive `_bglObject` yet must NOT be treated as inline-object-backed. Only the reference-vs-value
// LOCAL decision uses this predicate.
bool isReferenceBacked(classDef* cls){
    if(!cls) return false;
    // Match the single root `_bglObject` (the compiler's base of all Beguile objects). Since
    // `object : _bglObject` IS linked in the classDef graph, world-tree objects qualify via the base
    // chain too — no need to also name "object". Bare value structs don't derive `_bglObject`, so
    // they stay value types. Type names are stored lowercased in the graph, so compare lowercase.
    if(cls->name == "_bglobject") return true;
    if(cls->isAlias) return true;
    for(classDef* base : cls->baseClasses)
        if(isReferenceBacked(base)) return true;
    return false;
}

// True iff `ancestor` is a STRICT (transitive) base of `descendant`. Walks descendant's base
// chain; returns false if either is null or they are the same class (so an identity cast
// `(T)obj` where obj is already a T does NOT trigger ancestor dispatch). Multiple inheritance
// is handled — any base path that reaches `ancestor` counts.
bool isAncestorClass(classDef* ancestor, classDef* descendant){
    if(!ancestor || !descendant || ancestor == descendant) return false;
    for(classDef* base : descendant->baseClasses){
        if(base == ancestor) return true;
        if(isAncestorClass(ancestor, base)) return true;
    }
    return false;
}

const hiddenMember* findHiddenMember(classDef* cls, const string& memberName,
                                     const string& queriedOp,
                                     const vector<string>& operandTypeNames){
    if(!cls) return nullptr;
    for(const hiddenMember& hm : cls->hiddenMembers){
        if(hm.memberName != memberName) continue;
        // A whole-member hide (operatorName == "") subsumes any access — read, write, method,
        // any operator. An operator-scoped hide matches only its own operator.
        if(!hm.operatorName.empty() && hm.operatorName != queriedOp) continue;
        // Overload narrowing: an entry that named an operand list matches only that overload.
        if(!hm.operandTypes.empty()){
            if(hm.operandTypes.size() != operandTypeNames.size()) continue;
            bool same = true;
            for(size_t i = 0; i < hm.operandTypes.size(); i++)
                if(hm.operandTypes[i] != operandTypeNames[i]){ same = false; break; }
            if(!same) continue;
        }
        return &hm;
    }
    // Hides declared on a base apply to derived types too (the restriction is inherited).
    for(classDef* base : cls->baseClasses)
        if(const hiddenMember* hit = findHiddenMember(base, memberName, queriedOp, operandTypeNames))
            return hit;
    return nullptr;
}

bool isConstVariable(const string& name, functionDef* func, statementBlock* body){
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->isConst;
    if(func != nullptr && func->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(func->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->isConst;
    }
    if(auto* vd = languageService.findGlobalAs<variableDeclaration>(name)) return vd->isConst;
    return false;
}
