#pragma once
#include <string>
#include <sstream>
#include <deque>

#include "globals.h"
#include "token.h"
#include "fileLexer.h"
#include "i6Emitter.h"
#include "parseNode.h"
#include "typeDef.h"

using namespace std;

class bglParser {
    public:
        
        fileLexer file;         //what the parser reads from.  Tokens are produced by the filelexer.
        parseNode parseTree;    //where the parser stores what it interprets from tokens read in.
        parseNode tags;         //a placeholder to store any delared tags
        i6Emitter emit;         //how the parseTree is saved out as into i6 code

        resultsStruct results;// TODO: is this still needed?

        std::deque<parseNode*> currentNodeStack; //as we are generating nodes on the parseTree, we leverage this to help us mark where we are, as we nest nodes as children, of other nodes
 
        bglParser();
        
        bool parseFile(std::string);    //the main entry point: given a file, read it in, parse it, and store it in the parse tree
        bool parseError(std::string);   //called when there is a parse error, to output the error message and the place in the code where it appeared
        

        // void processFunctionBody(token);
        
        //as we are parsing a file, we enter and exit "contexts" which help the parser determine what is and isn't valid.  For example, the global context allow different things than in the context of a routine.
        void openCompileContext(eCompileContext);   //entering a new context
        void closeCompileContext();                 //closing out the current context and returning to the previous
        eCompileContext getCurrentCompileContext(); //what is the the current context?
        
        int getScopeNestingDepth();                 //how deeply are out contexts nested?  We use this to indent the code we generate
        
        parseNode& getCurrentNode();                //as we are parsing the input file, we write nodes to the current node in the parseTree.  This points to the current node we are actively working on.
        
        void addTag(std::string name, std::string value);
        void clearTags();
        void tagCount();
        bool processTags(token tok);

    private:
        

        std::deque<eCompileLanguage> compileLanguageStack;   //of course, these aren't really stacks, but we use them that way
        std::deque<eCompileContext> compileContextStack;     

        //void emitTo(std::ostream&);
        //eCompileLanguage getCurrentLanguage(); 

        //old...
        //bool processNextStatement();
        //void processFunctionParams();
        //void processEnumOrFlags(token, bool);
        //void processFunctionCall(token, token=_nullToken);
        //bool getArgumentExpression(std::string&);
        //void processI6();
        
        //void registerNewBaseDataType(std::string);
        //objTypeDef registerNewObjectType(std::string);
        //void registerNewRoutine(std::string);
        
        //void emitVariable(token, token, token= _nullToken);

        //new...
        
        void pushCurrentNode(parseNode&);
        void popCurrentNode();
        parseNode& commitNode(parseNode&);
        
        bool processNextStatement();
        bool processDataType(token);
        bool processParameterList(memberFunction&);

        bool processClassDeclaration(token);
        bool processEnumDeclaration(token);
        bool processVariableDeclaration(token, token, token);
        bool processConstantDeclaration(token, token, token);
        
        bool processRoutineDeclaration(token, token);
        bool processObjectDeclaration(token, token);
        bool processStatement(token);
        bool processDirective(token);
        
};

extern bglParser parser;
