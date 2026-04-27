#include <iostream>
#include <fstream>
#include <sstream>

#include "i6Emitter.h"
#include "settings.h"
#include "bglParser.h"
#include "beguiler.h"
#include "helpers.h"
#include "bglLanguageService.h"

using namespace std;

//The emitter writes to a standard output stream.  We can reassign this if we want...
void i6Emitter::to(ostream& strm){
    out.std::ios::rdbuf(strm.rdbuf());
}

// Load built-in I6 templates from beguilib/_builtins.i6b.
// Format: [templateName $param1 $param2 ...] on its own line, then body lines.
// Lines starting with // are comments; blank template headers are skipped.
void i6Emitter::loadBuiltinTemplates(string path){
    ifstream f(path);
    if(!f.is_open()) return;

    string currentName;
    vector<string> currentParams;
    string currentBody;

    auto flush = [&](){
        if(!currentName.empty())
            builtinTemplates[currentName] = {currentParams, currentBody};
        currentName = "";
        currentParams.clear();
        currentBody = "";
    };

    string line;
    while(getline(f, line)){
        // strip trailing CR/spaces
        while(!line.empty() && (line.back()=='\r'||line.back()==' '||line.back()=='\t'))
            line.pop_back();
        // skip comment lines
        if(line.size()>=2 && line[0]=='/' && line[1]=='/') continue;
        // template header: [name $p1 $p2 ...]
        if(!line.empty() && line[0]=='['){
            flush();
            size_t end = line.find(']');
            if(end==string::npos) continue;
            istringstream ss(line.substr(1, end-1));
            string word; bool first=true;
            while(ss >> word){
                if(first){ currentName=word; first=false; }
                else if(!word.empty() && word[0]=='$')
                    currentParams.push_back(word.substr(1));
            }
            continue;
        }
        if(!currentName.empty()) currentBody += line + "\n";
    }
    flush();
}

// Emit a named built-in template with $param substitution and indentation applied.
void i6Emitter::applyTemplate(string name, map<string,string> args, string indent){
    auto it = builtinTemplates.find(name);
    if(it == builtinTemplates.end()){
        out << indent << "! [missing builtin template: " << name << "]\n";
        return;
    }
    string body = it->second.second;
    // substitute $param tokens (word-boundary aware)
    for(auto& [param, val] : args){
        string from = "$" + param;
        size_t pos = 0;
        while((pos = body.find(from, pos)) != string::npos){
            bool leftOk  = pos==0 || !(isalnum(body[pos-1]) || body[pos-1]=='_');
            bool rightOk = pos+from.size()>=body.size() || !(isalnum(body[pos+from.size()]) || body[pos+from.size()]=='_');
            if(leftOk && rightOk){ body.replace(pos, from.size(), val); pos+=val.size(); }
            else pos+=from.size();
        }
    }
    // emit each non-blank line with indent prefix
    istringstream ss(body);
    string line;
    while(getline(ss, line)){
        if(line.empty() || line.find_first_not_of(" \t")==string::npos) continue;
        out << indent << line << "\n";
    }
}

// Static word-boundary replacement (also used in emitStatement via call instead of lambda)
string i6Emitter::replaceWord(string str, const string& from, const string& to){
    size_t pos=0;
    while((pos=str.find(from,pos))!=string::npos){
        // Word-boundary check. '.' on the left disqualifies so we don't rewrite property
        // accesses (`obj.width` must stay intact when renaming a local `width`).
        bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$' || str[pos-1]=='.');
        bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
        if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
        else pos+=from.size();
    }
    return str;
}

// Resolve whether the named target is a Z-machine variant
static bool isZTarget(const string& t){ return t=="z3"||t=="z5"||t=="z8"; }

// Recursively walk a statementBlock, collecting every variableDeclaration reachable through
// nested control-flow sub-blocks. Deduped by name — I6 allows only one declaration per routine
// header, so the first occurrence of a given name wins and later same-named decls (e.g. a loop
// counter reused in a sibling branch) share the slot.
void i6Emitter::collectBodyLocals(statementBlock* body, vector<variableDeclaration*>& out, set<string>& seen){
    if(body == nullptr) return;
    for(statement* s : body->statements){
        if(auto* vd = dynamic_cast<variableDeclaration*>(s)){
            if(seen.insert(vd->name).second) out.push_back(vd);
        } else if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            collectBodyLocals(ifs->thenBlock, out, seen);
            collectBodyLocals(ifs->elseBlock, out, seen);
        } else if(auto* fors = dynamic_cast<forStatement*>(s)){
            collectBodyLocals(fors->body, out, seen);
        } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
            collectBodyLocals(fis->body, out, seen);
        } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
            collectBodyLocals(ws->body, out, seen);
        } else if(auto* ds = dynamic_cast<doStatement*>(s)){
            collectBodyLocals(ds->body, out, seen);
        } else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            for(switchCase* sc : sw->cases) collectBodyLocals(sc->body, out, seen);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            collectBodyLocals(tc->tryBody, out, seen);
            collectBodyLocals(tc->catchBody, out, seen);
        }
    }
}

// Check if a function would overflow Z-machine's 15-local limit (needs _bglFrm slot too → 14)
// Returns true if the function needs the frame pool (body locals overflow I6's 14-slot limit).
// Param overflow (>5 params) is handled separately via _bglXPn globals.
bool i6Emitter::funcNeedsSpill(functionDef* fd){
    if(!isZTarget(currentTarget)) return false;
    int effectiveParams = min((int)fd->params.size(), 5);
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    return (effectiveParams + (int)locals.size()) > 14;
}

// Build spill map for fd:
//   - excess params  (params[5+])  → _bglXPn globals
//   - overflow body locals          → _bglFrm-->N frame slots
void i6Emitter::buildSpillMap(functionDef* fd){
    clearSpillMap();
    // Build the per-function display-name map regardless of target. Even on Glulx (no spill),
    // this lets spillName preserve user-chosen casing for params and locals.
    auto rememberDisplay = [&](const string& canonical, const string& display){
        if(!display.empty() && display != canonical)
            currentDisplayNames[canonical] = display;
    };
    for(paramDef* p : fd->params) rememberDisplay(p->name, p->displayName);
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    for(variableDeclaration* vd : locals) rememberDisplay(vd->name, vd->displayName);

    // Property-shadow rename pass — runs on every target (not just Z), since the I6 issue
    // exists regardless of target. Must run AFTER displayName population so the mangled
    // form preserves the user's chosen casing.
    buildLocalRenameMap(fd);

    if(!isZTarget(currentTarget)) return;
    const int maxParams = 5;
    // Map excess params to _bglXPn globals
    for(int i = maxParams; i < (int)fd->params.size(); i++)
        currentSpillAliases[fd->params[i]->name] = format("_bglXP{0}", i - maxParams);
    // Count only the params that fit in I6 locals
    int effectiveParams = min((int)fd->params.size(), maxParams);
    int total = effectiveParams + (int)locals.size();
    if(total <= 14) return;
    int excess = total - 14;
    for(int i = (int)locals.size() - excess; i < (int)locals.size(); i++)
        currentSpillAliases[locals[i]->name] = format("_bglFrm-->{0}", currentSpillCount++);
}

void i6Emitter::clearSpillMap(){
    currentSpillAliases.clear();
    currentDisplayNames.clear();
    currentLocalRenames.clear();
    currentSpillCount = 0;
}

// ── property-shadow rename helpers ──────────────────────────────────────────────
// Extract every identifier that appears after a '.' in `tok` and add to `out`.
// `tok` may be a multi-segment chained access like "bgl.glulx.windowOpen" — all
// segments after the first are property accesses. Stops at non-identifier chars.
static void extractPropertyNamesFromToken(const string& tok, set<string>& out){
    size_t pos = 0;
    while((pos = tok.find('.', pos)) != string::npos){
        pos++;
        size_t end = pos;
        while(end < tok.size() && (isalnum((unsigned char)tok[end]) || tok[end] == '_')) end++;
        if(end > pos) out.insert(tok.substr(pos, end - pos));
        pos = end;
    }
}

// Same as token version, but for free-form raw-I6 text fragments (returnExpression,
// for-loop init/increment, raw #i6{} text, emitter bodies). Conservative: any `.NAME`
// pattern is treated as a property access.
static void extractPropertyNamesFromText(const string& text, set<string>& out){
    extractPropertyNamesFromToken(text, out);
}

static void extractPropertyNamesFromExpr(expression* expr, set<string>& out){
    if(!expr) return;
    for(const string& t : expr->tokens) extractPropertyNamesFromToken(t, out);
    if(auto* il = dynamic_cast<initializerList*>(expr))
        for(expression* e : il->elements) extractPropertyNamesFromExpr(e, out);
}

