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
        bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$');
        bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
        if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
        else pos+=from.size();
    }
    return str;
}

// Resolve whether the named target is a Z-machine variant
static bool isZTarget(const string& t){ return t=="z3"||t=="z5"||t=="z8"; }

// Check if a function would overflow Z-machine's 15-local limit (needs _bglFrm slot too → 14)
// Returns true if the function needs the frame pool (body locals overflow I6's 14-slot limit).
// Param overflow (>5 params) is handled separately via _bglXPn globals.
bool i6Emitter::funcNeedsSpill(functionDef* fd){
    if(!isZTarget(currentTarget)) return false;
    int effectiveParams = min((int)fd->params.size(), 5);
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    int locals = 0;
    if(body) for(statement* s : body->statements)
        if(dynamic_cast<variableDeclaration*>(s)) locals++;
    return (effectiveParams + locals) > 14;
}

// Build spill map for fd:
//   - excess params  (params[5+])  → _bglXPn globals
//   - overflow body locals          → _bglFrm-->N frame slots
void i6Emitter::buildSpillMap(functionDef* fd){
    clearSpillMap();
    if(!isZTarget(currentTarget)) return;
    const int maxParams = 5;
    // Map excess params to _bglXPn globals
    for(int i = maxParams; i < (int)fd->params.size(); i++)
        currentSpillAliases[fd->params[i]->name] = format("_bglXP{0}", i - maxParams);
    // Count only the params that fit in I6 locals
    int effectiveParams = min((int)fd->params.size(), maxParams);
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    vector<variableDeclaration*> locals;
    if(body) for(statement* s : body->statements)
        if(auto* vd = dynamic_cast<variableDeclaration*>(s)) locals.push_back(vd);
    int total = effectiveParams + (int)locals.size();
    if(total <= 14) return;
    int excess = total - 14;
    for(int i = (int)locals.size() - excess; i < (int)locals.size(); i++)
        currentSpillAliases[locals[i]->name] = format("_bglFrm-->{0}", currentSpillCount++);
}

void i6Emitter::clearSpillMap(){
    currentSpillAliases.clear();
    currentSpillCount = 0;
}

// Like expr->text() but substitutes spilled variable names token-by-token
string i6Emitter::exprText(expression* expr){
    if(!expr) return "";
    if(currentSpillAliases.empty()) return expr->text();
    string result;
    for(const string& t : expr->tokens){
        string tok = (t=="!=") ? "~=" : t;
        auto it = currentSpillAliases.find(tok);
        result += (it != currentSpillAliases.end()) ? it->second : tok;
    }
    return result;
}

// Single name lookup — returns alias if spilled, else the name unchanged
string i6Emitter::spillName(const string& name){
    auto it = currentSpillAliases.find(name);
    return (it != currentSpillAliases.end()) ? it->second : name;
}

// Word-boundary substitution of all spill aliases in a raw string (for initText/incrementText)
string i6Emitter::spillWord(const string& text){
    if(currentSpillAliases.empty()) return text;
    string result = text;
    for(auto& [from, to] : currentSpillAliases)
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
    if(languageService.ternaryTempNeeded)
        out << "global _bgl_temp;\n";
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

    // Synthesise bglInit — always emitted, even if empty; guarded against double-call
    out << "global _bglInitDone = 0;\n";
    out << "[bglInit;\n";
    out << "    if(_bglInitDone) return;\n";
    out << "    _bglInitDone = 1;\n";
    for(const string& block : languageService.startupBlocks)
        out << block << "\n";
    for(auto& [varName, body] : languageService.globalInits)
        out << "    " << body << "\n";
    out << "];\n";

    // Pass 3: emit everything else
    for(typeDef* node : nodeList)
        generateI6(node);

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
     if (typeid(*node) == typeid(enumDef))  emitEnum((enumDef*)node);
     else if (typeid(*node) == typeid(classDef)) emitClass((classDef*)node);
     else if (typeid(*node) == typeid(objectDef)) emitObject((objectDef*)node);
     else if (typeid(*node) == typeid(verbObjectDef)) emitVerbObject((verbObjectDef*)node);
     else if (auto* gtd = dynamic_cast<grammarRuleListDecl*>(node)) emitGrammarRuleListDecl(gtd);
     else if (auto* vd = dynamic_cast<variableDeclaration*>(node)) emitGlobal(vd);
     else if (typeid(*node) == typeid(functionDef)) emitFunction((functionDef*)node);
     else if (typeid(*node) == typeid(i6RawNode)) out << ((i6RawNode*)node)->text << "\n";
}

