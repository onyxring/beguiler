#include <iostream>
#include <fstream>
#include <sstream>

#include "i6Emitter.h"
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
void i6Emitter::emit(vector<typeDef*>& nodeList){
    // Pass 1: emit ICL !% directives at the very top (Inform reads these before parsing)
    emitICL(&beguilerSettings);
    currentTarget = beguilerSettings.target;
    for(char& c : currentTarget) c = (char)tolower(c);
    framePoolSize = beguilerSettings.framePoolSize;

    // Pass 2: emit any settings-derived constants
    emitSettingsConstants(&beguilerSettings);

    // Emit ternary scratch variable only when actually used
    if(languageService.ternaryTempNeeded)
        out << "global _bgl_temp;\n";

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
            else if(auto* cd = dynamic_cast<classDef*>(node))
                for(typeMember* m : cd->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m)) scanFd(fd);
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

    // Pass 3: emit everything else
    for(typeDef* node : nodeList)
        generateI6(node);

    // Pass 4: synthesise initBeguile — only emitted if there are global inits
    if(!languageService.globalInits.empty()){
        out << "[initBeguile;\n";
        for(auto& [varName, body] : languageService.globalInits)
            out << "    " << body << "\n";
        out << "];\n";
    }
}
void i6Emitter::emitICL(beguilerSettingsDef* cfg){
    if(cfg->target == "glulx")     out << "!% -G\n";
    else if(cfg->target == "z3")   out << "!% -v3\n";
    else if(cfg->target == "z5")   out << "!% -v5\n";
    else if(cfg->target == "z8")   out << "!% -v8\n";
    if(!cfg->errorFormat.empty())  out << format("!% -E{0}\n", cfg->errorFormat);
    if(cfg->release > 0)           out << format("!% Release {0};\n", cfg->release);

    if(!cfg->i6IncludePaths.empty()){
        out << "!% +include_path=";
        for(size_t i=0; i<cfg->i6IncludePaths.size(); i++){
            if(i>0) out << ",";
            out << cfg->i6IncludePaths[i];
        }
        out << "\n";
    }
}
void i6Emitter::emitSettingsConstants(beguilerSettingsDef* cfg){
}
void i6Emitter::generateI6(typeDef* node){
     if (typeid(*node) == typeid(enumDef))  emitEnum((enumDef*)node);
     else if (typeid(*node) == typeid(classDef)) emitClass((classDef*)node);
     else if (typeid(*node) == typeid(objectDef)) emitObject((objectDef*)node);
     else if (auto* vd = dynamic_cast<variableDeclaration*>(node)) emitGlobal(vd);
     else if (typeid(*node) == typeid(functionDef)) emitFunction((functionDef*)node);
     else if (typeid(*node) == typeid(i6RawNode)) out << ((i6RawNode*)node)->text << "\n";
     else if (typeid(*node) == typeid(verbObjectDef)) emitVerbObject((verbObjectDef*)node);
     else if (typeid(*node) == typeid(grammarBlock)) emitGrammarBlock((grammarBlock*)node);
}