// Recursively walk a statementBlock collecting every identifier used as a property
// accessor (`obj.NAME`) in any expression or raw-text fragment. Used to detect locals
// that would shadow I6 property names.
static void collectDottedAccessNames(statementBlock* body, set<string>& out){
    if(!body) return;
    for(statement* s : body->statements){
        if(auto* asg = dynamic_cast<assignmentStatement*>(s)){
            extractPropertyNamesFromExpr(asg->assignedExpression, out);
            for(auto& seg : asg->interpSegments) extractPropertyNamesFromExpr(seg.expr, out);
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(s)){
            extractPropertyNamesFromExpr(vd->declaredExpressionValue, out);
            for(auto& seg : vd->interpSegments) extractPropertyNamesFromExpr(seg.expr, out);
        } else if(auto* fcs = dynamic_cast<functionCallStatement*>(s)){
            for(expression* e : fcs->args) extractPropertyNamesFromExpr(e, out);
            extractPropertyNamesFromText(fcs->emitterBody, out);
            for(auto& segs : fcs->interpSegmentsPerArg)
                for(auto& seg : segs) extractPropertyNamesFromExpr(seg.expr, out);
        } else if(auto* rs = dynamic_cast<returnStatement*>(s)){
            extractPropertyNamesFromText(rs->returnExpression, out);
        } else if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            extractPropertyNamesFromExpr(ifs->condition, out);
            collectDottedAccessNames(ifs->thenBlock, out);
            collectDottedAccessNames(ifs->elseBlock, out);
        } else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            extractPropertyNamesFromExpr(sw->condition, out);
            for(switchCase* sc : sw->cases){
                for(auto& ce : sc->entries){
                    extractPropertyNamesFromExpr(ce.value, out);
                    extractPropertyNamesFromExpr(ce.rangeLow, out);
                    extractPropertyNamesFromExpr(ce.rangeHigh, out);
                }
                collectDottedAccessNames(sc->body, out);
            }
        } else if(auto* ds = dynamic_cast<doStatement*>(s)){
            extractPropertyNamesFromExpr(ds->condition, out);
            collectDottedAccessNames(ds->body, out);
        } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
            extractPropertyNamesFromExpr(ws->condition, out);
            collectDottedAccessNames(ws->body, out);
        } else if(auto* fors = dynamic_cast<forStatement*>(s)){
            extractPropertyNamesFromExpr(fors->condition, out);
            extractPropertyNamesFromText(fors->initText, out);
            extractPropertyNamesFromText(fors->incrementText, out);
            collectDottedAccessNames(fors->body, out);
        } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
            for(expression* e : fis->inlineElements) extractPropertyNamesFromExpr(e, out);
            collectDottedAccessNames(fis->body, out);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            collectDottedAccessNames(tc->tryBody, out);
            collectDottedAccessNames(tc->catchBody, out);
        } else if(auto* ts = dynamic_cast<throwStatement*>(s)){
            extractPropertyNamesFromExpr(ts->value, out);
        } else if(auto* raw = dynamic_cast<i6RawNode*>(s)){
            extractPropertyNamesFromText(raw->text, out);
            for(auto& part : raw->parts) extractPropertyNamesFromText(part.text, out);
        }
    }
}

void i6Emitter::buildLocalRenameMap(functionDef* fd){
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    if(!body) return;
    set<string> propNames;
    collectDottedAccessNames(body, propNames);
    if(propNames.empty()) return;
    // Mangle params and locals whose canonical name matches a used property name.
    // Preserve original-case (display name) inside the mangled form for readability.
    auto maybeRename = [&](const string& canonical, const string& display){
        if(currentLocalRenames.count(canonical)) return;
        if(!propNames.count(canonical)) return;
        const string& shown = display.empty() ? canonical : display;
        currentLocalRenames[canonical] = "_l_" + shown;
    };
    for(paramDef* p : fd->params) maybeRename(p->name, p->displayName);
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    for(variableDeclaration* vd : locals) maybeRename(vd->name, vd->displayName);
}

// Like expr->text() but substitutes spilled variable names token-by-token, applies
// property-shadow renames, and falls back to original-case display form when known.
// The parser's binary-operator handling folds whole sub-expressions into single
// concatenated tokens (e.g. "width==0"), so per-token lookup alone would miss embedded
// names. After per-token substitution, we run a word-boundary replaceWord pass over
// the assembled result for the spill and rename maps to catch those.
string i6Emitter::exprText(expression* expr){
    if(!expr) return "";
    if(currentSpillAliases.empty() && currentDisplayNames.empty() && currentLocalRenames.empty())
        return expr->text();
    string result;
    for(const string& t : expr->tokens){
        string tok = (t=="!=") ? "~=" : t;
        auto sp = currentSpillAliases.find(tok);
        if(sp != currentSpillAliases.end()){ result += sp->second; continue; }
        auto rn = currentLocalRenames.find(tok);
        if(rn != currentLocalRenames.end()){ result += rn->second; continue; }
        auto dn = currentDisplayNames.find(tok);
        result += (dn != currentDisplayNames.end()) ? dn->second : tok;
    }
    // Catch concatenated sub-expressions (parser folds binary ops like "width==0" into
    // one token). replaceWord is word-boundary-safe and treats '.' as a left disqualifier,
    // so property accesses (`info.width`) stay intact.
    if(!currentLocalRenames.empty())
        for(auto& [from, to] : currentLocalRenames)
            result = replaceWord(result, from, to);
    if(!currentSpillAliases.empty())
        for(auto& [from, to] : currentSpillAliases)
            result = replaceWord(result, from, to);
    return result;
}

// Single name lookup. Resolution order:
//   1. spill alias  (Z-machine overflow → _bglFrm-->N or _bglXPn)
//   2. local rename (property-shadow avoidance → _l_<name>)
//   3. display name (user's original case)
//   4. canonical name (lowercase fallback)
string i6Emitter::spillName(const string& name){
    auto it = currentSpillAliases.find(name);
    if(it != currentSpillAliases.end()) return it->second;
    auto rn = currentLocalRenames.find(name);
    if(rn != currentLocalRenames.end()) return rn->second;
    auto dn = currentDisplayNames.find(name);
    return (dn != currentDisplayNames.end()) ? dn->second : name;
}

// Word-boundary substitution of all spill aliases AND local renames in a raw string
// (for initText/incrementText/returnExpression). Renames apply on the same word-boundary
// rules so a property access `obj.width` (where "width" is part of a multi-char span)
// isn't touched by replaceWord, which matches whole tokens only.
string i6Emitter::spillWord(const string& text){
    if(currentSpillAliases.empty() && currentLocalRenames.empty()) return text;
    string result = text;
    for(auto& [from, to] : currentSpillAliases)
        result = replaceWord(result, from, to);
    for(auto& [from, to] : currentLocalRenames)
        result = replaceWord(result, from, to);
    return result;
}

int i6Emitter::currentLine(){
    const string& s = out.str();
    return (int)count(s.begin(), s.end(), '\n') + 1;
}
void i6Emitter::writeSourceMap(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;
    for(auto& [i6Line, bglFile, bglLine] : sourceMap)
        f << i6Line << "\t" << bglFile << "\t" << bglLine << "\n";
}
void i6Emitter::writeSymbolTable(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;
    // Format: bglName <tab> i6Name <tab> kind
    // Walk languageService.globals; emit variables, objects, functions (non-emitter, non-extern)
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal) continue;
            f << vd->name << "\t" << vd->name << "\tglobal\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal) continue;
            f << od->name << "\t" << od->name << "\tobject\n";
            for(typeMember* m : od->members){
                if(auto* mv = dynamic_cast<variableDeclaration*>(m))
                    f << od->name << "." << mv->name << "\t" << mv->name << "\tproperty\n";
            }
        } else if(auto* fd = dynamic_cast<functionDef*>(node)){
            if(fd->isEmitter || fd->isExternal) continue;
            f << fd->name << "\t" << fd->name << "\tfunction\n";
        }
    }
}
void i6Emitter::writeTypesFile(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;

    // ── Type definitions (class declarations) ─────────────────────────────────
    // One 'type' block per Beguile class; one 'prop' line per property member.
    for(typeDef* node : languageService.globals){
        auto* cd = dynamic_cast<classDef*>(node);
        if(!cd || cd->isEmitterClass || cd->isExternal) continue;
        f << "type " << cd->name << "\n";
        for(typeMember* m : cd->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(!mv || mv->isExternal || mv->type.name.empty()) continue;
            const string& i6n = mv->i6name.empty() ? mv->name : mv->i6name;
            f << "  prop " << mv->name << " " << i6n << " " << mv->type.name << "\n";
        }
    }

    // ── Routine local variables ───────────────────────────────────────────────
    // One 'routine' block per compiled function; 'local' lines for params + locals.
    // Uses the same I6 name the function is emitted under so the adapter can
    // match against the .dbg <routine> <identifier>.
    for(typeDef* node : languageService.globals){
        auto* fd = dynamic_cast<functionDef*>(node);
        if(!fd || fd->isEmitter || fd->isExternal) continue;
        const string& funcI6n = fd->i6name.empty() ? fd->name : fd->i6name;
        f << "routine " << funcI6n << "\n";
        for(paramDef* p : fd->params){
            if(p->type.name.empty() || p->type.name == "void") continue;
            f << "  local " << p->name << " " << p->type.name << "\n";
        }
        auto* body = dynamic_cast<statementBlock*>(fd->body);
        if(body){
            for(statement* s : body->statements){
                auto* vd = dynamic_cast<variableDeclaration*>(s);
                if(!vd || vd->type.name.empty()) continue;
                f << "  local " << vd->name << " " << vd->type.name << "\n";
            }
        }
    }

    // ── Global variables ──────────────────────────────────────────────────────
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal || vd->type.name.empty()) continue;
            f << "global " << vd->name << " " << vd->type.name << "\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal || !od->objectClass) continue;
            f << "global " << od->name << " " << od->objectClass->name << "\n";
        }
    }
}

/**
 * writeDebugBundle — writes a single .bgldbg file containing the source map,
 * symbol table, and type information that the VS Code extension needs for
 * source-level debugging.  Format is section-delimited plain text:
 *
 *   [map]
 *   <source-map lines>
 *   [sym]
 *   <symbol-table lines>
 *   [types]
 *   <type-info lines>
 *
 * The I6 compiler writes its own debug file (.dbg) separately; this bundle
 * collects everything beguiler itself knows about the compiled program.
 */
