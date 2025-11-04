#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <stack>
#include "token.h"

using namespace std;

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
        bool isValidIdentifierChar(char c);

        token getBasicToken(bool=false);
        token getToken(eTokenType);
        token getToken(std::vector<eTokenType>);
        token getToken(std::string);
        token getToken(std::vector<std::string>);
        token getToken();
};