void i6Emitter::emitEnum(enumDef* enumNode){    
    for(enumValueDef* val : enumNode->namedValues)
        out<<format("constant _{0}_{1} = {2};\n", enumNode->name, val->name, val->value);    
}
void i6Emitter::emitClass(classDef* classNode){
    if(classNode->isExternal || classNode->isEmitterClass || classNode->isAlias) return;

    out << format("class {0}\n", classNode->i6Name());

    if(classNode->baseClasses.size() > 0){
        out << "  class";
        for(classDef* base : classNode->baseClasses) out << format(" {0}", base->i6Name());
        out << "\n";
    }

    // collect emittable members (skip emitter-only functions)
    vector<typeMember*> emittable;
    for(typeMember* m : classNode->members){
        if(auto* fd = dynamic_cast<functionDef*>(m))
            if(fd->isEmitter) continue;
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
                out << format("    {0}[", fd->name);
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
    if(funcNode->isEmitter || funcNode->isExternal) return;
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
        for(statement* stmt : body->statements){
            if(!stmt->src.file.empty())
                sourceMap.push_back({currentLine(), stmt->src.file, stmt->src.line});
            emitStatement(stmt, "    ");
        }
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
    if(typeid(*stmt) == typeid(variableDeclaration)){
        variableDeclaration* var = (variableDeclaration*)stmt;
        // spilled vars have no I6 local slot — only emit the initializer assignment if they have one
        if(var->declaredExpressionValue != nullptr && !var->declaredExpressionValue->text().empty())
            out << format("{0}{1} = {2};\n", indent, spillName(var->name), exprText(var->declaredExpressionValue));
    }
    else if(typeid(*stmt) == typeid(assignmentStatement)){
        assignmentStatement* assign = (assignmentStatement*)stmt;
        if(!assign->emitterBody.empty()){
            string b = assign->emitterBody;
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
            b=replaceWord(b,"$self", assign->emitterSelf.empty() ? spillName(assign->variableLeft) : spillName(assign->emitterSelf));
            b=replaceWord(b,assign->emitterParam, assign->assignedExpression != nullptr ? exprText(assign->assignedExpression) : "");
            while(!b.empty() && b.back()==';') b.pop_back();
            out << indent << b << ";\n";
        } else {
            out << format("{0}{1} = {2};\n", indent, spillName(assign->variableLeft), assign->assignedExpression != nullptr ? exprText(assign->assignedExpression) : "");
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
    else if(typeid(*stmt) == typeid(interpolatedPrintStatement)){
        interpolatedPrintStatement* ps = (interpolatedPrintStatement*)stmt;
        // isLog case: DEBUG guard is handled at parse time (non-DEBUG calls are dropped entirely)
        for(auto& seg : ps->segments){
            if(!seg.isExpr){
                // String segment: emit as I6 string literal
                if(!seg.text.empty())
                    out << indent << "print \"" << seg.text << "\";\n";
            } else {
                string rt = seg.expr->resolvedType;
                string exprStr = exprText(seg.expr);

                // For class types, look for a no-arg print() emitter on the class and inline it.
                // This lets $"Score: {scoreObj}" honour a custom print emitter on the class.
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
                            // trim and strip trailing semicolons so we can re-add one cleanly
                            size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                            size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                            out << indent << b << ";\n";
                            continue;
                        }
                    }
                    if(printFn != nullptr && !printFn->isEmitter){
                        // Regular method: emit as a call
                        out << indent << exprStr << ".print();\n";
                        continue;
                    }
                    // No print() on this class — fall through to generic emit below
                }

                // Primitive / unknown type: add I6 cast based on resolved type
                string cast;
                if(rt == "string" || rt == "stringliteral") cast = "(string)";
                else if(rt == "char" || rt == "charliteral") cast = "(char)";
                out << indent << "print " << cast << exprStr << ";\n";
            }
        }
    }
    else if(typeid(*stmt) == typeid(functionCallStatement)){
        functionCallStatement* call = (functionCallStatement*)stmt;
        if(!call->emitterBody.empty()){
            string b = call->emitterBody;
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
            for(size_t i=0; i<call->emitterParams.size() && i<call->args.size(); i++)
                b=replaceWord(b, call->emitterParams[i], exprText(call->args[i]));
            while(!b.empty() && b.back()==';') b.pop_back();
            out << indent << b << ";\n";
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
        applyTemplate("forIn.open",
            {{"counter", spillName(fi->counterVar)}, {"array", spillName(fi->arrayVar)}, {"element", spillName(fi->elementVar)}},
            indent);
        if(fi->body != nullptr)
            for(statement* s : fi->body->statements)
                emitStatement(s, indent + "    ");
        applyTemplate("forIn.close", {}, indent);
    }
    else if(typeid(*stmt) == typeid(switchStatement)){
        switchStatement* sw = (switchStatement*)stmt;
        out << indent << "switch (" << (sw->condition != nullptr ? exprText(sw->condition) : "") << ") {\n";
        for(switchCase* sc : sw->cases){
            if(!sc->values.empty()){
                out << indent << "    ";
                for(size_t i = 0; i < sc->values.size(); i++){
                    if(i > 0) out << ", ";
                    // verb-typed case values are action constants → emit as ##VerbName
                    if(sc->values[i]->resolvedType == "verb")
                        out << "##" << sc->values[i]->text();
                    else
                        out << sc->values[i]->text();
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
    else if(typeid(*stmt) == typeid(i6RawNode)){
        out << indent << ((i6RawNode*)stmt)->text << "\n";
    }
}
void i6Emitter::emitGlobal(variableDeclaration* varNode){
    if(varNode->isExternal) return;  // extern declarations are type-system only

    // Array declarations emit as I6 Array directives
    if(auto* arr = dynamic_cast<arrayDeclaration*>(varNode)){
        if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
            // Initialized: Array name --> count v1 v2 ...
            // Count is auto-prepended (table semantics: element 0 holds length)
            out << format("array {0} --> {1}", arr->name, list->elements.size());
            for(expression* elem : list->elements) out << " " << elem->text();
            out << ";\n";
        } else if(arr->arraySize > 0) {
            // Sized: Array name table N  (I6 sets element 0 = N automatically)
            out << format("array {0} table {1};\n", arr->name, arr->arraySize);
        }
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
    bool isObject = dynamic_cast<objectDef*>(&languageService.getType(varNode->type.name)) != nullptr;
    if(isObject){
        string typeName = varNode->type.name;
        if(auto* cd = dynamic_cast<classDef*>(&languageService.getType(typeName))) typeName = cd->i6Name();
        out<<format("{0} {1}", typeName, varNode->name);
    }
    else
        out<<format("global {0}", varNode->name);
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

    // Use the declared class name (if any) as the I6 object prefix; fall back to 'Object'
    string i6ClassName = (obj->objectClass && obj->objectClass->name != "object")
                         ? obj->objectClass->i6Name() : "object";
    if(parentValue.empty())
        out << format("{0} {1}\n", i6ClassName, obj->name);
    else
        out << format("{0} {1} {2}\n", i6ClassName, obj->name, parentValue);

    // collect property members (includes raw i6 blocks, which emit as 'with' properties)
    // 'parent' is excluded — it's emitted as a positional argument, not a 'with' property
    bool hasProps = false;
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->type.name != "attributecollection" && vd->name != "parent") { hasProps = true; break; }
        } else if(dynamic_cast<functionDef*>(m)){ hasProps = true; break; }
          else if(dynamic_cast<i6RawNode*>(m))  { hasProps = true; break; }

    if(hasProps){
        bool first = true;
        for(typeMember* m : obj->members){
            if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
                // Property array: emit as inline I6 property values
                out << (first ? "  with " : ",\n       ");
                out << arr->name << " ";
                if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
                    for(expression* elem : list->elements) out << elem->text() << " ";
                } else {
                    // N zero slots (size is encoded via obj.#prop, not in element 0)
                    for(int k = 0; k < arr->arraySize; k++) out << "0 ";
                }
                first = false;
            } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->type.name == "attributecollection") continue; // handled separately below
                if(vd->name == "parent") continue; // emitted as positional argument, not 'with' property
                out << (first ? "  with " : ",\n       ");
                out << vd->name << " ";
                if(vd->declaredExpressionValue) out << vd->declaredExpressionValue->text();
                first = false;
            } else if(auto* fd = dynamic_cast<functionDef*>(m)){
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

    // emit attributeCollection members as I6 'has' line
    for(typeMember* m : obj->members){
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->type.name != "attributecollection") continue;
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
        body = replaceWord(body, "$selfsub", obj->name + "sub");
        body = replaceWord(body, "$self",    obj->name);
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

void i6Emitter::emitGrammarBlock(grammarBlock* gb){
    emitGrammarLines(gb->verbName, gb->grammarLines);
}

// Group grammar lines by verb trigger word; emit one Verb/Extend block per unique trigger word.
// First occurrence of a trigger word → Verb 'word'; subsequent → Extend 'word' first.
void i6Emitter::emitGrammarLines(const string& verbName, const vector<grammarLine>& lines){
    vector<string> wordOrder;
    map<string, vector<vector<string>>> byWord;
    for(const grammarLine& line : lines){
        if(byWord.find(line.verbWord) == byWord.end())
            wordOrder.push_back(line.verbWord);
        byWord[line.verbWord].push_back(line.patternTokens);
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
        const auto& patterns = byWord[word];
        for(size_t i = 0; i < patterns.size(); i++){
            out << "    *";
            for(const string& pt : patterns[i]) out << " " << pt;
            out << format(" -> {0}", verbName);
            if(i + 1 == patterns.size()) out << ";";
            out << "\n";
        }
    }
}

i6Emitter emitter;