#pragma once
#include <string>
#include <sstream>
#include <stack>
#include "token.h"

class fileReader{
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

        token getToken(bool=false);
        token getToken(eTokenType);
        token getToken(std::vector<eTokenType>);
        token getToken(std::string);
        token getToken(std::vector<std::string>);
};