void i6Emitter::writeDebugBundle(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;

    // ── [map] section ─────────────────────────────────────────────────────────
    f << "[map]\n";
    for(auto& [i6Line, bglFile, bglLine] : sourceMap)
        f << i6Line << "\t" << bglFile << "\t" << bglLine << "\n";

    // ── [sym] section ─────────────────────────────────────────────────────────
    f << "[sym]\n";
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal) continue;
            f << vd->name << "\t" << vd->name << "\tglobal\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal) continue;
            f << od->name << "\t" << od->name << "\tobject\n";
            for(typeMember* m : od->members){
                if(auto* mv = dynamic_cast<variableDeclaration*>(m)){
                    if(mv->name == "parent") continue;
                    f << od->name << "." << mv->name << "\t" << mv->name << "\tproperty\n";
                }
            }
        } else if(auto* fd = dynamic_cast<functionDef*>(node)){
            if(fd->isEmitter || fd->isExternal) continue;
            f << fd->name << "\t" << fd->name << "\tfunction\n";
        }
    }
    // Extern objects — emit sym entries so the debugger can expand them
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(!vd->isExternal) continue;
            // Extern variables of object type — emit as object so debugger can expand
            if(dynamic_cast<objectDef*>(&languageService.getType(vd->type.name)) != nullptr
               || vd->type.name == "object")
                f << vd->name << "\t" << vd->name << "\tobject\n";
            else
                f << vd->name << "\t" << vd->name << "\tglobal\n";
        }
    }
    for(verbObjectDef* v : languageService.verbs){
        if(!v->isExternal) continue;
        f << v->name << "\t" << v->name << "\tobject\n";
    }

    // ── [types] section ───────────────────────────────────────────────────────
    f << "[types]\n";
    // Enum declarations
    for(typeDef* node : languageService.globals){
        auto* ed = dynamic_cast<enumDef*>(node);
        if(!ed) continue;
        f << "enum " << ed->name << "\n";
        for(enumValueDef* v : ed->namedValues)
            f << "  value " << v->name << " " << v->value << "\n";
    }
    // Helper lambda: resolve the debug type string for a member variable.
    // arrayDeclaration nodes emit "array<elementType>" so the debugger can
    // distinguish e.g. array<dictionaryWord> from plain int arrays.
    auto propTypeName = [](variableDeclaration* mv) -> string {
        if(auto* arr = dynamic_cast<arrayDeclaration*>(mv); arr && !arr->elementType.empty())
            return "array<" + arr->elementType + ">";
        return mv->type.name;
    };

    // Class declarations
    for(typeDef* node : languageService.globals){
        auto* cd = dynamic_cast<classDef*>(node);
        if(!cd || cd->isEmitterClass || cd->isExternal) continue;
        f << "type " << cd->name << "\n";
        for(typeMember* m : cd->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(!mv || mv->isExternal || mv->type.name.empty()) continue;
            if(mv->name == "parent") continue; // positional arg, not a real property
            const string& i6n = mv->i6name.empty() ? mv->name : mv->i6name;
            f << "  prop " << mv->name << " " << i6n << " " << propTypeName(mv) << "\n";
        }
    }
    // Object instances — emit a type entry for each object that has its own properties.
    // This covers bare objects (no class) and class-typed objects with properties unique
    // to the instance (e.g. 'name' on a specific object not defined on its class).
    for(typeDef* node : languageService.globals){
        auto* od = dynamic_cast<objectDef*>(node);
        if(!od || od->isExternal) continue;
        // Collect properties that are on this object instance
        bool hasProps = false;
        for(typeMember* m : od->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(mv && !mv->isExternal && !mv->type.name.empty() && mv->name != "parent"
               && mv->type.name != "grammartable" && mv->type.name != "grammarrulelist"
               && mv->type.name != "grammarrule" && mv->type.name != "attributelist"){
                hasProps = true; break;
            }
        }
        if(!hasProps) continue;
        f << "type " << od->name << "\n";
        for(typeMember* m : od->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(!mv || mv->isExternal || mv->type.name.empty()) continue;
            if(mv->name == "parent") continue;
            if(mv->type.name == "grammartable" || mv->type.name == "grammarrulelist"
               || mv->type.name == "grammarrule" || mv->type.name == "attributelist") continue;
            const string& i6n = mv->i6name.empty() ? mv->name : mv->i6name;
            f << "  prop " << mv->name << " " << i6n << " " << propTypeName(mv) << "\n";
        }
    }
    // Routine locals
    for(typeDef* node : languageService.globals){
        auto* fd = dynamic_cast<functionDef*>(node);
        if(!fd || fd->isEmitter || fd->isExternal) continue;
        const string& funcI6n = fd->i6name.empty() ? fd->name : fd->i6name;
        f << "routine " << funcI6n << "\n";
        for(paramDef* p : fd->params){
            if(p->type.name.empty() || p->type.name == "void") continue;
            f << "  local " << p->name << " " << p->type.name << "\n";
        }
        auto* body = dynamic_cast<statementBlock*>(fd->body);
        if(body){
            for(statement* s : body->statements){
                auto* vd = dynamic_cast<variableDeclaration*>(s);
                if(!vd || vd->type.name.empty()) continue;
                f << "  local " << vd->name << " " << vd->type.name << "\n";
            }
        }
    }
    // Global variables
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->type.name.empty()) continue;
            f << "global " << vd->name << " " << vd->type.name << "\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal) continue;  // object stubs have no properties to expand
            if(od->objectClass)
                f << "global " << od->name << " " << od->objectClass->name << "\n";
            else
                f << "global " << od->name << " " << od->name << "\n";
        }
    }
}

