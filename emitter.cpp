#include "emitter.h"
#include "parser.h"
#include "orbit.h"
#include <iostream>

using namespace std;

void emitter::put(string str){ 
    out<<str; 
}
void emitter::put(token tok){ 
    out<<tok.value; 
}

void emitter::to(ostream& strm){ out.std::ios::rdbuf(strm.rdbuf()); }
void emitter::variable(token datatype, token id, token val){
    if(val.isNull())  {
        out<<format("{0};\n",id.value);
        return;
    }
    
    if(val.tokenType==eTokenType::quote) datatype.assertOneOf({"string", "var"}, "Illegal assignment of string literal to data type '"+datatype.value+"'.");

    out<<format("{0}={1};\n",id.value,val.value);
}
void emitter::endStatement(){
    out<<";\n";
}
void emitter::newLine(){
    out<<"\n";
}
void emitter::globalVariable(token datatype, token id, token val){
    out<<"Global ";
    variable(datatype, id, val);
}
void emitter::indent(int extra){
    for(int t=0; t<bglParser.getScopeNestingDepth()+extra-1;t++) out<<"   ";
}

void emitter::globalFunction(token returnType, token name){
    indent();
    out<<format("[{0}",name.value);
    indent();
    functionParams(); 
    bglParser.processFunctionBody(returnType);
    indent();
    out<<"];";
}

//TODO: move this into parser.  We shouldn't be reading all this from the emitter
void emitter::functionParams(){
    string paramDefaultInit="";
    token datatype = bglParser.file.getToken(); 
    while(datatype.isNot(token::parenClose)){
        datatype.assertDataType();
        token varname=bglParser.file.getToken(eTokenType::identifier); 
        out<<" "<<varname.value;
        token symbol = bglParser.file.getToken(eTokenType::symbol); 
        if(symbol.is(token::assignment)){
            token val=bglParser.file.getToken({eTokenType::integer, eTokenType::quote});  
            if(val.tokenType==eTokenType::quote) datatype.assertOneOf({"string", "var"}, "Illegal default definition: string value is incompatible with type '"+datatype.value+"'.");
            paramDefaultInit+=format("if({0}==0){0}={1}; ",varname.value,val.value); //TODO: testing for zero doesn't work for all int types, because zero is a common and valid value that could be passed in.  Think about how to address this.
            //TODO: strings should be orStrings, with the appropriate new() and free() calls
            //TODO: add a list of variables with types, so that we can emit different code based on type, orString being the most obvious example of where this is needed
            datatype = bglParser.file.getToken(eTokenType::symbol); 
            if(datatype.value==token::comma) datatype = bglParser.file.getToken(eTokenType::dataType); 
        }
        else datatype=symbol;
    }
    out<<"; ";            
    //indent(1);
    out<<paramDefaultInit<<endl;            
}
