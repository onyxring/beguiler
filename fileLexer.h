#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <stack>
#include "token.h"
#include "typeDef.h"

using namespace std;

class fileLexer{
    public:
        // Stream pointer is istream so the stack can hold both ifstream (file-backed) and
        // istringstream (in-memory; used by openText() to parse #bgl{} blocks lifted from
        // raw I6 contexts). Memory: deleting the istream* invokes the derived destructor
        // (virtual), which closes the file for ifstream / frees the buffer for istringstream.
        std::stack<std::tuple<std::istream*, std::string, int, int>> files;
        eTokenType prevTokenType = eTokenType::unknown; // tracks last token returned by getToken()
        std::string prevTokenValue;                      // tracks last token's value (for symbol disambiguation)
        // Running count of '{' minus '}' tokens delivered through getToken() (and consumed by
        // getRawTextThroughClosingBrace). Used by LSP error recovery to unwind nested blocks
        // correctly when an exception fires from deeply inside a class/function body.
        int braceDepth = 0;

        void open(std::string);
        void openText(const std::string& content, const std::string& virtualName, int startLine = 1);
        void close();
        int getNumberOfOpenFiles();
        void moveToStart();
        std::istream* currentStream();
        std::tuple<std::istream*, std::string, int, int> getCurrentFileDetail();
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
        // Same as above but stops also when `#bgl{` is encountered. On return, outFoundBgl
        // is true if we stopped at #bgl{ (the marker is consumed; caller switches to Beguile
        // parsing). Otherwise the matching closing brace was consumed and outFoundBgl=false.
        // outRemainingDepth carries the brace count back to the caller for resumption.
        // When eofTerminates is true, EOF is treated as a natural end-of-content rather than
        // an error — used for `.inf`-as-input mode where the whole file is one implicit raw-I6
        // region. outRemainingDepth is set to 0 on EOF in that mode.
        string getRawTextUntilCloseOrBgl(bool& outFoundBgl, int& outRemainingDepth, int startDepth = 1, bool eofTerminates = false);
        sourceLocation currentLocation();
        //token getRunTokenEol();
        //token getRunTokenBraceClose();
};