void i6Emitter::emit(vector<typeDef*>& nodeList){
    // Pass 1: emit ICL !% directives at the very top (Inform reads these before parsing)
    emitICL(&beguilerSettings);
    currentTarget = beguilerSettings.target;
    for(char& c : currentTarget) c = (char)tolower(c);
    framePoolSize = beguilerSettings.framePoolSize;

    // Pass 2: emit any settings-derived constants
    emitSettingsConstants(&beguilerSettings);

    // Emit scratch variables only when actually used
    for(int i = 0; i < languageService.ternaryTempCount; i++)
        out << format("global _bgl_temp{0};\n", i);
    if(languageService.switchTempNeeded)
        out << "global _bgl_sw;\n";
    if(languageService.tryCatchNeeded){
        out << "global _bgl_catch_cookie;\n";
        // Emit per-instance cookie and save globals for each try/catch block
        // (needed because I6 function locals must be declared in the header)
        for(int i = 0; i < languageService.tryCatchCounter; i++){
            out << format("global _bgl_cv{0};\n", i);
            out << format("global _bgl_cvs{0};\n", i);
        }
    }

    // Pass 2b: if Z-machine target, scan all functions/methods for spill and XP needs.
    if(isZTarget(currentTarget)){
        bool needsPool = false;
        int maxXP = 0;
        auto scanFd = [&](functionDef* fd){
            if(fd->isEmitter || fd->isExternal) return;
            if(funcNeedsSpill(fd)) needsPool = true;
            maxXP = max(maxXP, max(0, (int)fd->params.size() - 5));
        };
        for(typeDef* node : nodeList){
            if(auto* fd = dynamic_cast<functionDef*>(node)) scanFd(fd);
            else if(auto* cd = dynamic_cast<classDef*>(node)) {
                for(typeMember* m : cd->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m)) scanFd(fd);
            }
            else if(auto* vobj = dynamic_cast<verbObjectDef*>(node)){
                if(vobj->doFunc) scanFd(vobj->doFunc);
            } else if(auto* obj = dynamic_cast<objectDef*>(node))
                for(typeMember* m : obj->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m)) scanFd(fd);
        }
        if(needsPool){
            applyTemplate("framePool", {{"size", to_string(framePoolSize)}}, "");
            frameAllocEmitted = true;
        }
        for(int i = 0; i < maxXP; i++)
            out << format("global _bglXP{0};\n", i);
        xpGlobalsNeeded = maxXP;
    }

    // Emit #emitfirst blocks — raw I6 after ICL headers, before bglInit
    for(const string& block : languageService.emitFirstBlocks)
        out << block << "\n";

    // Synthesise bglInit — always emitted, even if empty; guarded against double-call.
    // Anchor a source map entry at the routine header so I6 diagnostics about `bglInit`
    // (most commonly "declared but not used" if no startup blocks contributed) point back
    // to the extern declaration in the core library rather than appearing unmappable.
    {
        sourceLocation bglInitSrc;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->name == "bglinit"){ bglInitSrc = fd->src; break; }
        if(!bglInitSrc.file.empty() && bglInitSrc.line > 0)
            sourceMap.push_back({currentLine() + 1, bglInitSrc.file, bglInitSrc.line});
    }
    out << "global _bglInitDone = 0;\n";
    out << "[bglInit;\n";
    out << "    if(_bglInitDone) return;\n";
    out << "    _bglInitDone = 1;\n";
    for(const string& block : languageService.startupBlocks)
        out << block << "\n";
    for(auto& [varName, body] : languageService.globalInits)
        out << "    " << body << "\n";
    out << "];\n";

    // Pass 3: emit in source order with lazy class emission. When we encounter an instance
    // (variableDeclaration or objectDef whose declared class hasn't been emitted yet), emit
    // the class first, then the instance. Classes at their original source position are
    // emitted normally (unless already emitted by a prior lazy trigger). This keeps source
    // order everywhere except for classes that must move up ahead of a forward-referenced
    // instance — minimising reordering so extern attribute dependencies stay satisfied.
    set<classDef*> emittedClasses;
    auto resolveInstanceClass = [&](typeDef* node) -> classDef* {
        if(auto* od = dynamic_cast<objectDef*>(node)){
            if(dynamic_cast<verbObjectDef*>(node)) return nullptr;
            return od->objectClass;
        }
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal || vd->isConst) return nullptr;
            if(dynamic_cast<arrayDeclaration*>(vd)) return nullptr;
            if(vd->type.name == "attribute" || vd->type.name == "attributelist") return nullptr;
            auto* cd = dynamic_cast<classDef*>(&languageService.getType(vd->type.name));
            if(!cd || cd->isEmitterClass || cd->isAlias || cd->isExternal) return nullptr;
            // Only user classes with stored members require class-before-instance ordering.
            for(typeMember* m : cd->members){
                auto* mv = dynamic_cast<variableDeclaration*>(m);
                if(!mv || mv->isStatic) continue;
                if(mv->type.name == "attributelist") continue;
                if(mv->type.name == "grammarrulelist" || mv->type.name == "grammarrule") continue;
                return cd;
            }
            return nullptr;
        }
        return nullptr;
    };
    // Track extern attributes as we walk — they become "available" for `has` clauses at the
    // position of their `extern attribute X;` declaration. This acts as a compile-time proxy
    // for the #includeI6 directive that actually defines the attribute in I6; programmers
    // should declare the bindings file before the corresponding #includeI6.
    set<string> seenExternAttributes;
    // Precompute the set of declared extern attributes so we can distinguish "not yet seen
    // but will be declared later" (ordering error) from "never declared" (user typo — let
    // I6 surface it since we have no way to verify).
    set<string> declaredExternAttributes;
    for(typeDef* n : nodeList)
        if(auto* vd = dynamic_cast<variableDeclaration*>(n))
            if(vd->isExternal && vd->type.name == "attribute")
                declaredExternAttributes.insert(vd->name);
    // Recursive class emission: base classes are emitted before the class itself so that I6's
    // `class Derived class Base with …` declaration sees Base already defined.
    std::function<void(classDef*, const char*)> emitClassRecursive = [&](classDef* cd, const char* triggerReason){
        if(!cd || !emittedClasses.insert(cd).second) return;
        for(classDef* base : cd->baseClasses) emitClassRecursive(base, "base class");
        // Extern-attribute check: each `has X` must refer to an extern attribute already
        // declared at this point in source, or a non-extern identifier (which we don't police).
        for(typeMember* m : cd->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(!vd || vd->type.name != "attributelist") continue;
            auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue);
            if(!list) continue;
            for(expression* elem : list->elements){
                string attrName = elem->text();
                if(!declaredExternAttributes.count(attrName)) continue;  // not a known extern
                if(seenExternAttributes.count(attrName)) continue;       // already seen — OK
                // Declared later in source — emission here would reference an attribute I6
                // hasn't declared yet. Diagnose with source ordering hint.
                const string& clsLabel = cd->displayName.empty() ? cd->name : cd->displayName;
                throw runtime_error(format(
                    "class '{0}' uses `has {1}` but its bindings-file declaration `extern attribute {1};` "
                    "comes later in source (triggered by: {2}). Move the bindings file before the class "
                    "or its first instance.", clsLabel, attrName, triggerReason));
            }
        }
        generateI6(cd);
    };
    for(typeDef* node : nodeList){
        // Update extern-attribute availability as we pass each extern declaration.
        if(auto* vd = dynamic_cast<variableDeclaration*>(node))
            if(vd->isExternal && vd->type.name == "attribute")
                seenExternAttributes.insert(vd->name);
        if(auto* cd = dynamic_cast<classDef*>(node)){
            emitClassRecursive(cd, "source-order class declaration");
            continue;
        }
        if(classDef* needed = resolveInstanceClass(node))
            emitClassRecursive(needed, "instance declaration");
        generateI6(node);
    }

    // Emit #emitlast blocks at the very end of the I6 output
    for(const string& block : languageService.emitLastBlocks)
        out << block << "\n";
}
void i6Emitter::emitICL(beguilerSettingsDef* cfg){
    if(cfg->target == "glulx")     out << "!% -G\n";
    else if(cfg->target == "z3")   out << "!% -v3\n";
    else if(cfg->target == "z5")   out << "!% -v5\n";
    else if(cfg->target == "z8")   out << "!% -v8\n";
    if(!cfg->errorFormat.empty())  out << format("!% -E{0}\n", cfg->errorFormat);

    // Emit all search paths so I6 can resolve its own internal includes.
    // Uses ++include_path (double +) to ADD to the path rather than replace it.
    for(const string& p : cfg->includePaths)
        out << "!% ++include_path=" << p << "\n";
}
void i6Emitter::emitSettingsConstants(beguilerSettingsDef* cfg){
    // beguiler/beguilerMajor/beguilerMinor/beguilerPatch are resolved as compile-time
    // literals via #define symbols — no I6 constants emitted (avoids unused warnings).

    if(!cfg->serial.empty())
        out << "Serial \"" << cfg->serial << "\";\n";
    if(cfg->release > 0)
        out << "Release " << cfg->release << ";\n";

    // Treaty of Babel IFID: embed as a string so babel tools can find it in the story file
    if(!cfg->ifid.empty()){
        out << "Array UUID_ARRAY string \"UUID://" << cfg->ifid << "//\";\n";
        out << "#Ifdef UUID_ARRAY;#Endif;\n";
    }

    // Note: story/author/headline are NOT auto-emitted here.
    // Declare them explicitly in Beguile source using #beguilerSettings references:
    //   const string story    = #beguilerSettings.title;
    //   const string author   = #beguilerSettings.author;
    //   const string headline = #beguilerSettings.headline;
    //   const int gameRelease = #beguilerSettings.release;
}
void i6Emitter::generateI6(typeDef* node){
     // Skip value emitters — they expand inline at use sites, no standalone I6 output
     if(auto* fd = dynamic_cast<functionDef*>(node)){
         if(fd->isValueEmitter) return;
     }
     // Record source mapping for the upcoming emission so I6 diagnostics can be remapped
     // back to .bgl positions. Each node carries its own `src` (sourceLocation); pull it
     // and push a sourceMap entry at the current output line. emitFunction / emitStatement
     // push their own finer-grained entries inside; this is the coarse top-level anchor.
     {
         sourceLocation s;
         if(auto* fd = dynamic_cast<functionDef*>(node))           s = fd->src;
         else if(auto* cd = dynamic_cast<classDef*>(node))         s = cd->src;
         else if(auto* od = dynamic_cast<objectDef*>(node))        s = od->src;
         else if(auto* ed = dynamic_cast<enumDef*>(node))          s = ed->src;
         else if(auto* vd = dynamic_cast<variableDeclaration*>(node)) s = vd->src;
         else if(auto* rn = dynamic_cast<i6RawNode*>(node))        s = rn->src;
         if(!s.file.empty() && s.line > 0)
             sourceMap.push_back({currentLine(), s.file, s.line});
     }
     if (typeid(*node) == typeid(enumDef))  emitEnum((enumDef*)node);
     else if (typeid(*node) == typeid(classDef)) emitClass((classDef*)node);
     else if (typeid(*node) == typeid(objectDef)) emitObject((objectDef*)node);
     else if (typeid(*node) == typeid(verbObjectDef)) emitVerbObject((verbObjectDef*)node);
     else if (auto* gtd = dynamic_cast<grammarRuleListDecl*>(node)) emitGrammarRuleListDecl(gtd);
     else if (auto* vd = dynamic_cast<variableDeclaration*>(node)) emitGlobal(vd);
     else if (typeid(*node) == typeid(functionDef)) emitFunction((functionDef*)node);
     else if (typeid(*node) == typeid(i6RawNode)) {
         auto* raw = (i6RawNode*)node;
         if(!raw->parts.empty()){
             // Composite raw node: interleave text fragments with embedded Beguile statements.
             // Used when `#i6{}` at global scope contains `#bgl{}` regions whose statements
             // need to emit inline within the surrounding I6 stream. Each part carries its
             // own source location for the text fragment, so per-line source mapping points
             // at the correct .bgl line even across multiple #bgl interjections.
             for(auto& part : raw->parts){
                 if(!part.text.empty()) emitRawTextWithSourceMap(part.text, part.textSrc);
                 if(part.stmt != nullptr) emitStatement(part.stmt, "");
             }
             out << "\n";
         } else {
             emitRawTextWithSourceMap(raw->text, raw->src);
             out << "\n";
         }
     }
}

// Stream `text` to the output while pushing a sourceMap entry whenever a newline is
// emitted, mapping the next .inf line to the corresponding line of the .bgl source.
// `srcStart` carries the file and starting source line of `text`. The caller is expected
// to have already established a sourceMap entry for the .inf line we begin writing on
// (e.g. the coarse entry pushed in generateI6); pushing here only at newline boundaries
// avoids overriding that initial entry.
void i6Emitter::emitRawTextWithSourceMap(const string& text, const sourceLocation& srcStart){
    if(srcStart.file.empty() || srcStart.line <= 0){
        out << text;
        return;
    }
    int srcOffset = 0;  // newlines passed within `text`
    for(size_t i = 0; i < text.size(); i++){
        out << text[i];
        if(text[i] == '\n'){
            srcOffset++;
            // After the newline, currentLine() reflects the freshly-started .inf line.
            // Map it to the corresponding source line. Skip when there's no content after
            // (avoids a spurious entry past the end of the block).
            if(i + 1 < text.size())
                sourceMap.push_back({currentLine(), srcStart.file, srcStart.line + srcOffset});
        }
    }
}

