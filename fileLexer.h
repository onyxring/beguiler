#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <stack>
#include "token.h"
#include "typeDef.h"

using namespace std;

// Beguile-region opener variants recognized by the raw-I6 scanner. Used in `.inf`-mode
// (file scope) and inside `#i6{}` blocks (statement scope) to dispatch from raw I6 back
// into Beguile parsing. `Bgl` is the predominant form; `BglDecl` and `BglStmt` are
// explicit escape hatches when the author wants to constrain content type.
enum class eBglDirective {
    NotFound,   // not a #bgl directive (or not yet matched)
    Bgl,        // #bgl{...} or #bgl ...;  — auto-detect declarations vs statements
    BglDecl,    // #bglDecl{...}           — declarations only
    BglStmt,    // #bglStmt{...}           — statements only
};

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
        // Pending doc-comment text accumulated by getToken() — attached to the next non-comment
        // token's docComment field. Cleared when consumed or when a blank line orphans it.
        std::string pendingDocComment;
        int         pendingDocLastLine = -1;  // last source line of the captured doc; -1 = none

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

        // Reads raw chars up to the matching `}`. The `isI6Content` flag controls
        // `!` handling: true means `!` starts an I6 line comment (correct for raw-I6
        // bodies — #i6, #emitfirst, #emitlast, #startup, raw emitter bodies, where
        // an `!` comment may legitimately contain `{` chars that shouldn't count).
        // False (the default) treats `!` as a regular character — correct for
        // Beguile bodies during pre-scan, where `!` is an operator (`!=`, `!flag`,
        // `!(expr)`, `!self.x`, etc.) and never starts a comment.
        string getRawTextThroughClosingBrace(bool isI6Content = false);
        // Same as above but stops also when `#bgl`, `#bglDecl`, or `#bglStmt` is encountered.
        // On return, outDirective indicates which variant was found (or None if the matching
        // closing brace was consumed). When a directive is found, the directive token (e.g.
        // `#bgl` / `#bglDecl` / `#bglStmt`) has been consumed; the caller switches to Beguile
        // parsing. outRemainingDepth carries the brace count back for resumption.
        // When eofTerminates is true, EOF is treated as a natural end-of-content rather than
        // an error — used for `.inf`-as-input mode where the whole file is one implicit raw-I6
        // region. outRemainingDepth is set to 0 on EOF in that mode.
        string getRawTextUntilCloseOrBgl(eBglDirective& outDirective, int& outRemainingDepth, int startDepth = 1, bool eofTerminates = false);
        sourceLocation currentLocation();
        //token getRunTokenEol();
        //token getRunTokenBraceClose();
};