#include "emitter.h"
#include "parser.h"
#include "orbit.h"
#include <iostream>

using namespace std;


void emitter::put(string str){ 
    out<<str; 
}
void emitter::put(token tok){ 
    out<<tok.text; 
}

void emitter::to(ostream& strm){ out.std::ios::rdbuf(strm.rdbuf()); }
void emitter::variable(token datatype, token id, token val){
    if(val.isNull())  {
        out<<format("{0};\n",id.text);
        return;
    }
    
    if(val.tokenType==eTokenType::quote) datatype.assertOneOf({"string", "var"}, "Illegal assignment of string literal to data type '"+datatype.text+"'.");

    out<<format("{0}={1};\n",id.text,val.text);
}
void emitter::globalVariable(token datatype, token id, token val){
    out<<"Global ";
    variable(datatype, id, val);
}

void emitter::globalFunction(token returnType, token name){
    out<<format("[{0}",name.text);
    functionParams(); 
    beguiler.processFunctionBody(returnType);
    out<<"];";
}

void emitter::functionParams(){
    string init=";\n";
    token datatype = beguiler.file.getToken(); 
    while(datatype.isNot(token::parenClose)){
        datatype.assertDataType();
        token varname=beguiler.file.getToken(eTokenType::text); 
        out<<" "<<varname.text;
        token symbol = beguiler.file.getToken(eTokenType::symbol); 
        if(symbol.is(token::assignment)){
            token val=beguiler.file.getToken({eTokenType::text, eTokenType::quote});  
            if(val.tokenType==eTokenType::quote) datatype.assertOneOf({"string", "var"}, "Illegal default definition: string value is incompatible with type '"+datatype.text+"'.");
            init+=format("  if({0}==0){0}={1};",varname.text,val.text); //TODO: testing for zero doesn't work for all int types, because zero is a common and valid value that could be passed in.  Think about how to address this.
            //TODO: strings should be orStrings, with the appropriate new() and free() calls
            //TODO: add a list of variables with types, so that we can emit different code based on type, orString being the most obvious example of where this is needed
            datatype = beguiler.file.getToken(eTokenType::symbol); 
            if(datatype.text==token::comma) datatype = beguiler.file.getToken(eTokenType::text); 
        }
        else datatype=symbol;
    }
    out<<init<<endl;            
}