void i6Emitter::emitEnum(enumDef* enumNode){
    // Non-extern enum values are inlined as integer literals at use sites (see
    // bglParser.cpp qualifyIdentifier / enum-qualified access / namespaced enum value).
    // No I6 constants are emitted, avoiding unused-constant warnings from the I6 compiler.
    // Extern enums produce no output here either — their values map to I6-defined names
    // (e.g. true/false) resolved at use sites.
    (void)enumNode;
}
void i6Emitter::emitClass(classDef* classNode){
    if(classNode->isExternal || classNode->isEmitterClass || classNode->isAlias) return;

    // emit static members as mangled globals before the class definition
    for(typeMember* m : classNode->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->isStatic){
                out << format("global _bgl_{0}_{1}", classNode->dName(), vd->dName());
                if(vd->declaredExpressionValue != nullptr && !vd->declaredExpressionValue->text().empty())
                    out << format(" = {0}", vd->declaredExpressionValue->text());
                out << ";\n";
            }

    out << format("class {0}\n", classNode->i6Name());

    if(classNode->baseClasses.size() > 0){
        out << "  class";
        for(classDef* base : classNode->baseClasses) out << format(" {0}", base->i6Name());
        out << "\n";
    }

    // collect emittable members (skip emitter-only functions, static variables, and attributeList members)
    vector<typeMember*> emittable;
    for(typeMember* m : classNode->members){
        if(auto* fd = dynamic_cast<functionDef*>(m))
            if(fd->isEmitter) continue;
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->isStatic) continue;
            if(vd->type.name == "attributelist") continue; // emitted separately as 'has' line
            if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue; // emitted as I6 Verb directives
        }
        emittable.push_back(m);
    }

    if(!emittable.empty()){
        out << "  with\n";
        for(size_t i = 0; i < emittable.size(); i++){
            typeMember* m = emittable[i];
            string sep = (i + 1 < emittable.size()) ? "," : "";

            if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                out << format("    {0}", vd->dName());
                if(vd->declaredExpressionValue != nullptr && !vd->declaredExpressionValue->text().empty())
                    out << format(" {0}", vd->declaredExpressionValue->text());
                out << sep << "\n";
            }
            else if(auto* fd = dynamic_cast<functionDef*>(m)){
                buildSpillMap(fd);
                out << format("    {0}[", fd->i6name.empty() ? fd->dName() : fd->i6name);
                string sp;
                for(paramDef* p : fd->params)
                    if(currentSpillAliases.find(p->name) == currentSpillAliases.end())
                        { out << sp << spillName(p->name); sp=" "; }
                statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
                if(body != nullptr){
                    vector<variableDeclaration*> locals;
                    set<string> seen;
                    collectBodyLocals(body, locals, seen);
                    for(variableDeclaration* vd : locals)
                        if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                            { out << sp << spillName(vd->name); sp=" "; }
                }
                if(currentSpillCount > 0){ out << sp << "_bglFrm"; }
                out << ";\n";
                if(currentSpillCount > 0)
                    out << format("        _bglFrm = _bglFrameAlloc({0});\n", currentSpillCount);
                if(body != nullptr)
                    for(statement* s : body->statements)
                        emitStatement(s, "        ");
                if(currentSpillCount > 0)
                    out << format("        _bglFrameFree({0});\n", currentSpillCount);
                out << "    ]" << sep << "\n";
                clearSpillMap();
            }
        }
    }
    // emit attributeList members as I6 'has' line (same as emitObject)
    for(typeMember* m : classNode->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->type.name == "attributelist")
                if(auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue)){
                    out << "  has";
                    for(expression* elem : list->elements) out << " " << elem->text();
                    out << "\n";
                }
    out << ";\n";
}
void i6Emitter::emitMember(typeMember* member){
    out<< member->name<<" ";
    if (typeid(*member) == typeid(functionDef))  {
        out<<"[";
        out<<"\ntext\n";
        //emitFunctionBody((enumDef*)member);
        out<<"]";
    }

}
void i6Emitter::emitFunction(functionDef* funcNode){
    if(funcNode->isEmitter || funcNode->isExternal || funcNode->isReplacedDead) return;
    buildSpillMap(funcNode);
    if(!funcNode->src.file.empty())
        sourceMap.push_back({currentLine(), funcNode->src.file, funcNode->src.line});
    out << format("[{0}", funcNode->i6name.empty() ? funcNode->dName() : funcNode->i6name);
    for(paramDef* param : funcNode->params)
        if(currentSpillAliases.find(param->name) == currentSpillAliases.end())
            out << format(" {0}", spillName(param->name));

    statementBlock* body = dynamic_cast<statementBlock*>(funcNode->body);
    if(body != nullptr){
        vector<variableDeclaration*> locals;
        set<string> seen;
        collectBodyLocals(body, locals, seen);
        for(variableDeclaration* vd : locals)
            if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                out << format(" {0}", spillName(vd->name));
    }
    if(currentSpillCount > 0) out << " _bglFrm";
    out << ";\n";
    if(currentSpillCount > 0)
        out << format("    _bglFrm = _bglFrameAlloc({0});\n", currentSpillCount);

    currentCleanups = funcNode->cleanups.empty() ? nullptr : &funcNode->cleanups;
    if(body != nullptr)
        for(statement* stmt : body->statements)
            emitStatement(stmt, "    ");
    // emit deinit cleanups at implicit end of function (fall-through path)
    if(currentCleanups != nullptr)
        for(auto& [varName, body] : *currentCleanups)
            out << "    " << body << "\n";
    if(currentSpillCount > 0)
        out << format("    _bglFrameFree({0});\n", currentSpillCount);
    currentCleanups = nullptr;
    clearSpillMap();
    out << "];\n";
}
void i6Emitter::emitStatement(statement* stmt, string indent){
    if(!stmt->src.file.empty())
        sourceMap.push_back({currentLine(), stmt->src.file, stmt->src.line});
    if(typeid(*stmt) == typeid(variableDeclaration)){
        variableDeclaration* var = (variableDeclaration*)stmt;
        // emit initializer assignment if present
        if(var->declaredExpressionValue != nullptr && (!var->declaredExpressionValue->text().empty() || !var->interpSegments.empty())){
            if(!var->interpSegments.empty() && !var->initEmitterBody.empty()){
                // Interpolated string literal: split emitter body at parameter, splice print-block
                string b = var->initEmitterBody;
                b = replaceWord(b, "$self", spillName(var->name));
                emitInterpolatedEmitterBody(b, var->initEmitterParam, var->interpSegments, indent);
            } else if(!var->initEmitterBody.empty()){
                string b = var->initEmitterBody;
                b = replaceWord(b, "$" + var->initEmitterParam, exprText(var->declaredExpressionValue));
                b = replaceWord(b, "$self",              spillName(var->name));
                b = replaceWord(b, "$target",            spillName(var->name));
                size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1);
                out << format("{0}{1};\n", indent, b);
            } else {
                string rhs = exprText(var->declaredExpressionValue);
                if(rhs.find("$target") != string::npos){
                    rhs = replaceWord(rhs, "$target", spillName(var->name));
                    out << format("{0}{1};\n", indent, rhs);
                } else {
                    out << format("{0}{1} = {2};\n", indent, spillName(var->name), rhs);
                }
            }
        }
    }
    else if(typeid(*stmt) == typeid(assignmentStatement)){
        assignmentStatement* assign = (assignmentStatement*)stmt;
        if(!assign->interpSegments.empty() && !assign->emitterBody.empty()){
            // Interpolated string literal: split emitter body at parameter, splice print-block
            string b = assign->emitterBody;
            b=replaceWord(b,"$self", assign->emitterSelf.empty() ? spillName(assign->variableLeft) : spillName(assign->emitterSelf));
            emitInterpolatedEmitterBody(b, assign->emitterParam, assign->interpSegments, indent);
        } else if(!assign->emitterBody.empty()){
            string b = assign->emitterBody;
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
            // Substitute params before $self to avoid double-substitution when param name matches the LHS variable
            b=replaceWord(b,"$" + assign->emitterParam, assign->assignedExpression != nullptr ? exprText(assign->assignedExpression) : "");
            b=replaceWord(b,"$self", assign->emitterSelf.empty() ? spillName(assign->variableLeft) : spillName(assign->emitterSelf));
            b=replaceWord(b,"$target", spillName(assign->variableLeft));
            while(!b.empty() && b.back()==';') b.pop_back();
            out << indent << b << ";\n";
        } else {
            string rhs = assign->assignedExpression != nullptr ? exprText(assign->assignedExpression) : "";
            // $target in expression: substitute LHS and emit as statement (no "LHS =" prefix)
            if(rhs.find("$target") != string::npos){
                rhs = replaceWord(rhs, "$target", spillName(assign->variableLeft));
                out << indent << rhs << ";\n";
            } else {
                out << format("{0}{1} = {2};\n", indent, spillName(assign->variableLeft), rhs);
            }
        }
    }
    else if(typeid(*stmt) == typeid(returnStatement)){
        returnStatement* ret = (returnStatement*)stmt;
        // emit deinit cleanups before every return
        if(currentCleanups != nullptr)
            for(auto& [varName, body] : *currentCleanups)
                out << indent << body << "\n";
        if(currentSpillCount > 0)
            out << format("{0}_bglFrameFree({1});\n", indent, currentSpillCount);
        if(ret->returnExpression == "rtrue" || ret->returnExpression == "rfalse")
            out << format("{0}{1};\n", indent, ret->returnExpression);
        else if(ret->returnExpression != "")
            out << format("{0}return {1};\n", indent, spillWord(ret->returnExpression));
        else
            out << indent << "return;\n";
    }
    else if(typeid(*stmt) == typeid(functionCallStatement)){
        functionCallStatement* call = (functionCallStatement*)stmt;
        if(!call->emitterBody.empty()){
            string b = call->emitterBody;
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
            // Check if any argument is an interpolated string literal
            int interpArgIdx = -1;
            for(size_t i=0; i<call->interpSegmentsPerArg.size(); i++)
                if(!call->interpSegmentsPerArg[i].empty()){ interpArgIdx = (int)i; break; }
            // Substitute all non-interpolated parameters normally
            for(size_t i=0; i<call->emitterParams.size() && i<call->args.size(); i++)
                if((int)i != interpArgIdx)
                    b=replaceWord(b, "$" + call->emitterParams[i], exprText(call->args[i]));
            // $target substitution: for discarded call statements, allocate a temp
            {
                string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
                b = replaceWord(b, "$target", tempName);
            }
            if(interpArgIdx >= 0){
                // Splice interpolated print-block at the interpolated parameter's position
                emitInterpolatedEmitterBody(b, call->emitterParams[interpArgIdx],
                    call->interpSegmentsPerArg[interpArgIdx], indent);
            } else {
                while(!b.empty() && b.back()==';') b.pop_back();
                out << indent << b << ";\n";
            }
        } else {
            // On Z-machine, args beyond the 5th are passed via _bglXPn globals
            size_t maxDirectArgs = (isZTarget(currentTarget) && call->args.size() > 5) ? 5 : call->args.size();
            for(size_t i = maxDirectArgs; i < call->args.size(); i++)
                out << format("{0}_bglXP{1} = {2};\n", indent, i - 5, exprText(call->args[i]));
            out << indent << spillWord(call->functionName) << token::parenOpen;
            for(size_t i = 0; i < maxDirectArgs; i++){
                if(i>0) out << ", ";
                out << exprText(call->args[i]);
            }
            out << token::parenClose << ";\n";
        }
    }
    else if(typeid(*stmt) == typeid(ifStatement)){
        ifStatement* ifNode = (ifStatement*)stmt;
        out << indent << "if (" << (ifNode->condition != nullptr ? exprText(ifNode->condition) : "") << ") {\n";
        if(ifNode->thenBlock != nullptr)
            for(statement* s : ifNode->thenBlock->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
        if(ifNode->elseBlock != nullptr){
            out << indent << "else {\n";
            for(statement* s : ifNode->elseBlock->statements)
                emitStatement(s, indent + "    ");
            out << indent << "}\n";
        }
    }
    else if(typeid(*stmt) == typeid(doStatement)){
        doStatement* doNode = (doStatement*)stmt;
        string cond = doNode->condition != nullptr ? exprText(doNode->condition) : "";
        out << indent << "do {\n";
        if(doNode->body != nullptr)
            for(statement* s : doNode->body->statements)
                emitStatement(s, indent + "    ");
        // do-while negates the condition: loop while expr → until ~~(expr)
        if(doNode->isWhile)
            out << indent << "} until (~~(" << cond << "));\n";
        else
            out << indent << "} until (" << cond << ");\n";
    }
    else if(typeid(*stmt) == typeid(whileStatement)){
        whileStatement* whileNode = (whileStatement*)stmt;
        out << indent << "while (" << (whileNode->condition != nullptr ? exprText(whileNode->condition) : "") << ") {\n";
        if(whileNode->body != nullptr)
            for(statement* s : whileNode->body->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
    }
    else if(typeid(*stmt) == typeid(forStatement)){
        forStatement* forNode = (forStatement*)stmt;
        out << indent << "for (" << spillWord(forNode->initText) << " : ";
        out << (forNode->condition != nullptr ? exprText(forNode->condition) : "") << " : ";
        out << spillWord(forNode->incrementText) << ") {\n";
        if(forNode->body != nullptr)
            for(statement* s : forNode->body->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
    }
    else if(typeid(*stmt) == typeid(forInStatement)){
        forInStatement* fi = (forInStatement*)stmt;
        // Inline initializer list: emit push/make templates before the loop
        if(!fi->inlineElements.empty()){
            for(auto* elem : fi->inlineElements)
                applyTemplate("forInList.push", {{"element", exprText(elem)}}, indent);
            applyTemplate("forInList.make",
                {{"target", spillName(fi->arrayVar)}, {"count", to_string(fi->inlineElements.size())}}, indent);
        }
        string openTemplate  = fi->isByteArray ? "forIn.openByte"  : "forIn.open";
        string closeTemplate = fi->isByteArray ? "forIn.closeByte" : "forIn.close";
        applyTemplate(openTemplate,
            {{"counter", spillName(fi->counterVar)}, {"array", spillName(fi->arrayVar)}, {"element", spillName(fi->elementVar)}},
            indent);
        if(fi->body != nullptr)
            for(statement* s : fi->body->statements)
                emitStatement(s, indent + "    ");
        applyTemplate(closeTemplate, {}, indent);
    }
    else if(typeid(*stmt) == typeid(switchStatement)){
        switchStatement* sw = (switchStatement*)stmt;
        if(sw->needsIfChain){
            // Emit as if/else if chain (required when any case uses comparison guards)
            string condText = sw->condition != nullptr ? exprText(sw->condition) : "0";
            out << indent << "_bgl_sw = " << condText << ";\n";
            bool first = true;
            for(switchCase* sc : sw->cases){
                if(sc->entries.empty()){
                    // default case → else
                    out << indent << "else {\n";
                } else {
                    out << indent << (first ? "if (" : "else if (");
                    bool firstCond = true;
                    for(auto& e : sc->entries){
                        if(!firstCond) out << " || ";
                        if(!e.guardCondition.empty()){
                            out << "(" << e.guardCondition << ")";
                        } else if(e.rangeLow != nullptr){
                            out << "(_bgl_sw >= " << e.rangeLow->text() << " && _bgl_sw <= " << e.rangeHigh->text() << ")";
                        } else if(e.value != nullptr){
                            // Check for operator switch() emitter matching this value's type
                            string valType = e.value->resolvedType;
                            string valText = (valType == "verb") ? ("##" + e.value->text()) : e.value->text();
                            auto it = sw->switchEmitters.find(valType);
                            if(it == sw->switchEmitters.end() && !valType.empty())
                                it = sw->switchEmitters.find("var"); // fallback to var
                            if(it != sw->switchEmitters.end()){
                                // Inline the operator switch() emitter
                                string b = it->second;
                                b = replaceWord(b, "$self", "_bgl_sw");
                                // Find the parameter name — stored as part of the emitter body placeholder
                                // We need to substitute the first non-$self word. Use a generic approach:
                                // The emitter body has one parameter; replace all non-$self param names.
                                // Since we don't store param names here, use a simpler approach:
                                // replace any word that isn't a known keyword or $self with the value.
                                // Actually, we can just do a second replaceWord pass for common param names.
                                // Better: store param name in switchEmitters. For now, use convention.
                                // Let's store as "paramName\0body" and split here.
                                // Actually simplest: just replace the first identifier-like word that isn't $self.
                                // The emitter body after $self replacement looks like: _bgl_sw.equals(v)
                                // We need to replace 'v' with the value. Let's find it by checking
                                // what remains unresolved.
                                // Simplest correct approach: store param name with the body.
                                // switchEmitters stores "paramName:body"
                                size_t colonPos = b.find('\t');
                                if(colonPos != string::npos){
                                    string paramName = b.substr(0, colonPos);
                                    string body = b.substr(colonPos + 1);
                                    body = replaceWord(body, "$" + paramName, valText);
                                    body = replaceWord(body, "$self", "_bgl_sw");
                                    size_t s = body.find_first_not_of(" \t\n\r"); if(s!=string::npos) body=body.substr(s);
                                    size_t e2 = body.find_last_not_of(" \t\n\r;"); if(e2!=string::npos) body=body.substr(0,e2+1);
                                    out << "(" << body << ")";
                                } else {
                                    out << "_bgl_sw == " << valText;
                                }
                            } else {
                                out << "_bgl_sw == " << valText;
                            }
                        }
                        firstCond = false;
                    }
                    out << ") {\n";
                    first = false;
                }
                if(sc->body != nullptr)
                    for(statement* s : sc->body->statements)
                        emitStatement(s, indent + "    ");
                out << indent << "}\n";
            }
        } else {
            // Standard I6 switch — all entries are values or ranges (no comparison guards)
            out << indent << "switch (" << (sw->condition != nullptr ? exprText(sw->condition) : "") << ") {\n";
            for(switchCase* sc : sw->cases){
                if(!sc->entries.empty()){
                    out << indent << "    ";
                    for(size_t i = 0; i < sc->entries.size(); i++){
                        if(i > 0) out << ", ";
                        auto& e = sc->entries[i];
                        if(e.rangeLow != nullptr){
                            out << e.rangeLow->text() << " to " << e.rangeHigh->text();
                        } else if(e.value != nullptr){
                            if(e.value->resolvedType == "verb")
                                out << "##" << e.value->text();
                            else
                                out << e.value->text();
                        }
                    }
                    out << ":\n";
                } else {
                    out << indent << "    default:\n";
                }
                if(sc->body != nullptr)
                    for(statement* s : sc->body->statements)
                        emitStatement(s, indent + "        ");
            }
            out << indent << "}\n";
        }
    }
    else if(typeid(*stmt) == typeid(tryCatchStatement)){
        tryCatchStatement* tc = (tryCatchStatement*)stmt;
        string id = to_string(tc->id);
        string cvName = "_bgl_cv" + id;
        string cvSave = "_bgl_cvs" + id;
        string tryLabel = "_bgl_try" + id;
        string endLabel = "_bgl_tryend" + id;
        if(currentTarget == "glulx"){
            // Glulx: @catch cookie ?label — branches to label on first exec, falls through on throw
            out << indent << "@catch " << cvName << " ?" << tryLabel << ";\n";
            // Catch body (reached via @throw)
            out << indent << "    " << tc->catchVarName << " = " << cvName << ";\n";
            if(tc->catchBody != nullptr)
                for(statement* s : tc->catchBody->statements){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == tc->catchVarName) continue;
                    emitStatement(s, indent + "    ");
                }
            out << indent << "    jump " << endLabel << ";\n";
            // Try body (normal execution — branched here by @catch)
            out << indent << "." << tryLabel << ";\n";
            out << indent << "    " << cvSave << " = _bgl_catch_cookie;\n";
            out << indent << "    _bgl_catch_cookie = " << cvName << ";\n";
            if(tc->tryBody != nullptr)
                for(statement* s : tc->tryBody->statements)
                    emitStatement(s, indent + "    ");
            out << indent << "    _bgl_catch_cookie = " << cvSave << ";\n";
            out << indent << "." << endLabel << ";\n";
            out << indent << "    _bgl_catch_cookie = _bgl_catch_cookie;\n";  // no-op to satisfy I6 label requirement
        } else {
            // Z-machine: @catch -> cookie — no branch; saves frame cookie, resumes after @catch on throw
            out << indent << cvSave << " = _bgl_catch_cookie;\n";
            out << indent << "@catch -> " << cvName << ";\n";
            out << indent << "if (_bgl_catch_cookie == " << cvName << ") {\n";
            // First execution: cookie just stored, set it as the active catch cookie
            out << indent << "    _bgl_catch_cookie = " << cvName << ";\n";
            if(tc->tryBody != nullptr)
                for(statement* s : tc->tryBody->statements)
                    emitStatement(s, indent + "    ");
            out << indent << "    _bgl_catch_cookie = " << cvSave << ";\n";
            out << indent << "} else {\n";
            // Throw landed: cvName contains thrown value
            out << indent << "    _bgl_catch_cookie = " << cvSave << ";\n";
            out << indent << "    " << tc->catchVarName << " = " << cvName << ";\n";
            if(tc->catchBody != nullptr)
                for(statement* s : tc->catchBody->statements){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == tc->catchVarName) continue;
                    emitStatement(s, indent + "    ");
                }
            out << indent << "}\n";
        }
    }
    else if(typeid(*stmt) == typeid(throwStatement)){
        throwStatement* th = (throwStatement*)stmt;
        string val = th->value ? exprText(th->value) : "0";
        out << indent << "if (_bgl_catch_cookie == 0) {\n";
        out << indent << "    print \"^[Unhandled exception]^\";\n";
        out << indent << "    quit;\n";
        out << indent << "}\n";
        out << indent << "@throw " << val << " _bgl_catch_cookie;\n";
    }
    else if(typeid(*stmt) == typeid(i6RawNode)){
        auto* raw = (i6RawNode*)stmt;
        out << indent;
        emitRawTextWithSourceMap(raw->text, raw->src);
        out << "\n";
    }
}
void i6Emitter::emitInterpolatedSegments(const vector<interpolatedSegment>& segments, string indent){
    for(auto& seg : segments){
        if(!seg.isExpr){
            if(!seg.text.empty())
                out << indent << "print \"" << seg.text << "\";\n";
        } else {
            for(statement* inj : seg.injections)
                emitStatement(inj, indent);

            string rt = seg.expr->resolvedType;
            string exprStr = exprText(seg.expr);

            classDef* cls = dynamic_cast<classDef*>(&languageService.getType(rt));
            if(cls != nullptr){
                functionDef* printFn = nullptr;
                std::function<void(classDef*)> findPrint = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->name == "print" && fd->params.empty()){
                                printFn = fd;
                                return;
                            }
                    if(printFn == nullptr)
                        for(classDef* base : c->baseClasses){ findPrint(base); if(printFn) return; }
                };
                findPrint(cls);

                if(printFn != nullptr && printFn->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(printFn->body)){
                        string b = parser.processBglConditionals(blk->i6Body);
                        b = replaceWord(b, "$self", exprStr);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        out << indent << b << ";\n";
                        continue;
                    }
                }
                if(printFn != nullptr && !printFn->isEmitter){
                    out << indent << exprStr << ".print();\n";
                    continue;
                }
            }

            if(rt == "void"){
                out << indent << exprStr << "\n";
                continue;
            }

            string cast;
            if(rt == "string") cast = "(string)";
            out << indent << "print " << cast << exprStr << ";\n";
        }
    }
}
// Emits an emitter body that contains an interpolatedStringLiteral parameter.
// Splits the body at the parameter reference (word-boundary match) and splices in the print-block.
// Emitter body lines before and after the parameter reference are emitted as I6 statements.
void i6Emitter::emitInterpolatedEmitterBody(const string& body, const string& paramName, const vector<interpolatedSegment>& segments, string indent){
    string dollarParam = "$" + paramName;
    size_t pos = 0;
    size_t paramPos = string::npos;
    while(pos < body.size()){
        size_t found = body.find(dollarParam, pos);
        if(found == string::npos) break;
        bool leftOk  = found == 0 || !(isalnum(body[found-1]) || body[found-1]=='_' || body[found-1]=='$');
        bool rightOk = found+dollarParam.size() >= body.size() || !(isalnum(body[found+dollarParam.size()]) || body[found+dollarParam.size()]=='_');
        if(leftOk && rightOk){ paramPos = found; break; }
        pos = found + dollarParam.size();
    }

    if(paramPos == string::npos){
        string b = body;
        size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
        size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1);
        if(!b.empty())
            out << indent << b << ";\n";
        return;
    }

    string before = body.substr(0, paramPos);
    {
        size_t s=before.find_first_not_of(" \t\n\r;");
        size_t e=before.find_last_not_of(" \t\n\r;");
        before = (s!=string::npos && e!=string::npos) ? before.substr(s, e-s+1) : "";
        if(!before.empty())
            out << indent << before << ";\n";
    }

    emitInterpolatedSegments(segments, indent);

    size_t afterStart = paramPos + dollarParam.size();
    string after = afterStart < body.size() ? body.substr(afterStart) : "";
    {
        size_t s=after.find_first_not_of(" \t\n\r;");
        size_t e=after.find_last_not_of(" \t\n\r;");
        after = (s!=string::npos && e!=string::npos) ? after.substr(s, e-s+1) : "";
        if(!after.empty())
            out << indent << after << ";\n";
    }
}
string i6Emitter::synthesizeFieldBackings(classDef* cls, const string& instanceName, set<classDef*>& visited){
    string clause;
    bool first = true;
    for(typeMember* m : cls->members){
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        if(!vd || vd->isStatic || vd->isConst) continue;
        // Skip non-data fields
        if(vd->type.name == "attributelist") continue;
        if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue;
        // Field type must be a real, statically-instantiable class
        classDef* fieldCls = dynamic_cast<classDef*>(&languageService.getType(vd->type.name));
        if(!fieldCls || fieldCls->isEmitterClass || fieldCls->isAlias || fieldCls->isExternal) continue;
        // Skip if already on the instantiation path — same-class fields and any indirect cycles
        // are deliberately left at default (references owned elsewhere).
        if(visited.count(fieldCls)) continue;
        // Skip if the field's class manages its own allocation via init emitter (e.g. string).
        bool hasInitEmitter = false;
        for(typeMember* fm : fieldCls->members)
            if(auto* fn = dynamic_cast<functionDef*>(fm))
                if(fn->isEmitter && fn->name == "init" && fn->params.empty()){ hasInitEmitter = true; break; }
        if(hasInitEmitter) continue;
        // Skip if the field's class has no stored fields (it would emit as a plain global, not
        // an object instance — no point auto-backing it).
        bool storesFields = false;
        for(typeMember* fm : fieldCls->members){
            auto* fvd = dynamic_cast<variableDeclaration*>(fm);
            if(!fvd || fvd->isStatic) continue;
            if(fvd->type.name == "attributelist") continue;
            if(fvd->type.name == "grammarrulelist" || fvd->type.name == "grammarrule") continue;
            storesFields = true; break;
        }
        if(!storesFields) continue;

        // Synthesize a backing instance global. Recurse for its own fields. The backing's
        // mangled name uses the field's display form for human readability.
        string backingName = format("_bglField_{0}_{1}", instanceName, vd->dName());
        visited.insert(fieldCls);
        string subClause = synthesizeFieldBackings(fieldCls, backingName, visited);
        visited.erase(fieldCls);
        out << format("{0} {1}", fieldCls->i6Name(), backingName);
        if(!subClause.empty()) out << " " << subClause;
        out << ";\n";

        if(first){ clause = "with "; first = false; } else { clause += ", "; }
        clause += format("{0} {1}", vd->dName(), backingName);
    }
    return clause;
}

