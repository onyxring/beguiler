#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include "token.h"

class fileLexer{
    public:
        std::stack<std::tuple<std::ifstream*, std::string, int, int>> files; 
        
        void open(std::string);
        void close();
        int numOpen();
        void moveToStart();
        std::ifstream* currentStream();
        std::tuple<std::ifstream*, std::string, int, int> getDetail();
        void bleedSpaces();
        char peekChar();
        char readChar();
        bool isKeywordChar(char c);

        token getBasicToken(bool=false);
        token getBasicToken(eTokenType);
        token getBasicToken(std::vector<eTokenType>);
        token getBasicToken(std::string);
        token getBasicToken(std::vector<std::string>);
        token getToken(bool=false);
};