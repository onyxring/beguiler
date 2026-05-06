#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>

#include <map>
#include "helpers.h"
#include "bglLanguageService.h"

// Unicode codepoint → {ZSCII code, I6 accent string}
// Used to translate raw diacritical characters in source to I6-compatible output.
struct ZsciiEntry { int code; const char* i6Accent; };
static const map<uint32_t, ZsciiEntry> unicodeToZscii = {
    // diaeresis
    {0xE4, {155, "@:a"}}, {0xF6, {156, "@:o"}}, {0xFC, {157, "@:u"}},
    {0xC4, {158, "@:A"}}, {0xD6, {159, "@:O"}}, {0xDC, {160, "@:U"}},
    {0xEB, {164, "@:e"}}, {0xEF, {165, "@:i"}}, {0xFF, {166, "@:y"}},
    {0xCB, {167, "@:E"}}, {0xCF, {168, "@:I"}},
    // acute
    {0xE1, {169, "@'a"}}, {0xE9, {170, "@'e"}}, {0xED, {171, "@'i"}},
    {0xF3, {172, "@'o"}}, {0xFA, {173, "@'u"}}, {0xFD, {174, "@'y"}},
    {0xC1, {175, "@'A"}}, {0xC9, {176, "@'E"}}, {0xCD, {177, "@'I"}},
    {0xD3, {178, "@'O"}}, {0xDA, {179, "@'U"}}, {0xDD, {180, "@'Y"}},
    // grave
    {0xE0, {181, "@`a"}}, {0xE8, {182, "@`e"}}, {0xEC, {183, "@`i"}},
    {0xF2, {184, "@`o"}}, {0xF9, {185, "@`u"}},
    {0xC0, {186, "@`A"}}, {0xC8, {187, "@`E"}}, {0xCC, {188, "@`I"}},
    {0xD2, {189, "@`O"}}, {0xD9, {190, "@`U"}},
    // circumflex
    {0xE2, {191, "@^a"}}, {0xEA, {192, "@^e"}}, {0xEE, {193, "@^i"}},
    {0xF4, {194, "@^o"}}, {0xFB, {195, "@^u"}},
    {0xC2, {196, "@^A"}}, {0xCA, {197, "@^E"}}, {0xCE, {198, "@^I"}},
    {0xD4, {199, "@^O"}}, {0xDB, {200, "@^U"}},
    // angstrom / slashed o
    {0xE5, {201, "@oa"}}, {0xC5, {202, "@oA"}},
    {0xF8, {203, "@/o"}}, {0xD8, {204, "@/O"}},  // note: I6 uses @\o but we emit @/o
    // tilde
    {0xE3, {205, "@~a"}}, {0xF1, {206, "@~n"}}, {0xF5, {207, "@~o"}},
    {0xC3, {208, "@~A"}}, {0xD1, {209, "@~N"}}, {0xD5, {210, "@~O"}},
    // ligatures and special
    {0xE6, {211, "@ae"}}, {0xC6, {212, "@AE"}},
    {0xE7, {213, "@cc"}}, {0xC7, {214, "@cC"}},
    {0xFE, {215, "@th"}}, {0xDE, {217, "@Th"}},
    {0xF0, {216, "@et"}}, {0xD0, {218, "@Et"}},
    {0x153,{220, "@oe"}}, {0x152,{221, "@OE"}},  // œ Œ — outside Latin-1, 2-byte UTF-8
    {0xDF, {219, "@ss"}},  // ß
    // punctuation
    {0xA3, {222, "@LL"}},  // £
    {0xA1, {223, "@!!"}},  // ¡
    {0xBF, {224, "@??"}},  // ¿
};