void i6Emitter::emitGlobal(variableDeclaration* varNode){
    if(varNode->isExternal) return;  // extern declarations are type-system only

    //--Array declarations. We use I6 tables and buffers exclusively for Beguile constructs
    if(auto* arr = dynamic_cast<arrayDeclaration*>(varNode)){
        string arraySubtype;
        if(arr->isByteArray) arraySubtype="buffer"; else arraySubtype="table";

        // initialize with string...
        if(!arr->stringInitializer.empty()) {
            out << format("array {0} {1} {2};\n", arr->dName(), arraySubtype, arr->stringInitializer);
            return;
        }

        //no initialization, just allocating size
        if(arr->arraySize > 0) {
            out << format("array {0} {1} {2};\n", arr->dName(), arraySubtype, arr->arraySize);
            return;
        }

        //initialize with a list of elements
        if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
            out << format("array {0} {1}", arr->dName(), arraySubtype);
            for(expression* elem : list->elements) out << " " << elem->text();
            out << ";\n";
            return;
        }
        //NOTE: if we got here, something's wrong.  The parser hasn't filled in what we need.
        throw ("i6Emitter: unable to emit array.");
        return;
    }

    if(varNode->isConst){
        out << format("constant {0}", varNode->dName());
        if(varNode->declaredExpressionValue != nullptr)
            out << format(" = {0}", varNode->declaredExpressionValue->text());
        out << ";\n";
        return;
    }
    if(varNode->type.name == "attribute"){
        out << format("attribute {0}", varNode->dName());
        out << ";\n";
        return;
    }
    const string& varI6Name = varNode->i6name.empty() ? varNode->dName() : varNode->i6name;
    // Emit as an I6 object instance when the declared type is a user class with stored
    // (non-emitter, non-static, non-attribute) members. Primitive classes (int, bool, char,
    // string, etc.) have emitter-only bodies and emit as plain globals. This lets user
    // code write `Foo x;` without forcing `class Foo : object`.
    bool emitAsObjectInstance = false;
    {
        typeDef& td = languageService.getType(varNode->type.name);
        if(dynamic_cast<objectDef*>(&td)){
            emitAsObjectInstance = true;  // dedicated objectDef type
        } else if(auto* cd = dynamic_cast<classDef*>(&td)){
            if(!cd->isEmitterClass && !cd->isAlias && !cd->isExternal){
                for(typeMember* m : cd->members){
                    auto* vd = dynamic_cast<variableDeclaration*>(m);
                    if(!vd || vd->isStatic) continue;
                    if(vd->type.name == "attributelist") continue;
                    if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue;
                    emitAsObjectInstance = true;
                    break;
                }
            }
        }
    }
    if(emitAsObjectInstance){
        string typeName = varNode->type.name;
        classDef* instCls = dynamic_cast<classDef*>(&languageService.getType(typeName));
        if(instCls) typeName = instCls->i6Name();
        // Synthesize backing globals for class-typed fields so that operator= and
        // member-access on those fields write into a real instance, not object 0.
        // The visited set starts empty so the FIRST level may back a self-typed field
        // (giving operator= a real target). Recursion adds the field's class to visited,
        // so the backing's own self-typed slot is left empty — breaking the cycle while
        // still satisfying the "real instance to write into" requirement at level 0.
        string backingClause;
        if(instCls && varNode->declaredExpressionValue == nullptr){
            set<classDef*> visited;
            backingClause = synthesizeFieldBackings(instCls, varI6Name, visited);
        }
        out<<format("{0} {1}", typeName, varI6Name);
        if(!backingClause.empty()) out << " " << backingClause;
    }
    else
        out<<format("global {0}", varI6Name);
    if(varNode->declaredExpressionValue != nullptr)
        out<<format(" = {0}", varNode->declaredExpressionValue->text());
    out<<";\n";
}
void i6Emitter::emitObject(objectDef* obj){
    // find initial parent member, if set
    string parentValue;
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->name == "parent" && vd->declaredExpressionValue)
                { parentValue = vd->declaredExpressionValue->text(); break; }

    // Emit external global arrays for byte-array (array<char>) member arrays.
    // Byte arrays can't be stored as inline property values (those are word-sized),
    // so we emit a standalone Array and store a pointer as the property value.
    map<string, string> externalArrayNames; // member name → mangled global array name
    for(typeMember* m : obj->members){
        if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
            if(arr->isByteArray){
                string mangledName = "_" + obj->name + "_" + arr->name;
                if(!arr->stringInitializer.empty()){
                    // String initializer: Array name string "text";
                    out << format("Array {0} string {1};\n", mangledName, arr->stringInitializer);
                } else if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
                    // Brace initializer: Array name -> count v1 v2 ...;
                    out << format("Array {0} -> {1}", mangledName, list->elements.size());
                    for(expression* elem : list->elements) out << " " << elem->text();
                    out << ";\n";
                } else {
                    // Sized, zero-initialized: Array name -> count 0 0 ...;
                    out << format("Array {0} -> {1}", mangledName, arr->arraySize);
                    for(int k = 0; k < arr->arraySize; k++) out << " 0";
                    out << ";\n";
                }
                externalArrayNames[arr->name] = mangledName;
            }
        }
    }

    // Use the declared class name (if any) as the I6 object prefix; fall back to 'Object'
    string i6ClassName = (obj->objectClass && obj->objectClass->name != "object")
                         ? obj->objectClass->i6Name() : "object";
    const string& objI6Name = obj->i6name.empty() ? obj->dName() : obj->i6name;
    if(parentValue.empty())
        out << format("{0} {1}\n", i6ClassName, objI6Name);
    else
        out << format("{0} {1} {2}\n", i6ClassName, objI6Name, parentValue);

    // collect property members (includes raw i6 blocks, which emit as 'with' properties)
    // 'parent' is excluded — it's emitted as a positional argument, not a 'with' property
    bool hasProps = false;
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->isExternal) continue; // alias members have no I6 backing
            if(vd->type.name != "attributelist" && vd->type.name != "grammarrulelist" && vd->type.name != "grammarrule" && vd->name != "parent") { hasProps = true; break; }
        } else if(auto* fd = dynamic_cast<functionDef*>(m)){ if(!fd->isEmitter) { hasProps = true; break; } }
          else if(dynamic_cast<i6RawNode*>(m))  { hasProps = true; break; }

    if(hasProps){
        bool first = true;
        for(typeMember* m : obj->members){
            if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
                // Property array: emit as inline I6 property values
                out << (first ? "  with " : ",\n       ");
                out << arr->dName() << " ";
                auto extIt = externalArrayNames.find(arr->name);
                if(extIt != externalArrayNames.end()){
                    // String-initialized array: emit pointer to external global array
                    out << extIt->second;
                } else if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
                    for(expression* elem : list->elements) out << elem->text() << " ";
                } else {
                    // N zero slots (size is encoded via obj.#prop, not in element 0)
                    for(int k = 0; k < arr->arraySize; k++) out << "0 ";
                }
                first = false;
            } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->isExternal) continue; // alias members: compile-time indirection only
                if(vd->type.name == "attributelist") continue; // handled separately below
                if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue; // emitted as I6 Verb directives
                if(vd->name == "parent") continue; // emitted as positional argument, not 'with' property
                out << (first ? "  with " : ",\n       ");
                out << vd->dName() << " ";
                if(vd->declaredExpressionValue) out << vd->declaredExpressionValue->text();
                first = false;
            } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->isEmitter) continue; // emitter methods are inlined at call sites, not emitted as properties
                buildSpillMap(fd);
                out << (first ? "  with " : ",\n       ");
                out << (fd->i6name.empty() ? fd->dName() : fd->i6name) << " [";
                string sp;
                for(paramDef* p : fd->params)
                    if(currentSpillAliases.find(p->name) == currentSpillAliases.end())
                        { out << sp << p->dName(); sp=" "; }
                statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
                if(body){
                    vector<variableDeclaration*> locals;
                    set<string> seen;
                    collectBodyLocals(body, locals, seen);
                    for(variableDeclaration* vd : locals)
                        if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                            { out << sp << vd->dName(); sp=" "; }
                }
                if(currentSpillCount > 0){ out << sp << "_bglFrm"; }
                out << ";\n";
                if(currentSpillCount > 0)
                    out << format("    _bglFrm = _bglFrameAlloc({0});\n", currentSpillCount);
                if(body)
                    for(statement* s : body->statements)
                        emitStatement(s, "    ");
                if(currentSpillCount > 0)
                    out << format("    _bglFrameFree({0});\n", currentSpillCount);
                out << "  ]";
                clearSpillMap();
                first = false;
            } else if(auto* raw = dynamic_cast<i6RawNode*>(m)){
                // raw i6 property block — emitted verbatim inside 'with'
                string text = raw->text;
                size_t s = text.find_first_not_of(" \t\n\r");
                size_t e = text.find_last_not_of(" \t\n\r");
                if(s != string::npos) text = text.substr(s, e - s + 1);
                out << (first ? "  with " : ",\n       ");
                out << text;
                first = false;
            }
        }
        if(!first) out << "\n";
    }

    // emit attributeList members as I6 'has' line
    for(typeMember* m : obj->members){
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->type.name != "attributelist") continue;
            if(auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue)){
                out << "  has";
                for(expression* elem : list->elements) out << " " << elem->text();
                out << "\n";
            }
        }
    }

    out << ";\n";

    // If the object's class has a globalDeclaration emitter, emit it now with $self/$selfsub substituted
    if(obj->objectClass && !obj->objectClass->globalDeclarationBody.empty()){
        string body = obj->objectClass->globalDeclarationBody;
        size_t s = body.find_first_not_of(" \t\n\r"); if(s != string::npos) body = body.substr(s);
        size_t e = body.find_last_not_of(" \t\n\r");  if(e != string::npos) body = body.substr(0, e+1);
        body = replaceWord(body, "$selfsub", objI6Name + "sub");
        body = replaceWord(body, "$self",    objI6Name);
        out << body << "\n";
    }
}