void i6Emitter::emitEnum(enumDef* enumNode){    
    for(enumValueDef* val : enumNode->namedValues)
        out<<format("constant _{0}_{1} = {2};\n", enumNode->name, val->name, val->value);    
}
void i6Emitter::emitClass(classDef* classNode){
    if(classNode->isExternal || classNode->isEmitterClass || classNode->isAlias) return;

    // emit static members as mangled globals before the class definition
    for(typeMember* m : classNode->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->isStatic){
                out << format("global _bgl_{0}_{1}", classNode->name, vd->name);
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
                out << format("    {0}", vd->name);
                if(vd->declaredExpressionValue != nullptr && !vd->declaredExpressionValue->text().empty())
                    out << format(" {0}", vd->declaredExpressionValue->text());
                out << sep << "\n";
            }
            else if(auto* fd = dynamic_cast<functionDef*>(m)){
                buildSpillMap(fd);
                out << format("    {0}[", fd->i6name.empty() ? fd->name : fd->i6name);
                string sp;
                for(paramDef* p : fd->params)
                    if(currentSpillAliases.find(p->name) == currentSpillAliases.end())
                        { out << sp << p->name; sp=" "; }
                statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
                if(body != nullptr)
                    for(statement* s : body->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                                { out << sp << vd->name; sp=" "; }
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
    out << format("[{0}", funcNode->name);
    for(paramDef* param : funcNode->params)
        if(currentSpillAliases.find(param->name) == currentSpillAliases.end())
            out << format(" {0}", param->name);

    statementBlock* body = dynamic_cast<statementBlock*>(funcNode->body);
    if(body != nullptr){
        for(statement* stmt : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(stmt))
                if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                    out << format(" {0}", vd->name);
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
            // $target substitution: for discarded call statements, use _bgl_temp
            b = replaceWord(b, "$target", "_bgl_temp");
            if(b.find("_bgl_temp") != string::npos) languageService.ternaryTempNeeded = true;
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
        out << indent << ((i6RawNode*)stmt)->text << "\n";
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
void i6Emitter::emitGlobal(variableDeclaration* varNode){
    if(varNode->isExternal) return;  // extern declarations are type-system only

    //--Array declarations. We use I6 tables and buffers exclusively for Beguile constructs
    if(auto* arr = dynamic_cast<arrayDeclaration*>(varNode)){
        string arraySubtype;
        if(arr->isByteArray) arraySubtype="buffer"; else arraySubtype="table"; 

        // initialize with string...
        if(!arr->stringInitializer.empty()) { 
            out << format("array {0} {1} {2};\n", arr->name, arraySubtype, arr->stringInitializer);
            return;
        }
        
        //no initialization, just allocating size
        if(arr->arraySize > 0) { 
            out << format("array {0} {1} {2};\n", arr->name, arraySubtype, arr->arraySize);
            return;
        }
        
        //initialize with a list of elements
        if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){ 
            out << format("array {0} {1}", arr->name, arraySubtype);
            for(expression* elem : list->elements) out << " " << elem->text();
            out << ";\n";
            return;
        } 
        //NOTE: if we got here, something's wrong.  The parser hasn't filled in what we need.
        throw ("i6Emitter: unable to emit array.");
        return;
    }

    if(varNode->isConst){
        out << format("constant {0}", varNode->name);
        if(varNode->declaredExpressionValue != nullptr)
            out << format(" = {0}", varNode->declaredExpressionValue->text());
        out << ";\n";
        return;
    }
    if(varNode->type.name == "attribute"){
        out << format("attribute {0}", varNode->name);
        out << ";\n";
        return;
    }
    const string& varI6Name = varNode->i6name.empty() ? varNode->name : varNode->i6name;
    bool isObject = dynamic_cast<objectDef*>(&languageService.getType(varNode->type.name)) != nullptr;
    if(isObject){
        string typeName = varNode->type.name;
        if(auto* cd = dynamic_cast<classDef*>(&languageService.getType(typeName))) typeName = cd->i6Name();
        out<<format("{0} {1}", typeName, varI6Name);
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
    const string& objI6Name = obj->i6name.empty() ? obj->name : obj->i6name;
    if(parentValue.empty())
        out << format("{0} {1}\n", i6ClassName, objI6Name);
    else
        out << format("{0} {1} {2}\n", i6ClassName, objI6Name, parentValue);

    // collect property members (includes raw i6 blocks, which emit as 'with' properties)
    // 'parent' is excluded — it's emitted as a positional argument, not a 'with' property
    bool hasProps = false;
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->type.name != "attributelist" && vd->type.name != "grammarrulelist" && vd->type.name != "grammarrule" && vd->name != "parent") { hasProps = true; break; }
        } else if(auto* fd = dynamic_cast<functionDef*>(m)){ if(!fd->isEmitter) { hasProps = true; break; } }
          else if(dynamic_cast<i6RawNode*>(m))  { hasProps = true; break; }

    if(hasProps){
        bool first = true;
        for(typeMember* m : obj->members){
            if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
                // Property array: emit as inline I6 property values
                out << (first ? "  with " : ",\n       ");
                out << arr->name << " ";
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
                if(vd->type.name == "attributelist") continue; // handled separately below
                if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue; // emitted as I6 Verb directives
                if(vd->name == "parent") continue; // emitted as positional argument, not 'with' property
                out << (first ? "  with " : ",\n       ");
                out << vd->name << " ";
                if(vd->declaredExpressionValue) out << vd->declaredExpressionValue->text();
                first = false;
            } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->isEmitter) continue; // emitter methods are inlined at call sites, not emitted as properties
                buildSpillMap(fd);
                out << (first ? "  with " : ",\n       ");
                out << fd->name << " [";
                string sp;
                for(paramDef* p : fd->params)
                    if(currentSpillAliases.find(p->name) == currentSpillAliases.end())
                        { out << sp << p->name; sp=" "; }
                statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
                if(body)
                    for(statement* s : body->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                                { out << sp << vd->name; sp=" "; }
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