// Decode a UTF-8 lead byte + continuation bytes into a Unicode codepoint.
// Returns the codepoint, consuming continuation bytes from the stream.
// If the byte is plain ASCII (< 0x80), returns it as-is.
// Falls back to Latin-1 interpretation if the next byte isn't a valid UTF-8 continuation.
static uint32_t decodeUtf8(unsigned char lead, function<char()> readNext, function<char()> peekNext){
    if(lead < 0x80) return lead;
    if((lead & 0xE0) == 0xC0){
        // 2-byte: 110xxxxx 10xxxxxx — verify continuation byte before consuming
        unsigned char next = (unsigned char)peekNext();
        if((next & 0xC0) != 0x80) return lead; // not valid UTF-8 continuation → treat as Latin-1
        readNext(); // consume the continuation byte
        return ((lead & 0x1F) << 6) | (next & 0x3F);
    }
    if((lead & 0xF0) == 0xE0){
        // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
        unsigned char c2 = (unsigned char)readNext();
        unsigned char c3 = (unsigned char)readNext();
        return ((lead & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    if((lead & 0xF8) == 0xF0){
        // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        unsigned char c2 = (unsigned char)readNext();
        unsigned char c3 = (unsigned char)readNext();
        unsigned char c4 = (unsigned char)readNext();
        return ((lead & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    }
    return lead; // single byte >= 0x80 → Latin-1 codepoint
}
#include "fileLexer.h"
#include "bglParser.h"
#include "beguiler.h"

using namespace std;

//----------------------------------------------------------------------------------------
//--Opening and closing files, and managing which of these is the "current file"
void fileLexer::open(string filename){
    ifstream* inputFileStream = new ifstream(filename);
    if (!inputFileStream->is_open()) {
        delete inputFileStream;
        parser.parsingError("Unable to open file "+filename+".");
    }
    files.push(make_tuple(static_cast<istream*>(inputFileStream), filename, 1, 0));
}
void fileLexer::openText(const std::string& content, const std::string& virtualName, int startLine){
    // Push a stringstream as a virtual file. Used to parse #bgl{} blocks extracted from
    // surrounding raw I6 — gives Beguile its normal token-fetching path over an in-memory buffer.
    istringstream* stream = new istringstream(content);
    files.push(make_tuple(static_cast<istream*>(stream), virtualName, startLine, 0));
}
void fileLexer::close(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top();
    // Virtual destructor on the istream-pointed-to object closes the file (ifstream) or
    // frees the buffer (istringstream); no explicit close() needed.
    delete inputFileStream;
    files.pop();
}
int fileLexer::getNumberOfOpenFiles(){
    return files.size();
}
void fileLexer::moveToStart(){
    currentStream()->seekg(0);
}
istream* fileLexer::currentStream(){
    auto[inputFileStream, fileName, curLine, curCol]=files.top();
    return inputFileStream;
}
tuple<istream*, string, int, int> fileLexer::getCurrentFileDetail(){ //return saved information about the current file
    return files.top();
}

//----------------------------------------------------------------------------------------
//--Reading and evaluating characters from current file
void fileLexer::bleedSpaces(){ // ignore all spaces before the next token
    char c=peekChar(); 
    while(c!=EOF && isspace(c)) { //this is just whitespace; discard it
        readChar(); 
        c=peekChar(); 
    }
}
char fileLexer::peekChar(){ //look at what the next character is, without actually pulling it off the stream
    return currentStream()->peek();
}
char fileLexer::readChar(){ //actually get the next character, removing it from the stream
    int retval;
    auto&[inputFileStream, fileName, curLine, curCol]=files.top(); 

    retval=(char)(inputFileStream->get());
    if(retval=='\n'){
        curLine++;
        curCol=0;
    }

    curCol++;
    return retval;
}

bool fileLexer::isValidIdentifierChar(char c){
    if(isalnum(c)) return true;
    if(c=='_') return true;
    return false;
}
/*----------------------------------------------------------------------------------------
    getBasicToken()

    This routine reads in one character at a time, constructing, then returning, the most 
    meaningful token than can be pulled from a stream without additional consideration.  
    All input can be translated into these tokens (see commends on getToken for more 
    information.    

        EOF             
        comment               
        quote    
        unclassifiedText
        operator 
        symbol

    Note: by default, all begining whitespace is ignored ("bled" off of the stream) and each call to getBasicToken()
    returns the first actual text token; however, if the suppressBleed argument is passed as true, and the first 
    encountered character is a whitespace, it will be returned as a symbol token.
*/
token fileLexer::getBasicToken(bool suppressBleed){
    token retval;    
    
    if(!suppressBleed) bleedSpaces();
    char c=peekChar(); 

    while(c!=EOF){
        if(retval.tokenType==eTokenType::unknown) {     /*  We don't yet know what sort of token we are building yet (this is probably  the first pass
                                                            through of our loop).  Let's determine the basic type of this token so we know how to process 
                                                            it in subsequent passes. */

            retval.value+=c;    //add the character we previewed (peeked at) to the value we are building
            readChar();         //dispose of it and move on to the next
            char nc=peekChar();                             
            string twoChars=retval.value + nc; /*   we do a lot of two character checks, so let's just build this string once and use it repeatedly; we will 
                                                    readChar() to dispose of the nc if we end up using it, but for now we just want to peek at it without consuming it*/
            
            if(twoChars=="//" || twoChars=="/*")
                retval.tokenType=eTokenType::comment;
            else if(c=='\"')
                retval.tokenType=eTokenType::quote;
            else if(c=='@' && nc=='"') {
                retval.tokenType=eTokenType::rawQuote;
                retval.value="\""; // adopt the same outer-quote format as regular quote tokens
                readChar();        // consume the '"'
            }
            else if(c == '$' && nc == '$'){
                // Possible binary literal: $$01010110
                readChar(); // consume second $
                char bc = peekChar();
                if(bc != '0' && bc != '1'){
                    // Not binary — put back as two $ symbols. Since we can't un-read,
                    // return first $ as symbol; second $ will be read next call.
                    retval.tokenType = eTokenType::symbol;
                    // nc ('$') is already consumed — we need to handle this.
                    // Simplest: treat "$$" without binary digits as a symbol token "$$"
                    retval.value = "$$";
                    break;
                }
                string bits;
                while(peekChar() == '0' || peekChar() == '1'){ bits += peekChar(); readChar(); }
                if(bits.empty()) parser.parsingError("Expected binary digits after '$$'");
                int val = 0;
                for(char b : bits) val = (val << 1) | (b - '0');
                retval.value = to_string(val);
                retval.tokenType = eTokenType::integer;
                break;
            }
            else if(c == '$' && isxdigit(nc)){
                // Hex literal: $FF — read hex digits, convert to decimal
                readChar(); // consume first hex digit
                string hex;
                hex += nc;
                while(isxdigit(peekChar())){ hex += peekChar(); readChar(); }
                int val = (int)strtol(hex.c_str(), nullptr, 16);
                retval.value = to_string(val);
                retval.tokenType = eTokenType::integer;
                break;
            }
            else if(isValidIdentifierChar(c))
                retval.tokenType=eTokenType::unclassifiedText;
            else {
                if(find(languageService.operators.begin(), languageService.operators.end(), retval.value)!=languageService.operators.end()){
                    retval.tokenType=eTokenType::oper; 
                }
                else if(find(languageService.operators.begin(), languageService.operators.end(), twoChars)!=languageService.operators.end()){
                    retval.value=twoChars; //note: this overwrites the previous value, which is just the first character
                    readChar(); //dispose of the second character we previewed
                    // Check for three-character operators (e.g. <<=, >>=) by peeking one more
                    char nc3 = peekChar();
                    string threeChars = twoChars + nc3;
                    if(find(languageService.operators.begin(), languageService.operators.end(), threeChars)!=languageService.operators.end()){
                        retval.value = threeChars;
                        readChar();
                    }
                    retval.tokenType=eTokenType::oper;
                }
                else{
                    retval.tokenType=eTokenType::symbol;
                }
                break;
            }
            c=peekChar(); //peek at the next character to process
            continue;
        }
        
        if(retval.tokenType==eTokenType::quote){
            readChar(); //dispose of the character we previewed

            if(c=='\\')  { //translate Beguile escape sequences to I6 equivalents
                c=peekChar();
                readChar();
                if     (c=='n')  retval.value+='^';          // \n  -> ^ (I6 newline)
                else if(c=='"')  retval.value+='~';          // \"  -> ~ (I6 double-quote)
                else if(c=='\\') retval.value+="@@92";        // \\  -> @@92 (literal backslash)
                else if(c=='@')  retval.value+="@@64";       // \@  -> @@64 (literal at-sign)
                else if(c=='$') {                             // \$XX -> @{XX} (hex character code)
                    string hex;
                    while(isxdigit(peekChar())) { hex += readChar(); }
                    retval.value += "@{" + hex + "}";
                }
                else if(isdigit(c)) {                         // \NNN -> @{hex} (decimal character code)
                    string dec; dec += c;
                    while(isdigit(peekChar())) { dec += readChar(); }
                    char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "%X", stoi(dec));
                    retval.value += "@{"; retval.value += hexBuf; retval.value += "}";
                }
                // ── Diacritical accent shorthands ──
                // Context-sensitive: \^vowel = circumflex, \^ alone = literal caret.
                // \^^ = forced literal caret, \~~ = forced literal tilde.
                else if(c=='^') {
                    char nc = peekChar();
                    if(nc == '^') { readChar(); retval.value += "@@94"; }       // \^^ -> literal caret (forced)
                    else if(string("aeiouyAEIOUY").find(nc) != string::npos) {
                        readChar(); retval.value += "@^"; retval.value += nc;   // \^a -> @^a (â)
                    } else { retval.value += "@@94"; }                          // \^  -> literal caret
                }
                else if(c=='~') {
                    char nc = peekChar();
                    if(nc == '~') { readChar(); retval.value += "@@126"; }      // \~~ -> literal tilde (forced)
                    else if(string("anoANO").find(nc) != string::npos) {
                        readChar(); retval.value += "@~"; retval.value += nc;   // \~n -> @~n (ñ)
                    } else { retval.value += "@@126"; }                         // \~  -> literal tilde
                }
                else if(c=='\'') {
                    char nc = peekChar();
                    if(string("aeiouyAEIOUY").find(nc) != string::npos) {
                        readChar(); retval.value += "@'"; retval.value += nc;  // \'e -> @'e (é)
                    } else { retval.value += '\''; }                           // \'  -> literal quote
                }
                else if(c=='`') {
                    char nc = peekChar();
                    if(string("aeiouyAEIOUY").find(nc) != string::npos) {
                        readChar(); retval.value += "@`"; retval.value += nc;  // \`a -> @`a (à)
                    } else { retval.value += '`'; }                            // \`  -> literal backtick
                }
                else if(c==':') {
                    char nc = peekChar();
                    if(string("aeiouyAEIOUY").find(nc) != string::npos) {
                        readChar(); retval.value += "@:"; retval.value += nc;  // \:u -> @:u (ü)
                    } else { retval.value += ':'; }                            // \:  -> literal colon
                }
                else if(c=='/') {
                    char nc = peekChar();
                    if(string("oO").find(nc) != string::npos) {
                        readChar(); retval.value += "@\\"; retval.value += nc; // \/o -> @\o (ø)
                    } else { retval.value += '/'; }                            // \/  -> literal slash
                }
                else if(c=='c') {
                    char nc = peekChar();
                    if(nc=='c' || nc=='C') {
                        readChar(); retval.value += "@c"; retval.value += nc;  // \cc -> @cc (ç), \cC -> @cC (Ç)
                    } else { retval.value += 'c'; }                            // \c  -> literal c
                }
                else if(c=='o') {
                    char nc = peekChar();
                    if(nc=='a' || nc=='A') {
                        readChar(); retval.value += "@o"; retval.value += nc;  // \oa -> @oa (å), \oA -> @oA (Å)
                    } else { retval.value += 'o'; }                            // \o  -> literal o
                }
                // Multi-char accent names: \ss \ae \AE \oe \OE \th \et \LL \!! \?? \<< \>>
                else if(c=='s' && peekChar()=='s') { readChar(); retval.value += "@ss"; }  // ß
                else if(c=='a' && peekChar()=='e') { readChar(); retval.value += "@ae"; }  // æ
                else if(c=='A' && peekChar()=='E') { readChar(); retval.value += "@AE"; }  // Æ
                else if(c=='O' && peekChar()=='E') { readChar(); retval.value += "@OE"; }  // Œ
                else if(c=='t' && peekChar()=='h') { readChar(); retval.value += "@th"; }  // þ
                else if(c=='T' && peekChar()=='H') { readChar(); retval.value += "@Th"; }  // Þ
                else if(c=='e' && peekChar()=='t') { readChar(); retval.value += "@et"; }  // ð
                else if(c=='E' && peekChar()=='T') { readChar(); retval.value += "@Et"; }  // Ð
                else if(c=='L' && peekChar()=='L') { readChar(); retval.value += "@LL"; }  // £
                else if(c=='!' && peekChar()=='!') { readChar(); retval.value += "@!!"; }  // ¡
                else if(c=='?' && peekChar()=='?') { readChar(); retval.value += "@??"; }  // ¿
                else if(c=='<' && peekChar()=='<') { readChar(); retval.value += "@<<"; }  // «
                else if(c=='>' && peekChar()=='>') { readChar(); retval.value += "@>>"; }  // »
                else           { retval.value+='\\'; retval.value+=c; } // unknown: pass through
            }
            else if((unsigned char)c >= 0x80){
                // Non-ASCII: decode UTF-8 (or Latin-1) and emit I6 accent notation
                uint32_t codepoint = decodeUtf8((unsigned char)c, [&](){ return readChar(); }, [&](){ return peekChar(); });
                auto it = unicodeToZscii.find(codepoint);
                if(it != unicodeToZscii.end())
                    retval.value += it->second.i6Accent;
                else {
                    char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "%04X", codepoint);
                    parser.parsingError(format("Unsupported Unicode character U+{0} in string literal", string(hexBuf)));
                }
            }
            else{
                retval.value+=c;
                if(c=='\"')  break; //closing quote
            }
            c=peekChar(); //peek at the next character to process
            continue;
        } 
        if(retval.tokenType==eTokenType::rawQuote){
            readChar(); // consume the peeked character
            // No Beguile escape processing — but translate I6-special chars so they stay literal
            if     (c=='"')   { retval.value+=c; break; }  // closing quote
            else if(c=='~')   retval.value+="@@126";        // literal tilde
            else if(c=='^')   retval.value+="@@94";         // literal caret
            else              retval.value+=c;
            c=peekChar();
            continue;
        }
        if(retval.tokenType==eTokenType::comment){
            retval.value+=c;
            readChar(); //dispose of the character we previewed    
            c=peekChar(); //peek at the next character to process

            if(retval.value[1]=='/' && c== '\n') break; //singleline 
            
            if(retval.value[1]=='*' && retval.value.length()>3 && retval.value[retval.value.length()-1]=='*' && c=='/'){ //multiline
                retval.value+=c;
                readChar();
                break;
            } 
            continue;  
        }
        
        //we are building a token from contiguous keyword characters
        if(!isValidIdentifierChar(c)) break;  //break at the first non-keyword character
        retval.value+=c;
        
        readChar();  //dispose of the character we previewed
        c=peekChar(); //peek at the next character to process
    }

    //a special case: we've reached the end of file.  If we are not at the global scope, then throw an error, because we've terminated early
    if(c==EOF) {
        // Sub-parses driven by openText (e.g. #bgl{} content lifted out of an #i6 raw block)
        // legitimately end at EOF inside whatever compile context the caller is in.
        // looseIdentifierMode is only set during those sub-parses, so use it as the signal
        // that hitting EOF is benign rather than premature.
        if(parser.getCurrentCompileContext()!=eCompileContext::global && !parser.looseIdentifierMode)
            parser.parsingError("End of file encountered prematurely");
        retval.tokenType=eTokenType::eof;
    }

    return retval;//return our completed basic token
}
string fileLexer::getRawTextThroughClosingBrace(){
    string retval;
    int count=1; //we have already encountered the first open brace, which is why we are calling this function, so we start our count at 1
    char c=readChar();

     while(count>0){
         if(c == EOF){
             parser.parsingError("Unexpected end of file — missing closing '}'");
             break;
         }
         // Skip // line comments — braces inside don't count
         if(c=='/' && peekChar()=='/'){
             retval += c; c = readChar();  // second /
             retval += c; c = readChar();
             while(c != '\n' && c != EOF){
                 retval += c;
                 c = readChar();
             }
             if(c == '\n'){ retval += c; c = readChar(); }
             continue;
         }
         // Skip /* … */ block comments — same reasoning as // and !: braces inside
         // don't count, and (critically) apostrophes inside ("Edaw's") would otherwise
         // open a runaway char-literal skip that swallows braces far below.
         if(c=='/' && peekChar()=='*'){
             retval += c; c = readChar();  // consume first '/'
             retval += c; c = readChar();  // consume the '*'
             while(c != EOF){
                 if(c == '*' && peekChar() == '/'){
                     retval += c; c = readChar();  // consume the '*'
                     retval += c; c = readChar();  // consume the closing '/'
                     break;
                 }
                 retval += c; c = readChar();
             }
             continue;
         }
         // Skip I6 ! line comments — same reasoning. Critical: English contractions
         // like "don't" inside ! comments would otherwise open a runaway char-literal
         // skip that swallows braces in unrelated code far below.
         if(c == '!'){
             while(c != '\n' && c != EOF){ retval += c; c = readChar(); }
             if(c == '\n'){ retval += c; c = readChar(); }
             continue;
         }
         // Skip string literals — braces inside don't count. Raw I6 uses ^/~/@ for special
         // chars rather than C-style backslash escapes, so we terminate at the next "
         // without interpreting \" as an escaped quote.
         if(c=='"'){
             retval += c; c = readChar();
             while(c != '"' && c != EOF){
                 retval += c; c = readChar();
             }
             if(c == '"'){ retval += c; c = readChar(); }
             continue;
         }
         // Skip character/dict-word literals — same reasoning: I6 doesn't C-escape, so '\'
         // is a 3-char literal that terminates at the second '.
         if(c=='\''){
             retval += c; c = readChar();
             while(c != '\'' && c != EOF){
                 retval += c; c = readChar();
             }
             if(c == '\''){ retval += c; c = readChar(); }
             continue;
         }

         if(c=='}') count--;
         if(count==0) break; //don't include the closing brace in the text we return
         if(c=='{') count++;

         retval=retval+c;
         c=readChar();
     }
     // The caller had already consumed the opening '{' via getToken(), which incremented
     // braceDepth. We've now consumed the matching '}' here as raw char reads, so decrement to
     // keep the counter balanced for LSP error recovery.
     if(braceDepth > 0) braceDepth--;
     return retval;
}

// Variant that also stops on a `#bgl{` marker so the caller can switch to Beguile parsing.
// Same comment/string/char-literal awareness as getRawTextThroughClosingBrace. On return:
//   outFoundBgl = true   → consumed `#bgl{`; outRemainingDepth unchanged for caller.
//   outFoundBgl = false  → consumed matching `}`; outRemainingDepth = 0.
// The caller is responsible for re-entering the raw-skip after the embedded #bgl block
// closes, with the same outRemainingDepth.
string fileLexer::getRawTextUntilCloseOrBgl(eBglDirective& outDirective, int& outRemainingDepth, int startDepth, bool eofTerminates){
    string retval;
    int count = startDepth;
    outDirective = eBglDirective::NotFound;
    char c = readChar();
    while(count > 0){
        if(c == EOF){
            if(eofTerminates){
                // .inf-as-input mode: EOF is the natural end of the implicit raw-I6 region.
                count = 0;
                break;
            }
            parser.parsingError("Unexpected end of file — missing closing '}'");
            break;
        }
        // Line comments
        if(c == '/' && peekChar() == '/'){
            retval += c; c = readChar();
            retval += c; c = readChar();
            while(c != '\n' && c != EOF){ retval += c; c = readChar(); }
            if(c == '\n'){ retval += c; c = readChar(); }
            continue;
        }
        // /* … */ block comments — same reasoning as // and !: braces inside don't
        // count, and apostrophes inside ("Edaw's") would otherwise open a runaway
        // char-literal skip that swallows braces far below.
        if(c == '/' && peekChar() == '*'){
            retval += c; c = readChar();
            retval += c; c = readChar();
            while(c != EOF){
                if(c == '*' && peekChar() == '/'){
                    retval += c; c = readChar();
                    retval += c; c = readChar();
                    break;
                }
                retval += c; c = readChar();
            }
            continue;
        }
        // I6 ! line comments — must be skipped so contractions like "don't" inside
        // a comment don't open a stray char-literal scan.
        if(c == '!'){
            while(c != '\n' && c != EOF){ retval += c; c = readChar(); }
            if(c == '\n'){ retval += c; c = readChar(); }
            continue;
        }
        // String literals — terminate at next " (no C-escape interpretation; I6 uses ^/~/@)
        if(c == '"'){
            retval += c; c = readChar();
            while(c != '"' && c != EOF){ retval += c; c = readChar(); }
            if(c == '"'){ retval += c; c = readChar(); }
            continue;
        }
        // Char/dict-word literals
        if(c == '\''){
            retval += c; c = readChar();
            while(c != '\'' && c != EOF){ retval += c; c = readChar(); }
            if(c == '\''){ retval += c; c = readChar(); }
            continue;
        }
        // #bgl / #bglDecl / #bglStmt marker — only ASCII letters follow; none are line
        // breaks, so we can roll back via tellg/seekg without disturbing curLine accounting.
        if(c == '#'){
            auto savedPos = currentStream()->tellg();
            int savedCol;
            { auto& [s, n, ln, cc] = files.top(); (void)s; (void)n; (void)ln; savedCol = cc; }

            auto rewindTo = [&](std::streampos pos, int col){
                currentStream()->clear();
                currentStream()->seekg(pos);
                auto& [s, n, ln, cc] = files.top(); (void)s; (void)n; (void)ln; cc = col;
            };

            // Match "bgl" first; if matched and the boundary is alphabetic, try the
            // recognized suffixes "Decl" and "Stmt" with a fresh boundary check.
            bool bglMatched = true;
            for(int i = 0; i < 3; i++){
                char nc = readChar();
                if(nc == EOF || nc != "bgl"[i]){ bglMatched = false; break; }
            }
            eBglDirective which = eBglDirective::NotFound;
            if(bglMatched){
                char boundary = peekChar();
                if(boundary != EOF && isalpha((unsigned char)boundary)){
                    // Could be #bglDecl or #bglStmt. Try each suffix; rewind between attempts.
                    auto afterBglPos = currentStream()->tellg();
                    int afterBglCol;
                    { auto& [s, n, ln, cc] = files.top(); (void)s; (void)n; (void)ln; afterBglCol = cc; }

                    struct { const char* suffix; eBglDirective dir; } candidates[] = {
                        { "Decl", eBglDirective::BglDecl },
                        { "Stmt", eBglDirective::BglStmt },
                    };
                    for(auto& cand : candidates){
                        bool sMatched = true;
                        for(int j = 0; cand.suffix[j]; j++){
                            char nc = readChar();
                            if(nc == EOF || nc != cand.suffix[j]){ sMatched = false; break; }
                        }
                        if(sMatched){
                            char b = peekChar();
                            if(b != EOF && (isalnum((unsigned char)b) || b == '_'))
                                sMatched = false;
                        }
                        if(sMatched){ which = cand.dir; break; }
                        rewindTo(afterBglPos, afterBglCol);
                    }
                    // No recognized suffix — `#bglFoo` is not a Beguile directive opener.
                } else if(boundary != EOF && (isdigit((unsigned char)boundary) || boundary == '_')){
                    // `#bgl0`, `#bgl_x`, etc. — not a Beguile directive opener.
                } else {
                    // Plain word boundary after "bgl" (whitespace, '{', EOF, punctuation).
                    which = eBglDirective::Bgl;
                }
            }

            if(which != eBglDirective::NotFound){
                outDirective = which;
                outRemainingDepth = count;
                return retval;
            }
            // Mismatch — rewind to position right after '#' and treat '#' as a regular char.
            rewindTo(savedPos, savedCol);
            // c is still '#'; fall through to normal handling below.
        }

        if(c == '}') count--;
        if(count == 0) break;
        if(c == '{') count++;
        retval += c;
        c = readChar();
    }
    if(outDirective == eBglDirective::NotFound){
        if(braceDepth > 0) braceDepth--;
        outRemainingDepth = 0;
    }
    return retval;
}

sourceLocation fileLexer::currentLocation(){
    if(files.empty()) return {};
    auto [stream, fileName, curLine, curCol] = files.top();
    return {fileName, curLine};
}

// token fileLexer::getRunTokenEol(){
//     token retval;
//     retval.tokenType=eTokenType::unclassifiedText;
    
//     char c=peekChar(); 

//     while(c!=EOF&&c != '\n'){
//         retval.value+=c; 
//         readChar();      
//     }
//     return retval;
// }
// token fileLexer::getRunTokenBraceClose(){
//     token retval;
//     retval.tokenType=eTokenType::unclassifiedText;
    
//     char c=peekChar(); 
//     while(c!=EOF){
//         &&c != '}'){
//         retval.value+=c; 
//         readChar();      
//     }
//     return retval;
// } 

/*----------------------------------------------------------------------------------------
    getToken()

    Return a token from the stream. This calls getBasicToken, and tries to classify it further.  After this runs, the token 
    can be any of the following: 

    EOF:                The next token encountered was the end of file.    
    quote:              The next token encountered was a quoted string.  Text contains the entirety of the string, 
                        including the quotes.
    dataType:           The next token is a valid data type.
    identifier:         The next token is a valid identifier, but not a data type.
    integer:            The next token is an integer number.
    directive:          The next token encountered was a compiler directive (e.g., #include).
    operator:           The next token encountered is a recognized language operator.
    symbol:             The next token encountered is a single symbol which was not matched to an operator.
                        Note: multiple symbols may appear contiguously in the file, but each will be 
                        returned as separate tokens.
                        Note: comments and text strings are not returned as symbols, even though they both start
                        with symbols or symbol pairs.
    unclassifiedText:   The next token encountered was a series of contiguous "identifier appropriate" characters, 
                        including the underscore and alphanumeric characters; however, it could not be further classified
                        into another token type.  Usually, this indicates an error condition.
    
    Note: comment tokens are discarded, whether single line or multiline comments 
*/
token fileLexer::getToken(){
    token next;
    token retval;
    
    // Doc-comment capture (`///` line form, `/** */` block form):
    // Comments are normally discarded, but doc-comments are accumulated into `pendingDocComment`
    // and attached to the next non-comment token. A blank line between accumulated docs and the
    // next non-doc-comment input orphans the buffer (it becomes free-floating narrative).
    auto isDocLineForm   = [](const std::string& v){ return v.size() >= 3 && v[0]=='/' && v[1]=='/' && v[2]=='/'; };
    auto isDocBlockForm  = [](const std::string& v){
        // /** ... */ — two leading stars, but distinguish from /** / and from regular /* */
        return v.size() >= 4 && v[0]=='/' && v[1]=='*' && v[2]=='*' && v[3] != '/';
    };
    auto extractDocText  = [&](const std::string& v) -> std::string {
        if(isDocLineForm(v)){
            // Strip leading `///` and at most one space; drop trailing newline if present
            size_t s = 3;
            if(s < v.size() && v[s] == ' ') s++;
            std::string body = v.substr(s);
            while(!body.empty() && (body.back()=='\n' || body.back()=='\r')) body.pop_back();
            return body;
        }
        if(isDocBlockForm(v)){
            // Strip `/**` and trailing `*/`. Then for each line, trim leading whitespace + leading `*` + one space.
            std::string inner = v.substr(3);
            if(inner.size() >= 2 && inner[inner.size()-2]=='*' && inner[inner.size()-1]=='/')
                inner = inner.substr(0, inner.size()-2);
            std::string out;
            size_t pos = 0;
            bool first = true;
            while(pos <= inner.size()){
                size_t nl = inner.find('\n', pos);
                std::string line = (nl == std::string::npos) ? inner.substr(pos) : inner.substr(pos, nl - pos);
                // Trim leading whitespace
                size_t a = 0;
                while(a < line.size() && (line[a]==' '||line[a]=='\t')) a++;
                // Strip a single leading `*` followed by optional space
                if(a < line.size() && line[a] == '*'){ a++; if(a < line.size() && line[a]==' ') a++; }
                std::string trimmed = line.substr(a);
                // Trim trailing whitespace
                while(!trimmed.empty() && (trimmed.back()==' '||trimmed.back()=='\t'||trimmed.back()=='\r')) trimmed.pop_back();
                if(first) { out = trimmed; first = false; }
                else      { out += "\n"; out += trimmed; }
                if(nl == std::string::npos) break;
                pos = nl + 1;
            }
            // Trim leading/trailing blank lines
            while(!out.empty() && out.front()=='\n') out.erase(0,1);
            while(!out.empty() && out.back()=='\n')  out.pop_back();
            return out;
        }
        return "";
    };

    //a special case: let's ignore all comment tokens (but capture doc-comments into pendingDocComment)
    do{
        retval=getBasicToken();
        if(retval.tokenType==eTokenType::comment){
            bool isDoc = isDocLineForm(retval.value) || isDocBlockForm(retval.value);
            if(isDoc){
                int lineNow = retval.src.line;
                // If a previous doc was captured but this new doc is more than one line later,
                // discard the prior buffer (blank line orphaned it).
                if(!pendingDocComment.empty() && pendingDocLastLine >= 0 && lineNow > pendingDocLastLine + 1){
                    pendingDocComment.clear();
                }
                std::string text = extractDocText(retval.value);
                if(!pendingDocComment.empty()) pendingDocComment += "\n";
                pendingDocComment += text;
                pendingDocLastLine = lineNow;
            }
            // Either way, fall through and read the next basic token (comments are not returned)
        }
    }while(retval.tokenType==eTokenType::comment);

    // If pending doc comment exists, check the blank-line orphan rule against this token.
    // If the token starts more than one line after the doc's last line, the doc is orphaned.
    if(!pendingDocComment.empty()){
        if(pendingDocLastLine >= 0 && retval.src.line > pendingDocLastLine + 1){
            pendingDocComment.clear();
            pendingDocLastLine = -1;
        } else {
            retval.docComment = pendingDocComment;
            pendingDocComment.clear();
            pendingDocLastLine = -1;
        }
    }

    if(retval.isOneOf({eTokenType::eof, eTokenType::quote, eTokenType::rawQuote, eTokenType::charLiteral})){ prevTokenType = retval.tokenType; return retval; } //just return tokens which we know we can't expand

    // normalize to lowercase for case-insensitive parsing (string literals excluded above)
    retval.originalValue = retval.value; // save pre-lowercase value for case-sensitive I6 emission
    transform(retval.value.begin(), retval.value.end(), retval.value.begin(), ::tolower);

    //we have our basic token, but let's try to classify it a little more specifically, possibly grabbing additional basic tokens to complete more complex ones
    if(retval.is("#")){
        if(peekChar() == '#'){
            // ## prefix — Beguile compile-time directive (evaluated by the transpiler, not passed to I6)
            readChar(); // consume second '#'
            next=getBasicToken(true);
            if(!next.isValidIdentifier()) parser.parsingError("Encountered invalid Beguile directive name '"+next.value+"'.");
            transform(next.value.begin(), next.value.end(), next.value.begin(), ::tolower);
            retval.value = "##" + next.value;
            retval.tokenType=eTokenType::directive;
            prevTokenType = eTokenType::directive;
            retval.src = currentLocation();
            return retval;
        }
        next=getBasicToken(true); //to make sense, this MUST be a name directly connected to the # with no whitespaces in between
        if(!next.isValidIdentifier()) parser.parsingError("Encountered invalid directive name '"+next.value+"'.");
        transform(next.value.begin(), next.value.end(), next.value.begin(), ::tolower);
        retval.value+=next.value;
        retval.tokenType=eTokenType::directive;
        prevTokenType = eTokenType::directive;
        retval.src = currentLocation();
        return retval;
    }

    // Character literals: 'x'  (single character, with escape support)
    // Supports: \n \' \\ \NNN (ZSCII numeric) and diacritical accents:
    //   \^a \:u \'e \`a \~n \/o \cc \oa \ae \AE \OE \oe \th \et
    if(retval.is("'")){
        char c = peekChar(); readChar();
        string charVal;
        if(c == '\\'){
            c = peekChar(); readChar();
            if     (c == 'n')  charVal += '^';   // \n -> ^ (I6 newline)
            else if(c == '\\') charVal += '\\';  // \\ -> backslash
            else if(c == '$'){
                // Hex escape: \$XX — character code in hexadecimal; emit as raw integer
                string hex;
                while(isxdigit(peekChar())){ hex += peekChar(); readChar(); }
                if(hex.empty()) parser.parsingError("Expected hex digits after \\$ in character literal");
                charVal += to_string(stoi(hex, nullptr, 16));
            }
            else if(isdigit(c)){
                // Numeric escape: \NNN — ZSCII character code; emit as raw integer for I6 expressions
                charVal += c;
                while(isdigit(peekChar())){ charVal += peekChar(); readChar(); }
            }
            // ── Diacritical accent shorthands → ZSCII numeric codes ──
            // Char literals are used in I6 expressions where @:a syntax is invalid;
            // convert to numeric ZSCII codes so they emit as bare integers.
            // Note: \' for acute accent is checked BEFORE literal quote fallback.
            else if(c == '\'' && string("aeiouy").find(peekChar()) != string::npos) {
                // acute lowercase: á169 é170 í171 ó172 ú173 ý174
                static map<char,int> acute = {{'a',169},{'e',170},{'i',171},{'o',172},{'u',173},{'y',174}};
                charVal += to_string(acute[readChar()]);
            }
            else if(c == '\'' && string("AEIOUY").find(peekChar()) != string::npos) {
                // acute uppercase: Á175 É176 Í177 Ó178 Ú179 Ý180
                static map<char,int> acute = {{'A',175},{'E',176},{'I',177},{'O',178},{'U',179},{'Y',180}};
                charVal += to_string(acute[readChar()]);
            }
            else if(c == '\'') charVal += '\'';  // \' -> literal quote (only if not followed by vowel)
            else if(c == ':' && string("aeiouy").find(peekChar()) != string::npos) {
                // diaeresis lowercase: ä155 ö156 ü157 ë164 ï165 ÿ166
                static map<char,int> diaer = {{'a',155},{'o',156},{'u',157},{'e',164},{'i',165},{'y',166}};
                charVal += to_string(diaer[readChar()]);
            }
            else if(c == ':' && string("AEIOUY").find(peekChar()) != string::npos) {
                // diaeresis uppercase: Ä158 Ö159 Ü160 Ë167 Ï168
                static map<char,int> diaer = {{'A',158},{'O',159},{'U',160},{'E',167},{'I',168}};
                charVal += to_string(diaer[readChar()]);
            }
            else if(c == '^' && string("aeiou").find(peekChar()) != string::npos) {
                // circumflex lowercase: â191 ê192 î193 ô194 û195
                static map<char,int> circ = {{'a',191},{'e',192},{'i',193},{'o',194},{'u',195}};
                charVal += to_string(circ[readChar()]);
            }
            else if(c == '^' && string("AEIOU").find(peekChar()) != string::npos) {
                // circumflex uppercase: Â196 Ê197 Î198 Ô199 Û200
                static map<char,int> circ = {{'A',196},{'E',197},{'I',198},{'O',199},{'U',200}};
                charVal += to_string(circ[readChar()]);
            }
            else if(c == '`' && string("aeiou").find(peekChar()) != string::npos) {
                // grave lowercase: à181 è182 ì183 ò184 ù185
                static map<char,int> grave = {{'a',181},{'e',182},{'i',183},{'o',184},{'u',185}};
                charVal += to_string(grave[readChar()]);
            }
            else if(c == '`' && string("AEIOU").find(peekChar()) != string::npos) {
                // grave uppercase: À186 È187 Ì188 Ò189 Ù190
                static map<char,int> grave = {{'A',186},{'E',187},{'I',188},{'O',189},{'U',190}};
                charVal += to_string(grave[readChar()]);
            }
            else if(c == '~' && string("ano").find(peekChar()) != string::npos) {
                // tilde lowercase: ã205 ñ206 õ207
                static map<char,int> tild = {{'a',205},{'n',206},{'o',207}};
                charVal += to_string(tild[readChar()]);
            }
            else if(c == '~' && string("ANO").find(peekChar()) != string::npos) {
                // tilde uppercase: Ã208 Ñ209 Õ210
                static map<char,int> tild = {{'A',208},{'N',209},{'O',210}};
                charVal += to_string(tild[readChar()]);
            }
            else if(c == '/' && peekChar()=='o') { readChar(); charVal += "203"; }  // ø
            else if(c == '/' && peekChar()=='O') { readChar(); charVal += "204"; }  // Ø
            else if(c == 'c' && peekChar()=='c') { readChar(); charVal += "213"; }  // ç
            else if(c == 'c' && peekChar()=='C') { readChar(); charVal += "214"; }  // Ç
            else if(c == 'o' && peekChar()=='a') { readChar(); charVal += "201"; }  // å
            else if(c == 'o' && peekChar()=='A') { readChar(); charVal += "202"; }  // Å
            else if(c == 'a' && peekChar()=='e') { readChar(); charVal += "211"; }  // æ
            else if(c == 'A' && peekChar()=='E') { readChar(); charVal += "212"; }  // Æ
            else if(c == 'O' && peekChar()=='E') { readChar(); charVal += "221"; }  // Œ
            else if(c == 'o' && peekChar()=='e') { readChar(); charVal += "220"; }  // œ
            else if(c == 't' && peekChar()=='h') { readChar(); charVal += "215"; }  // þ
            else if(c == 'T' && peekChar()=='H') { readChar(); charVal += "217"; }  // Þ
            else if(c == 'e' && peekChar()=='t') { readChar(); charVal += "216"; }  // ð
            else if(c == 'E' && peekChar()=='T') { readChar(); charVal += "218"; }  // Ð
            else             { charVal += '\\'; charVal += c; } // unknown: pass through
        } else if((unsigned char)c >= 0x80){
            // Non-ASCII: decode UTF-8 (or Latin-1) and look up ZSCII code
            uint32_t codepoint = decodeUtf8((unsigned char)c, [&](){ return readChar(); }, [&](){ return peekChar(); });
            auto it = unicodeToZscii.find(codepoint);
            if(it != unicodeToZscii.end())
                charVal += to_string(it->second.code);
            else {
                char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "%04X", codepoint);
                parser.parsingError(format("Unsupported Unicode character U+{0} in character literal", string(hexBuf)));
            }
        } else {
            charVal += c;
        }
        // consume closing '
        if(peekChar() == '\'') readChar();
        retval.value = charVal;
        retval.tokenType = eTokenType::charLiteral;
        prevTokenType = eTokenType::charLiteral;
        return retval;
    }

    // Dictionary word literals: .word (singular) and ..word (plural)
    // Not recognised after an identifier, data type, or closing paren/bracket (dot-access on expression).
    bool prevIsExprEnd = (prevTokenType == eTokenType::identifier || prevTokenType == eTokenType::dataType
                          || prevTokenType == eTokenType::quote || prevTokenType == eTokenType::rawQuote
                          || prevTokenType == eTokenType::integer || prevTokenType == eTokenType::charLiteral
                          || prevTokenType == eTokenType::directive
                          || (prevTokenType == eTokenType::symbol && (prevTokenValue == ")" || prevTokenValue == "]")));
    if(retval.is(".") && !prevIsExprEnd){
        char c1 = peekChar();
        // Helper lambda: read dict-word chars (identifier chars + apostrophe).
        // A trailing '.' triggers a warning and is discarded.
        auto readDictWord = [&]() -> string {
            string w;
            char ch = peekChar();
            while(isValidIdentifierChar(ch) || ch == '\'' || (unsigned char)ch >= 0x80){
                if((unsigned char)ch >= 0x80){
                    // Unicode diacritical in dictionary word — decode and emit I6 accent notation
                    readChar();
                    uint32_t codepoint = decodeUtf8((unsigned char)ch, [&](){ return readChar(); }, [&](){ return peekChar(); });
                    auto it = unicodeToZscii.find(codepoint);
                    if(it != unicodeToZscii.end())
                        w += it->second.i6Accent;
                    else
                        w += ch; // unrecognized — pass through
                } else {
                    w += (char)tolower(ch);
                    readChar();
                }
                ch = peekChar();
            }
            if(ch == '.'){
                auto loc = currentLocation();
                cerr << loc.file << ":" << loc.line << ": warning: character '.' is not valid for dictionary words. ignoring.\n";
                readChar(); // discard the period
            }
            return w;
        };

        if(c1 == '.'){
            auto savepos = currentStream()->tellg();
            readChar(); // consume the second '.'
            char c2 = peekChar();
            if(isalpha(c2)){
                retval.value = readDictWord();
                retval.tokenType = eTokenType::dictionaryWord;
                retval.isPlural = true;
                prevTokenType = eTokenType::dictionaryWord;
                return retval;
            }
            currentStream()->seekg(savepos); // not a plural dict word; restore stream
        } else if(isalpha(c1)){
            retval.value = readDictWord();
            retval.tokenType = eTokenType::dictionaryWord;
            retval.isPlural = false;
            prevTokenType = eTokenType::dictionaryWord;
            return retval;
        }
    }
    
    // Reject C-style hex literals explicitly. A token that starts with "0x" or "0X" is almost
    // certainly an attempt to write a hex literal in C notation; Beguile uses the I6 convention
    // `$XX` instead. Catching it here gives a clear error instead of letting the mangled token
    // leak into the AST and then into the emitted I6 (where I6 would reject it with a cryptic
    // "applied to undeclared variable" style error).
    if(retval.value.size() >= 3 && retval.value[0] == '0' &&
       (retval.value[1] == 'x' || retval.value[1] == 'X')){
        parser.parsingError(format("C-style hex literal '{0}' is not supported; use '${1}' for hex literals",
            retval.value, retval.value.substr(2)));
    }
    if(retval.isValidIdentifier()) retval.tokenType=eTokenType::identifier; //if it meets the identifier format, default classification an identifier; however,
                                                                            //  it may NOT be and could change below; other tokenTypes also meet the algorithmic
                                                                            //  rules for valid identifiers (data types for example)
    if(retval.isNumeric())retval.tokenType=eTokenType::integer;
    if(retval.isDataType()) retval.tokenType=eTokenType::dataType; //this would replace the previous identifier assumption
    if(retval.tokenType==eTokenType::symbol && retval.isOneOf({"=","+","-","*","/","%","<",">","!","&","|","^"})){
        retval.tokenType=eTokenType::oper;
    }

    prevTokenType = retval.tokenType;
    prevTokenValue = retval.value;
    retval.src = currentLocation();
    // Track brace depth for LSP error recovery — counts '{' and '}' tokens as they're emitted.
    if(retval.is(token::braceOpen))  braceDepth++;
    else if(retval.is(token::braceClose) && braceDepth > 0) braceDepth--;
    return retval;
}
token fileLexer::peekToken(){
    return peekToken(1);
}
token fileLexer::peekToken(int tokNum){
    token retval;
    auto savepos=currentStream()->tellg();
    eTokenType savedPrev = prevTokenType;
    string savedPrevValue = prevTokenValue;
    int savedBraceDepth = braceDepth;
    // Save line/col from the current file's tuple (stream seek doesn't reset these)
    auto& [pStream, pName, pLine, pCol] = files.top();
    int saveLine = pLine;
    int saveCol  = pCol;
    for(int i=0;i<tokNum;i++){
        retval=getToken();
    }
    currentStream()->seekg(savepos);
    prevTokenType = savedPrev;
    prevTokenValue = savedPrevValue;
    braceDepth = savedBraceDepth;
    pLine = saveLine;
    pCol  = saveCol;
    return retval;
}
//Get tokens, limited to specific types or values. Throw a compile-time error if the next token does not match the requirements.
//These are used when the language absolutely requires the next token to conform to a specific set of features.
token fileLexer::getToken(eTokenType tokenType){
    token retval=getToken();
    return retval.assertOneOf({tokenType}); 
}
token fileLexer::getToken(vector<eTokenType> types){
    token retval=getToken();
    return retval.assertOneOf(types); 
}
token fileLexer::getToken(string val){
    token retval=getToken();
    return retval.assertOneOf({val}); 
}
token fileLexer::getToken(std::vector<string> vals){
    token retval=getToken();
    return retval.assertOneOf(vals); 
}

