#include <iostream>
#include <fstream>

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
    for(typeDef* node : nodeList)
        if(typeid(*node) == typeid(beguilerSettingsDef))
            emitICL((beguilerSettingsDef*)node);

    // Pass 2: emit Story/Headline constants from beguilerSettings
    for(typeDef* node : nodeList)
        if(typeid(*node) == typeid(beguilerSettingsDef))
            emitSettingsConstants((beguilerSettingsDef*)node);

    // Pass 3: emit everything else
    for(typeDef* node : nodeList)
        if(typeid(*node) != typeid(beguilerSettingsDef))
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
    if(cfg->target == "Glulx")     out << "!% -G\n";
    else if(cfg->target == "Z3")   out << "!% -v3\n";
    else if(cfg->target == "Z5")   out << "!% -v5\n";
    else if(cfg->target == "Z8")   out << "!% -v8\n";
    if(!cfg->errorFormat.empty())  out << format("!% -E{0}\n", cfg->errorFormat);
    if(cfg->release > 0)           out << format("!% Release {0};\n", cfg->release);

    if(!cfg->includePaths.empty()){
        out << "!% +include_path=";
        for(size_t i=0; i<cfg->includePaths.size(); i++){
            if(i>0) out << ",";
            out << cfg->includePaths[i];
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
}

void i6Emitter::emitEnum(enumDef* enumNode){    
    for(enumValueDef* val : enumNode->namedValues)
        out<<format("Constant _{0}_{1} = {2};\n", enumNode->name, val->name, val->value);    
}
void i6Emitter::emitClass(classDef* classNode){
    if(classNode->isExternal) return;

    out << format("Class {0}\n", classNode->name);

    if(classNode->baseClasses.size() > 0){
        out << "  class";
        for(classDef* base : classNode->baseClasses) out << format(" {0}", base->name);
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
                out << format("    {0}[", fd->name);
                string sp;
                for(paramDef* p : fd->params) { out << sp << p->name; sp=" "; }
                statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
                if(body != nullptr)
                    for(statement* s : body->statements)
                        if(typeid(*s) == typeid(variableDeclaration))
                            { out << sp << ((variableDeclaration*)s)->name; sp=" "; }
                out << ";\n";
                if(body != nullptr)
                    for(statement* s : body->statements)
                        emitStatement(s, "        ");
                out << "    ]" << sep << "\n";
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
    if(!funcNode->src.file.empty())
        sourceMap.push_back({currentLine(), funcNode->src.file, funcNode->src.line});
    out << format("[{0}", funcNode->name);
    for(paramDef* param : funcNode->params)
        out << format(" {0}", param->name);

    statementBlock* body = dynamic_cast<statementBlock*>(funcNode->body);
    if(body != nullptr){
        for(statement* stmt : body->statements)
            if(typeid(*stmt) == typeid(variableDeclaration))
                out << format(" {0}", ((variableDeclaration*)stmt)->name);
    }
    out << ";\n";

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
    currentCleanups = nullptr;
    out << "];\n";
}
void i6Emitter::emitStatement(statement* stmt, string indent){
    auto replaceWord=[](string str, const string& from, const string& to){
        size_t pos=0;
        while((pos=str.find(from,pos))!=string::npos){
            bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$');
            bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
            if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
            else pos+=from.size();
        }
        return str;
    };

    if(typeid(*stmt) == typeid(variableDeclaration)){
        variableDeclaration* var = (variableDeclaration*)stmt;
        if(var->declaredExpressionValue != nullptr && !var->declaredExpressionValue->text().empty())
            out << format("{0}{1} = {2};\n", indent, var->name, var->declaredExpressionValue->text());
    }
    else if(typeid(*stmt) == typeid(assignmentStatement)){
        assignmentStatement* assign = (assignmentStatement*)stmt;
        if(!assign->emitterBody.empty()){
            string b = assign->emitterBody;
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
            b=replaceWord(b,"$self",assign->variableLeft);
            b=replaceWord(b,assign->emitterParam, assign->assignedExpression != nullptr ? assign->assignedExpression->text() : "");
            out << indent << b << "\n";
        } else {
            out << format("{0}{1} = {2};\n", indent, assign->variableLeft, assign->assignedExpression != nullptr ? assign->assignedExpression->text() : "");
        }
    }
    else if(typeid(*stmt) == typeid(returnStatement)){
        returnStatement* ret = (returnStatement*)stmt;
        // emit deinit cleanups before every return
        if(currentCleanups != nullptr)
            for(auto& [varName, body] : *currentCleanups)
                out << indent << body << "\n";
        if(ret->returnExpression != "")
            out << format("{0}return {1};\n", indent, ret->returnExpression);
        else
            out << indent << "return;\n";
    }
    else if(typeid(*stmt) == typeid(functionCallStatement)){
        functionCallStatement* call = (functionCallStatement*)stmt;
        if(!call->emitterBody.empty()){
            string b = call->emitterBody;
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
            for(size_t i=0; i<call->emitterParams.size() && i<call->args.size(); i++)
                b=replaceWord(b, call->emitterParams[i], call->args[i]->text());
            out << indent << b << "\n";
        } else {
            out << indent << call->functionName << token::parenOpen;
            for(size_t i=0; i<call->args.size(); i++){
                if(i>0) out << ", ";
                out << call->args[i]->text();
            }
            out << token::parenClose << ";\n";
        }
    }
    else if(typeid(*stmt) == typeid(ifStatement)){
        ifStatement* ifNode = (ifStatement*)stmt;
        out << indent << "if (" << (ifNode->condition != nullptr ? ifNode->condition->text() : "") << ") {\n";
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
        string cond = doNode->condition != nullptr ? doNode->condition->text() : "";
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
        out << indent << "while (" << (whileNode->condition != nullptr ? whileNode->condition->text() : "") << ") {\n";
        if(whileNode->body != nullptr)
            for(statement* s : whileNode->body->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
    }
    else if(typeid(*stmt) == typeid(forStatement)){
        forStatement* forNode = (forStatement*)stmt;
        out << indent << "for (" << forNode->initText << " : ";
        out << (forNode->condition != nullptr ? forNode->condition->text() : "") << " : ";
        out << forNode->incrementText << ") {\n";
        if(forNode->body != nullptr)
            for(statement* s : forNode->body->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
    }
    else if(typeid(*stmt) == typeid(switchStatement)){
        switchStatement* sw = (switchStatement*)stmt;
        out << indent << "switch (" << (sw->condition != nullptr ? sw->condition->text() : "") << ") {\n";
        for(switchCase* sc : sw->cases){
            if(sc->value != nullptr)
                out << indent << "    " << sc->value->text() << ":\n";
            else
                out << indent << "    default:\n";
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
            out << format("Array {0} --> {1}", arr->name, list->elements.size());
            for(expression* elem : list->elements) out << " " << elem->text();
            out << ";\n";
        } else if(arr->arraySize > 0) {
            // Sized: Array name table N  (I6 sets element 0 = N automatically)
            out << format("Array {0} table {1};\n", arr->name, arr->arraySize);
        }
        return;
    }

    if(varNode->isConst){
        out << format("Constant {0}", varNode->name);
        if(varNode->declaredExpressionValue != nullptr)
            out << format(" = {0}", varNode->declaredExpressionValue->text());
        out << ";\n";
        return;
    }
    if(varNode->type.name == "attribute"){
        out << format("Attribute {0}", varNode->name);
        out << ";\n";
        return;
    }
    bool isObject = dynamic_cast<objectDef*>(&languageService.getType(varNode->type.name)) != nullptr;
    if(isObject)
        out<<format("{0} {1}", varNode->type.name, varNode->name);
    else
        out<<format("Global {0}", varNode->name);
    if(varNode->declaredExpressionValue != nullptr)
        out<<format(" = {0}", varNode->declaredExpressionValue->text());
    out<<";\n";
}
void i6Emitter::emitObject(objectDef* obj){
    out << format("Object {0}\n", obj->name);

    // collect property members and raw i6 members
    bool hasProps = false;
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->type.name != "attributecollection") { hasProps = true; break; }
        } else if(dynamic_cast<functionDef*>(m)){ hasProps = true; break; }

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
                out << (first ? "  with " : ",\n       ");
                out << vd->name << " ";
                if(vd->declaredExpressionValue) out << vd->declaredExpressionValue->text();
                first = false;
            } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                out << (first ? "  with " : ",\n       ");
                out << fd->name << " [";
                // emit params as I6 locals
                for(paramDef* p : fd->params) out << " " << p->name;
                out << ";\n";
                statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
                if(body)
                    for(statement* s : body->statements)
                        emitStatement(s, "    ");
                out << "  ]";
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

    // emit raw i6 blocks (attribute lines etc.)
    for(typeMember* m : obj->members){
        if(auto* raw = dynamic_cast<i6RawNode*>(m)){
            string text = raw->text;
            size_t s = text.find_first_not_of(" \t\n\r");
            size_t e = text.find_last_not_of(" \t\n\r");
            if(s != string::npos) text = text.substr(s, e - s + 1);
            out << "  " << text << "\n";
        }
    }

    out << ";\n";
}

i6Emitter emitter;