//===============================================================================================================================
// Verb and grammar emission
//===============================================================================================================================

void i6Emitter::emitVerbObject(verbObjectDef* vd){
    if(vd->isExternal) return;
    emitObject(vd);   // also fires globalDeclaration emitter if defined on the verb class
    if(!vd->grammarLines.empty()) emitGrammarLines(vd->name, vd->grammarLines);
}

void i6Emitter::emitGrammarRuleListDecl(grammarRuleListDecl* gtd){
    emitGrammarLines(gtd->verbName, gtd->grammarLines);
}

// Group grammar lines by verb trigger word; emit one Verb/Extend block per unique trigger word.
// First occurrence of a trigger word → Verb 'word'; subsequent → Extend 'word' first.
// Per-line targetVerb overrides verbName (for multi-verb grammar objects).
void i6Emitter::emitGrammarLines(const string& verbName, const vector<grammarLine>& lines){
    // Each entry: {triggerWord, patternTokens, actionName}
    struct lineEntry { vector<string> patternTokens; string actionName; };
    vector<string> wordOrder;
    map<string, vector<lineEntry>> byWord;
    for(const grammarLine& line : lines){
        if(byWord.find(line.verbWord) == byWord.end())
            wordOrder.push_back(line.verbWord);
        string action = line.targetVerb.empty() ? verbName : line.targetVerb;
        byWord[line.verbWord].push_back({line.patternTokens, action});
    }

    auto toI6Word = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch;
        return (e.size() == 1) ? ("'" + e + "//'") : ("'" + e + "'");
    };

    for(const string& word : wordOrder){
        bool isFirst = declaredVerbWords.find(word) == declaredVerbWords.end();
        if(isFirst){
            declaredVerbWords.insert(word);
            out << format("verb {0}\n", toI6Word(word));
        } else {
            out << format("extend {0} first\n", toI6Word(word));
        }
        const auto& entries = byWord[word];
        for(size_t i = 0; i < entries.size(); i++){
            out << "    *";
            for(const string& pt : entries[i].patternTokens) out << " " << pt;
            out << format(" -> {0}", entries[i].actionName);
            if(i + 1 == entries.size()) out << ";";
            out << "\n";
        }
    }
}

i6Emitter emitter;