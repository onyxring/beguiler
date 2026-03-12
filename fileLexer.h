#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include "token.h"
#include "typeDef.h"

using namespace std;

class fileLexer{
    public:
        std::stack<std::tuple<std::ifstream*, std::string, int, int>> files;
        eTokenType prevTokenType = eTokenType::unknown; // tracks last token returned by getToken()
        
        void open(std::string);
        void close();
        int getNumberOfOpenFiles();
        void moveToStart();
        std::ifstream* currentStream();
        std::tuple<std::ifstream*, std::string, int, int> getCurrentFileDetail();
        void bleedSpaces();
        char peekChar();
        char readChar();
        bool isValidIdentifierChar(char c);

        token getBasicToken(bool=false);
        token getToken(eTokenType);
        token getToken(std::vector<eTokenType>);
        token getToken(std::string);
        token getToken(std::vector<std::string>);
        token getToken();
        token peekToken();
        token peekToken(int);
        
        string getRawTextThroughClosingBrace();
        sourceLocation currentLocation();
        //token getRunTokenEol(); 
        //token getRunTokenBraceClose(); 
};