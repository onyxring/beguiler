#include <iostream>

#include "i6Emitter.h"
#include "bglParser.h"
#include "orbit.h"
#include "globals.h"
#include "parseNode.h"
#include "bglLanguageService.h"

using namespace std;

//The emitter writes to a standard output stream.  We can reassign this if we want...
void i6Emitter::to(ostream& strm){ out.std::ios::rdbuf(strm.rdbuf()); }


void i6Emitter::put(string str){ out<<str; }
void i6Emitter::put(token tok){ out<<tok.value; }

void i6Emitter::generateI6(parseNode& node){
    indent(node);
    switch(node.type){
        case eNodeType::root:
            rootNode(node);
            return;
        case eNodeType::directive:
             directive(node);
             return;
        case eNodeType::routine:
            if(node.parent->type==eNodeType::root)
                out<<format("[{0}", (string)node["routineName"]);
            else
                out<<format("{0}[", (string)node["routineName"]);
            //params= &node["parameters"]; 
            
            for(parseNode& param : node["parameters"].children){
                out<<" "<<(string)param["variableName"];
                //if(param.properties.contains("assignedValue")){
                    //defaultInits+=format("if({0}==0) {0}={1}; ", (string)param["variableName"], (string)param["assignedValue"]);
                //}   
            }
            endStatement();
            
            // if(defaultInits!=""){
            //     indent(node,1);
            //     out<<defaultInits<<endl;
            // }
            
            for(parseNode& statement : node.children){
                generateI6(statement); 
            }
            
            indent(node);
            out<<"]";
            if(node.parent->type==eNodeType::root) endStatement(); 
            newLine();
            return;
        case eNodeType::variableDeclaration:           
            if(node.resolveContext()==eCompileContext::global){
                if(languageService.isObjectType((string)node["dataType"])){
                    out<<format("{0} {1}", (string)node["dataType"], (string)node["variableName"]);
                }
                else{
                    out<<format("global {0}", (string)node["variableName"]);
                }
                if(node.properties.contains("assignedValue")){
                    out<<format("={0}", (string)node["assignedValue"]);
                }
                endStatement();
            }
            else{
                out<<format("{0}", (string)node["variableName"]);
                if(node.properties.contains("assignedValue")){
                    out<<format(" {0}", (string)node["assignedValue"]);
                }
            }
           return;
           break;
        case eNodeType::constantDeclaration:           
            out<<format("constant  {0}={1};", (string)node["variableName"],(string)node["assignedValue"]);
            newLine();
            return;
            break;
        case eNodeType::executableStatement:
            executableStatement(node);
            return;
        case eNodeType::objectDeclaration:
            objectDeclaration(node);
            return;
        
        
    //     case eNodeType::objectDeclaration:
    //         out<<format("Object {0} {{\n", node.keyToken.value);
    //         return;
    //     case eNodeType::classDeclaration:
    //         out<<format("Class {0} {{\n", node.keyToken.value);
    //         return;
    //     default:
    //         break;
    }

}

void i6Emitter::directive(parseNode& node){
    switch(((token)node).chk()){
        case chk("#include"): out<<format("{0} {1};\n", (string)node, (string)node["filename"]);
            break;
        case chk("#i6"): out<<format("{0}\n", (string)node["i6Content"]);
            break;
    }
}
void i6Emitter::objectDeclaration(parseNode& node){
    out<<format("{0} {1} ", (string)node["objectType"], (string)node["objectName"]);
    
    bool isFirst=true;
    for(parseNode& child : node.children) {
        if(((string)child)!="#i6") {
            if(isFirst)
                out<<" with ";
            else
                out<<", ";
            isFirst=false;
        }
        generateI6(child); 
        out<<endl;
    }
    out<<";\n";
}
void i6Emitter::executableStatement(parseNode& node){
    switch(((token)node["statement"]).chk()){
        case chk("print"):
            out<<"print ";
            out<<(string)node["value"];
            endStatement();
            return;
        case chk("return"):
            //TODO: perform type checking against the declared return type of the routine
            if(node.properties.contains("returnValue")){
                switch(((token)node["returnValue"]).chk()){
                    case chk("true"): out << "rtrue";
                        break;
                    case chk("false"): out << "rfalse";
                        break;
                    default: out<<"return "<<(string)node["returnValue"];
                }
            }
            else{
                out<<"return";
            }
            endStatement();
            return;
        default:
            //assume it's a function call
            out<<(string)node["functionName"];
            if(node.properties.contains("memberName")){
                out<<"."<<(string)node["memberName"];
            }
            out<<"(";
        }
}
void i6Emitter::endStatement(){
    out<<";";
    newLine();
}
void i6Emitter::newLine(){
    out<<"\n";
}

