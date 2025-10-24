#pragma once
#include <string>
#include <sstream>
#include <stack>
#include "token.h"

class emitter{
    public:
        std::stringstream out; 

        void to(std::ostream&);
        void globalFunction(token, token);
        void functionParams();
        void functionCall(token, token=_nullToken);
        void enumOrFlags(token,bool);
        void globalVariable(token, token, token= _nullToken);
        void variable(token, token, token= _nullToken);
        void put(std::string);
        void put(token);
        void indent();

        //bool getArgumentExpression(std::string&);
};