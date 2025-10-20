#pragma once
#include <string>
#include <sstream>
#include <deque>

#include "token.h"
#include "fileReader.h"
#include "emitter.h"

struct resultsStruct{
    std::stringstream tempText;
    std::stringstream bodyText;
};

enum class eCompileLanguage {beguile, i6};
enum class eCompileScope {root, classDef, codeBlock, languageBlock};


class parser {
    public:
        emitter emit;
        resultsStruct results;
        fileReader file;
        
 
        parser();
        bool parseFile(std::string);
        void parseError(std::string);
        void processFunctionBody(token);
        eCompileScope resolveCurrentCompileScope(); 
        
        void openCompileScope(eCompileScope); 
        void closeCompileScope(); 
        
    private:
        std::deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        std::deque<eCompileScope> compileScopeStack; 
        
        void emitTo(std::ostream&);
        eCompileLanguage getCurrentLanguage(); 

        bool processNextStatement();
        void processFunctionParams();
        void processEnumOrFlags(token, bool);
        
        void processFunctionCall(token, token=_nullToken);
        bool getArgumentExpression(std::string&);
        void processI6();

        void emitVariable(token, token, token= _nullToken);
        
};