void i6Emitter::rootNode(parseNode & node){
    out<<"!% +include_path=../_myExtensions,../../orLibraryI6,../../inform6/lib\n";
    for(auto& child : node.children){
        generateI6(child);
    }
}
// void i6Emitter::globalVariable(token datatype, token id, token val){
//     out<<format("global {0}",(string)id);
    
//     if(!val.isNull())  out<<format("={0}",(string)val);
    
//     endStatement();
//     return;

// }
void i6Emitter::indent(parseNode& node, int extra){
    for(int t=0; t<node.resolveNestingDepth()+extra;t++) out<<"   ";    
}
void i6Emitter::indent(int extra){
    for(int t=0; t<parser.getCurrentNode().resolveNestingDepth()+extra;t++) out<<"   ";    
}

/*
void i6Emitter::globalFunction(token returnType, token name){
    indent();
    out<<format("[{0}",name.value);
    indent();
    functionParams(); 
    parser.processFunctionBody(returnType);
    indent();
    out<<"];";
}
*/
/*
void i6Emitter::functionParams(){
    string paramDefaultInit="";
    token datatype = parser.file.getToken(); 
    while(datatype.isNot(token::parenClose)){
        datatype.assertDataType();
        token varname=parser.file.getToken(eTokenType::identifier); 
        out<<" "<<varname.value;
        token symbol = parser.file.getToken(eTokenType::symbol); 
        if(symbol.is(token::assignment)){
            token val=parser.file.getToken({eTokenType::integer, eTokenType::quote});  
            if(val.tokenType==eTokenType::quote) datatype.assertOneOf({"string", "var"}, "Illegal default definition: string value is incompatible with type '"+datatype.value+"'.");
            paramDefaultInit+=format("if({0}==0){0}={1}; ",varname.value,val.value); //TODO: testing for zero doesn't work for all int types, because zero is a common and valid value that could be passed in.  Think about how to address this.
            //TODO: strings should be orStrings, with the appropriate new() and free() calls
            //TODO: add a list of variables with types, so that we can emit different code based on type, orString being the most obvious example of where this is needed
            datatype = parser.file.getToken(eTokenType::symbol); 
            if(datatype.value==token::comma) datatype = parser.file.getToken(eTokenType::dataType); 
        }
        else datatype=symbol;
    }
    out<<"; ";            
    //indent(1);
    out<<paramDefaultInit<<endl;            
}
*/
// bool parser::error(string msg){
//     string errorMessage;
//     if(file.numOpen()>0) {
//         auto [inputFileStream, fileName, curLine, curCol]=file.getDetail(); 
//         //errorMessage=format("\033[1m{0}:{1}:{2}: \x1b[31merror:\x1b[0m {3}",fileName,curLine,curCol,msg); 
//         errorMessage=format("{0}:{1}:{2}: error: {3}",fileName,curLine,curCol,msg); 
//     }
//     else{
//         errorMessage=msg; 
//     }
    
//     throw runtime_error(errorMessage); 
//     return true; //won't every actually run
// }