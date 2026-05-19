// ═══════════════════════════════════════════════════════════════════════════════
// bglParser.cpp — Beguile language parser
//
// Two-pass compilation:
//   Pass 1 (preScanFile): registers type/class/object/function stubs so that
//     forward references resolve. Stubs have isPrePassStub=true and are replaced
//     during pass 2 via replaceStubMember().
//   Pass 2 (parseFile → processNextStatement): full parse, type checking, AST
//     construction. Statements are dispatched through processStatement() which
//     handles keywords (if/for/while/switch/return), assignments, function calls,
//     and compound operators.
//
// Key entry points:
//   preScanFile()       — pass 1 stub registration
//   parseFile()         — pass 2 main loop
//   parseExpression()   — expression parser (900+ lines, section-commented)
//   processStatement()  — statement-level dispatch
//
// Shared helpers (extracted to reduce duplication):
//   resolveMethod()         — unified method lookup across class hierarchy + objectDef members
//   replaceStubMember()     — replace pre-scan stub with real definition
//   applyBinaryOperator()   — binary operator emitter resolution + RHS parsing
//   findMemberInHierarchy() — recursive class member search
//   isTypeCompatible()      — type compatibility check with operator/conversion fallbacks
//   evaluateCondition()     — #if/#elif compile-time condition evaluator
//
// parseExpression() branch map (search for these section markers):
//   PARENS          — open/close paren, lambda detection, cast prefix
//   LITERALS        — int, string, char, dictionary word
//   IDENTIFIER      — the largest block: subscript, function call, optional chain,
//                     dot-access, postfix query, identifier fallback
//   NULL COALESCING — ??
//   BINARY OPERATOR — emitter dispatch via applyBinaryOperator()
//   DOT CHAINING    — .method() / .property on resolved expression
//   TERNARY         — ? : operator
//   DIRECTIVE       — #beguilerSettings.property references
// ═══════════════════════════════════════════════════════════════════════════════
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype> 
#include <tuple> 
#include <optional>
#include <string_view>

#include "helpers.h"
#include "settings.h"
#include "typeDef.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"

using namespace std;

// Normalize path separators to the OS path separator unless rewritePaths is explicitly false.
// Treats unset (nullopt) as true — rewriting is the default.
// Resolve an include file path by searching source directory then a list of search paths.
// Tries each root + filename, optionally with an added extension.
// Returns the resolved absolute path, or empty string if not found.
string resolveIncludePath(const string& filename, const string& extension,
                                  const filesystem::path& sourceDir,
                                  const vector<string>& searchPaths){
    string normalized = filename;
    for(char& c : normalized)
        if(c == '/' || c == '\\') c = filesystem::path::preferred_separator;
    // Try source directory first
    filesystem::path candidate = sourceDir / (normalized + extension);
    if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
    if(!extension.empty()){
        candidate = sourceDir / normalized;
        if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
    }
    // Try each search path
    for(const string& sp : searchPaths){
        candidate = filesystem::path(sp) / (normalized + extension);
        if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
        if(!extension.empty()){
            candidate = filesystem::path(sp) / normalized;
            if(filesystem::exists(candidate)) return filesystem::canonical(candidate).string();
        }
    }
    return "";
}

string rewritePathSeps(const string& path){
    if(beguilerSettings.rewritePaths.has_value() && !beguilerSettings.rewritePaths.value())
        return path;
    string result = path;
    for(char& c : result)
        if(c == '/' || c == '\\') c = settings.pathSep;
    return result;
}

// Case-insensitive file lookup: returns the first directory entry whose
// lowercased filename equals the lowercased form of `target`. Falls back
// to `target` itself (which will produce a normal "file not found" error)
// if nothing matches. If multiple entries match, one is chosen arbitrarily.
filesystem::path findCaseInsensitive(const filesystem::path& dir, const string& target){
    string lower = target;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if(filesystem::exists(dir)){
        for(const auto& entry : filesystem::directory_iterator(dir)){
            string fname = entry.path().filename().string();
            string flwr = fname;
            transform(flwr.begin(), flwr.end(), flwr.begin(), ::tolower);
            if(flwr == lower) return entry.path();
        }
    }
    return dir / target; // not found; return as-is so the error surfaces normally
}

bglParser::bglParser(){
    openCompileContext(eCompileContext::global);
    // Pre-defined compiler symbols (read-only; calculated from BEGUILER_VERSION in settings.h)
    // beguiler encodes major*1000 + minor*10 + patch (e.g. 1.0.0 = 1000, 1.1.0 = 1010)
    definedSymbols["beguiler"] = to_string(BEGUILER_VERSION);
    definedSymbols["beguilermajor"]   = to_string(BEGUILER_VERSION / 1000);
    definedSymbols["beguilerminor"]   = to_string((BEGUILER_VERSION % 1000) / 10);
    definedSymbols["beguilerpatch"]   = to_string(BEGUILER_VERSION % 10);
    //emit.to(cout);  //write the result to the terminal window for now
}
void bglParser::reset(){
    // Clear all accumulated state so the parser can re-parse from scratch (for LSP re-parse)
    compileContextStack.clear();
    openCompileContext(eCompileContext::global);
    onceFiles.clear();
    startupFiles.clear();
    usingImports.clear();
    usingObjectImports.clear();
    includeDepth = 0;
    forInCounter = 0;
    lambdaCounter = 0;
    loopDepth = 0;
    ternaryDepth = 0;
    preScanOnceFiles.clear();
    preScanDepth = 0;
    pendingInjections.clear();
    postInjections.clear();
    currentObject = nullptr;
    currentClass = nullptr;
    currentFunc = nullptr;
    // Reset definedSymbols to only the built-in version symbols
    definedSymbols.clear();
    definedSymbols["beguiler"] = to_string(BEGUILER_VERSION);
    definedSymbols["beguilermajor"]   = to_string(BEGUILER_VERSION / 1000);
    definedSymbols["beguilerminor"]   = to_string((BEGUILER_VERSION % 1000) / 10);
    definedSymbols["beguilerpatch"]   = to_string(BEGUILER_VERSION % 10);
    lspErrors.clear();
}

// Open an input file, process each statement.  This is the entry point to this whole parsing process
// .inf-as-input mode: process the entire file as one implicit raw-I6 region with #bgl{}
// re-entry. Mirrors the global-scope branch of the #i6{} directive handler, with two
// differences: (a) we don't consume an opening brace (the file IS the region), and (b)
// EOF is the natural end-of-content rather than an error.
//
// Three Beguile-region directives are recognized at .inf top level:
//   #bgl{}      — auto-detect (Slice 3 will dispatch decl vs stmt; today, loose-stmt)
//   #bglStmt{}  — loose-statement form (same parser as in-routine #bgl{})
//   #bglDecl{}  — declaration form (registers types/globals/functions in source order)
// Find the first top-level `end;` directive in `text`. Walks character-by-character with
// state for I6 single-line comments (`!` to EOL), string literals (`"..."`, no `"`-escape;
// strings can span lines), and char/dictionary literals (`'...'`). The `end` token must
// have non-identifier word boundaries on both sides; the `;` may be separated by whitespace
// and any number of comments. Returns the byte offset of the start of `end`, or npos.
//
// Scope simplification: I6's `end;` is a top-level directive only — never valid inside a
// routine body — so we don't track `[`/`]` depth. Pathological inputs like `'end;'` as a
// dictionary literal at line start are accepted as failure modes per the design plan.
size_t bglParser::findInfEndDirective(const std::string& text){
    bool inDoubleString = false;
    bool inSingleQuote  = false;
    auto isIdentChar = [](char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    size_t i = 0;
    while(i < text.size()){
        char c = text[i];
        if(inDoubleString){
            if(c == '"') inDoubleString = false;
            i++; continue;
        }
        if(inSingleQuote){
            if(c == '\'') inSingleQuote = false;
            i++; continue;
        }
        if(c == '!'){ // I6 line comment to EOL
            while(i < text.size() && text[i] != '\n') i++;
            continue;
        }
        if(c == '"'){ inDoubleString = true; i++; continue; }
        if(c == '\''){ inSingleQuote = true; i++; continue; }
        // Try matching `end` as a complete word.
        if((c == 'e' || c == 'E') && i + 2 < text.size()){
            bool leftBoundary = (i == 0) || !isIdentChar(text[i-1]);
            if(leftBoundary &&
               (text[i+1] == 'n' || text[i+1] == 'N') &&
               (text[i+2] == 'd' || text[i+2] == 'D')){
                size_t after = i + 3;
                bool rightBoundary = (after >= text.size()) || !isIdentChar(text[after]);
                if(rightBoundary){
                    // Skip whitespace and comments looking for `;`. Maintain the same
                    // masking states because comments after `end` are still comments.
                    size_t j = after;
                    while(j < text.size()){
                        char d = text[j];
                        if(d == ' ' || d == '\t' || d == '\r' || d == '\n'){ j++; continue; }
                        if(d == '!'){
                            while(j < text.size() && text[j] != '\n') j++;
                            continue;
                        }
                        break;
                    }
                    if(j < text.size() && text[j] == ';') return i;
                }
            }
        }
        i++;
    }
    return std::string::npos;
}

// Cross-language collision check (.inf-mode). Walks every i6RawNode in `globals`
// (which carries the .inf body's verbatim raw text), scans for I6 top-level declarations,
// and warns when an I6-declared name collides with a Beguile-declared global in the same
// compilation. Detection is intentionally conservative — false negatives are acceptable
// (an I6 declaration form we don't recognize), false positives must be near-zero.
//
// Detected forms (case-insensitive keyword, identifier follows):
//   Object NAME       Class NAME       Constant NAME      Global NAME
//   Array NAME        Attribute NAME   Property NAME      [ NAME ...
//
// Masking states (mirroring findInfEndDirective): I6 `!` line comments, `"..."` strings,
// `'...'` char/dict literals. Same-file scope: included files (.h, .bgl) are not scanned.
void bglParser::detectInfModeI6Collisions(){
    if(languageService.infHeader.empty() && languageService.infTrailer.empty()) return;  // not .inf-mode

    // Collect all Beguile-declared global names (lowercased) for cross-check.
    std::set<std::string> beguileNames;
    for(typeDef* g : languageService.globals){
        if(dynamic_cast<i6RawNode*>(g)) continue;  // raw I6 nodes aren't named decls
        std::string n = g->name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if(!n.empty()) beguileNames.insert(n);
    }
    if(beguileNames.empty()) return;

    auto isIdentStart = [](char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto isIdentChar = [](char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    auto matchKeywordCI = [&](const std::string& text, size_t pos, const char* kw) -> size_t {
        size_t i = pos;
        for(int k = 0; kw[k]; k++){
            if(i >= text.size()) return 0;
            char tc = text[i], kc = kw[k];
            if(tc >= 'A' && tc <= 'Z') tc = (char)(tc - 'A' + 'a');
            if(kc >= 'A' && kc <= 'Z') kc = (char)(kc - 'A' + 'a');
            if(tc != kc) return 0;
            i++;
        }
        if(i < text.size() && isIdentChar(text[i])) return 0;  // word boundary
        return i;
    };
    auto skipWS = [](const std::string& text, size_t i){
        while(i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
        return i;
    };
    auto readIdentifier = [&](const std::string& text, size_t i, std::string& out, size_t& endPos){
        if(i >= text.size() || !isIdentStart(text[i])) return false;
        size_t s = i;
        while(i < text.size() && isIdentChar(text[i])) i++;
        out = text.substr(s, i - s);
        endPos = i;
        return true;
    };

    static const char* keywords[] = {
        "Object", "Class", "Constant", "Global", "Array", "Attribute", "Property"
    };

    auto scanText = [&](const std::string& text, const std::string& fileForWarn){
        bool inDoubleString = false, inSingleQuote = false;
        size_t i = 0;
        while(i < text.size()){
            char c = text[i];
            if(inDoubleString){ if(c == '"') inDoubleString = false; i++; continue; }
            if(inSingleQuote){  if(c == '\'') inSingleQuote = false; i++; continue; }
            if(c == '!'){ while(i < text.size() && text[i] != '\n') i++; continue; }
            if(c == '"'){ inDoubleString = true; i++; continue; }
            if(c == '\''){ inSingleQuote = true; i++; continue; }
            // Token boundary check: keyword/[ must be at start of a token (preceded by
            // start-of-text, whitespace, or `;` — i.e. not extending an identifier).
            bool tokStart = (i == 0) || !isIdentChar(text[i-1]);
            if(!tokStart){ i++; continue; }
            // [ NAME — routine declaration
            if(c == '['){
                size_t j = skipWS(text, i + 1);
                std::string name; size_t endPos;
                if(readIdentifier(text, j, name, endPos)){
                    std::string lower = name;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if(beguileNames.count(lower))
                        std::cerr << format("{0}: warning: I6 routine '{1}' collides with Beguile-declared global of the same name\n", fileForWarn, name);
                    i = endPos; continue;
                }
            }
            // Keyword NAME — declaration
            for(const char* kw : keywords){
                size_t after = matchKeywordCI(text, i, kw);
                if(after == 0) continue;
                size_t j = skipWS(text, after);
                std::string name; size_t endPos;
                if(readIdentifier(text, j, name, endPos)){
                    std::string lower = name;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if(beguileNames.count(lower))
                        std::cerr << format("{0}: warning: I6 {1} '{2}' collides with Beguile-declared global of the same name\n", fileForWarn, kw, name);
                    i = endPos; goto continueOuter;
                }
                i = after;
                goto continueOuter;
            }
            i++;
            continueOuter:;
        }
    };

    for(typeDef* g : languageService.globals){
        if(auto* raw = dynamic_cast<i6RawNode*>(g)){
            std::string fileForWarn = raw->src.file.empty() ? "<inf>" : raw->src.file;
            if(!raw->text.empty()) scanText(raw->text, fileForWarn);
            for(auto& part : raw->parts){
                if(!part.text.empty()){
                    std::string pfile = part.textSrc.file.empty() ? fileForWarn : part.textSrc.file;
                    scanText(part.text, pfile);
                }
            }
        }
    }
}

bool bglParser::parseInfFileBody(abstractObject& contextObj){
    // Composite raw-I6 nodes accumulate the .inf body's verbatim text plus any in-line
    // loose statements (from #bgl{} / #bglStmt{}). Declaration islands (#bglDecl{}) cause
    // the current compositeNode to be flushed to globals, declarations to be parsed (which
    // self-register into globals in source order), and a fresh compositeNode to be allocated
    // for subsequent raw text — preserving end-to-end source-order interleaving.
    i6RawNode* compositeNode = new i6RawNode();
    functionDef* synthFunc = new functionDef();
    synthFunc->name = "__bgl_inf_file_body";
    statementBlock* synthBody = new statementBlock();
    synthFunc->body = synthBody;
    openCompileContext(eCompileContext::codeBlock, synthBody);

    string accumulatedRaw;
    sourceLocation accumulatedRawSrc;
    bool firstSegment = true;  // for `!%` header extraction on the very first raw chunk

    // Helper: finalize the current compositeNode (folding any pending raw text into it),
    // push it to globals, and allocate a fresh compositeNode. Called before parsing a
    // declaration island so source order in `languageService.globals` interleaves correctly.
    auto flushCompositeNode = [&](){
        if(!accumulatedRaw.empty()){
            if(compositeNode->parts.empty()){
                compositeNode->text = accumulatedRaw;
                compositeNode->src = accumulatedRawSrc;
            } else {
                compositeNode->parts.push_back({accumulatedRaw, nullptr, accumulatedRawSrc});
            }
            accumulatedRaw.clear();
            accumulatedRawSrc = {};
        }
        if(!compositeNode->parts.empty() || !compositeNode->text.empty())
            languageService.globals.push_back(compositeNode);
        compositeNode = new i6RawNode();
    };

    int depth = 1;  // pseudo-depth: stays > 0 until EOF (forced to 0 by getRawText…'s eofTerminates)
    while(depth > 0){
        eBglDirective directive = eBglDirective::NotFound;
        sourceLocation segStart = file.currentLocation();
        string segment = file.getRawTextUntilCloseOrBgl(directive, depth, depth, /*eofTerminates=*/true);
        if(firstSegment){
            // Extract the user's `!%` ICL block from the very top of the file. I6 only honors
            // `!%` when it occupies the first line(s); we hoist it to the top of the emitted
            // output (and skip our own ICL generation) so the user's config remains effective.
            // Line-based scan: take consecutive lines whose first non-whitespace chars are `!%`,
            // stop at the first non-matching (or blank) line.
            size_t i = 0;
            size_t headerEnd = 0;
            while(i < segment.size()){
                size_t lineStart = i;
                size_t j = lineStart;
                while(j < segment.size() && (segment[j] == ' ' || segment[j] == '\t')) j++;
                bool isIclLine = (j + 1 < segment.size() && segment[j] == '!' && segment[j+1] == '%');
                if(!isIclLine) break;
                while(i < segment.size() && segment[i] != '\n') i++;
                if(i < segment.size()) i++;  // consume newline
                headerEnd = i;
            }
            if(headerEnd > 0){
                languageService.infHeader = segment.substr(0, headerEnd);
                segment = segment.substr(headerEnd);
                // Advance segStart.line by the number of header lines so the compositeNode's
                // source-line tracking reflects where the remaining segment actually begins
                // in the source file. Without this, the i6Emitter's source-map entries would
                // map every transpiled-line back to the original file but with all line
                // numbers off by the header length — breaking debugger breakpoint mapping.
                int headerNewlines = 0;
                for(size_t k = 0; k < headerEnd; k++)
                    if(languageService.infHeader[k] == '\n') headerNewlines++;
                segStart.line += headerNewlines;
            }
            firstSegment = false;
        }
        if(accumulatedRaw.empty()) accumulatedRawSrc = segStart;
        accumulatedRaw += segment;
        if(directive != eBglDirective::NotFound){
            languageService.sawBglIsland = true;
            // Two forms (matching #i6):
            //   #bgl{ stmts… }   — multi-line, terminated by matching `}`
            //   #bgl stmt;…      — single-line, terminated by newline
            string bglContent;
            sourceLocation hereLoc = file.currentLocation();
            bool isMultiLine = false;
            while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
            if(file.peekChar() == '{'){
                file.readChar();
                isMultiLine = true;
            }
            if(isMultiLine){
                file.braceDepth++;
                bglContent = file.getRawTextThroughClosingBrace();
            } else {
                char c = file.peekChar();
                while(c != '\n' && c != (char)EOF){
                    file.readChar();
                    bglContent += c;
                    c = file.peekChar();
                }
            }

            // Open the virtual file once. The auto-detect path (#bgl) peeks the first
            // token(s) to decide between declaration and statement parsing; both downstream
            // paths consume from the same open file.
            file.openText(bglContent, hereLoc.file, hereLoc.line);

            // Resolve the effective directive. For plain #bgl, examine the first few tokens:
            //   - top-level directive (#using/#once/...)         → declarations
            //   - declaration-only keywords (class/enum/...)     → declarations
            //   - typed function form (Type ident '(')           → declarations
            //   - everything else (assignment, if, typed local…) → statements (loose mode)
            // #bglDecl and #bglStmt skip the discriminator entirely.
            eBglDirective effective = directive;
            if(effective == eBglDirective::Bgl){
                file.bleedSpaces();
                if(file.peekChar() == (char)EOF){
                    effective = eBglDirective::BglStmt;  // empty: no-op stmt
                } else {
                    token t1 = file.peekToken();
                    bool isDecl = false;
                    if(t1.is(eTokenType::directive)){
                        isDecl = true;
                    } else if(t1.is("class") || t1.is("enum") || t1.is("namespace") ||
                              t1.is("grammar") || t1.is("emitter") || t1.is("extend") ||
                              t1.is(token::external) || t1.is("static") || t1.is("explicit") ||
                              t1.is("const")){
                        isDecl = true;
                    } else if(t1.is("void") || t1.isDataType()){
                        // `Type ident <terminator>` at file scope = declaration. Terminators:
                        //   '(' → function form;  ';' or '=' → variable form;
                        //   '{' → object instance with init block, or verb body, etc.
                        // Local variable declarations are not allowed in statement islands;
                        // any `Type ident` form elevates to a global decl. Mixed-content blocks
                        // (a decl-shaped first line followed by bare statements) get caught
                        // downstream when the global parser rejects the bare statement.
                        token t2 = file.peekToken(2);
                        if(t2.is(eTokenType::identifier)){
                            token t3 = file.peekToken(3);
                            if(t3.is(token::parenOpen) || t3.is(token::endStatement) || t3.is(token::assignment) || t3.is(token::braceOpen))
                                isDecl = true;
                        }
                    }
                    effective = isDecl ? eBglDirective::BglDecl : eBglDirective::BglStmt;
                }
            }

            if(effective == eBglDirective::BglDecl){
                // Declaration island: switch to global compile context and let the normal
                // top-level dispatcher handle declarations (classes, enums, globals, etc.).
                // Source-order interleaving: flush the current compositeNode first so any
                // raw I6 emitted before this island lands in `globals` ahead of the decls.
                // Loose-identifier mode applies through nested sub-parses (method bodies,
                // initializers) so references to surrounding I6 symbols pass through —
                // same migration-ramp rationale as statement islands.
                flushCompositeNode();
                closeCompileContext(eCompileContext::codeBlock);
                bool savedLoose = looseIdentifierMode;
                looseIdentifierMode = true;
                try {
                    while(true){
                        file.bleedSpaces();
                        if(file.peekChar() == (char)EOF) break;
                        if(processNextStatement()) break;
                    }
                } catch(...) {
                    looseIdentifierMode = savedLoose;
                    file.close();
                    openCompileContext(eCompileContext::codeBlock, synthBody);
                    throw;
                }
                looseIdentifierMode = savedLoose;
                file.close();
                openCompileContext(eCompileContext::codeBlock, synthBody);
            } else {
                // Statement island (#bgl with stmt content or #bglStmt): sub-parse as Beguile
                // statements in loose mode (so unknown identifiers pass through to I6).
                size_t stmtCountBefore = synthBody->statements.size();
                bool savedLoose = looseIdentifierMode;
                looseIdentifierMode = true;
                try {
                    while(true){
                        file.bleedSpaces();
                        if(file.peekChar() == (char)EOF) break;
                        token nt = file.getToken();
                        if(nt.is(eTokenType::eof)) break;
                        if(processStatementDispatch(nt, *synthFunc)) break;
                    }
                } catch(...) { looseIdentifierMode = savedLoose; file.close(); throw; }
                looseIdentifierMode = savedLoose;
                file.close();
                // Move newly-parsed statements into the composite node's parts list, attaching
                // the accumulated raw text in front of the first statement.
                bool firstStatement = true;
                for(size_t i = stmtCountBefore; i < synthBody->statements.size(); i++){
                    statement* s = synthBody->statements[i];
                    string lead = firstStatement ? accumulatedRaw : "";
                    sourceLocation leadSrc = firstStatement ? accumulatedRawSrc : sourceLocation{};
                    compositeNode->parts.push_back({lead, s, leadSrc});
                    firstStatement = false;
                }
                if(!firstStatement){
                    accumulatedRaw.clear();
                    accumulatedRawSrc = {};
                }
                synthBody->statements.resize(stmtCountBefore);
            }
        }
    }
    closeCompileContext(eCompileContext::codeBlock);
    // Trailer extraction: find the first `end;` directive in the trailing raw text and
    // hoist from there through EOF to the bottom of the emitted output. Common case is
    // `end;` as the last meaningful line of the .inf; rare edge case (an `end;` in raw
    // I6 *before* a later #bgl island) is unsupported here — the trailer scan only sees
    // text accumulated after the most recent island flush.
    if(!accumulatedRaw.empty()){
        size_t pos = findInfEndDirective(accumulatedRaw);
        if(pos != std::string::npos){
            languageService.infTrailer = accumulatedRaw.substr(pos);
            accumulatedRaw = accumulatedRaw.substr(0, pos);
        }
    }
    if(!accumulatedRaw.empty()){
        if(compositeNode->parts.empty()){
            compositeNode->text = accumulatedRaw;
            compositeNode->src = accumulatedRawSrc;
        } else {
            compositeNode->parts.push_back({accumulatedRaw, nullptr, accumulatedRawSrc});
        }
    }
    if(!compositeNode->parts.empty() || !compositeNode->text.empty())
        languageService.globals.push_back(compositeNode);
    (void)contextObj;  // reserved for future declaration-context support
    return false;
}

bool bglParser::parseFile(string filename, const std::string* contentOverride){
    string absPath;
    try { absPath = filesystem::canonical(filesystem::absolute(filename)).string(); }
    catch(...) { absPath = filename; }  // contentOverride callers may pass paths that don't exist on disk
    // If this file declared #once, silently skip subsequent inclusions.
    if(onceFiles.count(absPath)) return false;
    // Guard against runaway or circular includes.
    if(includeDepth > maxIncludeDepth)
        return parsingError(format("Maximum include nesting depth ({0}) exceeded while including '{1}'", maxIncludeDepth, filename));
    includeDepth++;
    // Save file-scoped #using imports — each file has its own imports
    vector<classDef*>  savedUsingImports = usingImports;
    vector<objectDef*> savedUsingObjectImports = usingObjectImports;
    usingImports.clear();
    usingObjectImports.clear();

    try{
        if(contentOverride)
            file.openText(*contentOverride, absPath, 1);  // LSP path: parse the editor buffer directly
        else
            file.open(absPath);
    }
    catch(runtime_error& e){
        usingImports = savedUsingImports;
        usingObjectImports = savedUsingObjectImports;
        includeDepth--;
        return parsingError(e.what());
    }

    bool isInfMode = false;
    if(file.getNumberOfOpenFiles()==1){
        cout<<format("Beguiling file \"{0}\"...\n",filename);

        // Always load the Beguile Language Runtime first so built-in types (eBool, eTarget, etc.)
        // and emitters are available to user code — including #bgl{} blocks inside .inf files.
        filesystem::path systemPath = filesystem::path(settings.libPath) / "core" / "__beguileCore.bgl";
        parseFile(systemPath.string());
        // Load built-in I6 templates (for-in loop etc.) from beguilib/core/__builtins.i6b
        filesystem::path builtinsPath = filesystem::path(settings.libPath) / "core" / "__builtins.i6b";
        emitter.loadBuiltinTemplates(builtinsPath.string());

        // .inf-as-input mode: detect the entry-file extension. The whole file becomes a single
        // implicit raw-I6 region with #bgl{} re-entry. Skip the normal Beguile statement loop.
        isInfMode = (filesystem::path(filename).extension() == ".inf");
        languageService.isInfMode = isInfMode;
    }

    if(isInfMode){
        try{
            // Synthesize a function context so #bgl statements have a valid host scope.
            functionDef synthCtx;
            synthCtx.name = "__bgl_inf_file_body";
            statementBlock synthBody;
            synthCtx.body = &synthBody;
            parseInfFileBody(synthCtx);
        } catch(...){
            file.close();
            usingImports = savedUsingImports;
            usingObjectImports = savedUsingObjectImports;
            includeDepth--;
            throw;
        }
        file.close();
        usingImports = savedUsingImports;
        usingObjectImports = savedUsingObjectImports;
        includeDepth--;
        return false;
    }

    //process all statements in the file one by one.  This may include recursive calls for included files.
    try{
        while(processNextStatement()==false){
            //cout<<"Block processed."<<endl;
        }
    } catch(lspRecoverySignal&){
        // LSP error recovery: unwind back to top-level scope, then continue parsing the next
        // top-level declaration. Uses file.braceDepth (maintained by the lexer) to know exactly
        // how deeply nested the parser was when the error fired, so we consume the right number
        // of closing braces to reach global scope — not just the nearest '}'. This matters for
        // errors inside nested function bodies (e.g. `class Foo { void m(){ bad_stmt; } }`),
        // which previously left recovery stuck at mid-file depth and silently dropped every
        // subsequent top-level declaration.
        while(lspMode && file.getNumberOfOpenFiles() > 0) {
            bool hitEof = false;
            if(file.braceDepth > 0) {
                // Nested error: consume tokens until the lexer's braceDepth falls back to 0.
                // getToken() auto-decrements on each '}' it emits, so this naturally unwinds
                // the full nesting level at the error site.
                while(file.braceDepth > 0) {
                    token t = file.getToken();
                    if(t.is(eTokenType::eof)) { hitEof = true; break; }
                }
            } else {
                // Top-level error: scan forward to the next ';' so we skip past the bad statement
                // without consuming subsequent top-level declarations.
                while(true) {
                    token t = file.getToken();
                    if(t.is(eTokenType::eof)) { hitEof = true; break; }
                    if(t.is(token::endStatement)) break;
                    if(t.is(token::braceClose) && file.braceDepth == 0) break;
                }
            }
            if(hitEof) break;
            // Reset compile context to global (error may have left us nested). The stack uses
            // push_front/pop_front — front() is the top — so pop_front() unwinds the nested
            // frames while leaving the bottom (global) frame intact.
            while(compileContextStack.size() > 1) compileContextStack.pop_front();
            currentObject = nullptr;
            currentClass = nullptr;
            currentFunc = nullptr;
            // Continue parsing — if another error occurs, the loop catches it
            try {
                while(processNextStatement()==false){}
                break;  // parsing finished normally
            } catch(lspRecoverySignal&) {
                continue;  // another error — loop and recover again
            } catch(exitFileSignal&) {
                break;
            }
        }
    } catch(exitFileSignal&){
        // #exit directive: treat as end-of-file, discarding any open directive nesting
    }
    //emit.out<<endl;
    file.close();
    usingImports = savedUsingImports;  // restore parent file's imports
    usingObjectImports = savedUsingObjectImports;
    includeDepth--;
    return false;
}

//==================================================================
// Pre-scanner methods (preScanFile, preScanDirective, preScanSkip*)
// have been moved to bglPreScanner.cpp for code organization.
//==================================================================


//===============================================================================================================================
// Grammar-driven pattern matching (V2 parser dispatcher)
//===============================================================================================================================

// initGrammarTable() is in grammarTable.cpp

bool bglParser::matchElement(const PatternElement& elem, token& tok) {
    switch(elem.kind) {
        case PatternElement::Kind::Literal:
            return tok.value == elem.literal || tok.is(elem.literal);
        case PatternElement::Kind::TokenType:
            return tok.is(elem.tokenType);
        case PatternElement::Kind::AnyOf:
            for(const string& s : elem.anyOfValues)
                if(tok.value == s || tok.is(s)) return true;
            return false;
        case PatternElement::Kind::Semantic:
            return elem.predicate && elem.predicate(tok);
        case PatternElement::Kind::Wildcard:
            return true;
    }
    return false;
}

string bglParser::describeExpected(const PatternElement& elem) {
    if(!elem.tag.empty()) return elem.tag;
    switch(elem.kind) {
        case PatternElement::Kind::Literal:   return "'" + elem.literal + "'";
        case PatternElement::Kind::TokenType:
            switch(elem.tokenType) {
                case eTokenType::identifier:    return "an identifier";
                case eTokenType::dataType:      return "a type name";
                case eTokenType::integer:       return "a number";
                case eTokenType::quote:         return "a string";
                case eTokenType::directive:     return "a directive";
                case eTokenType::symbol:        return "a symbol";
                case eTokenType::eof:           return "end of file";
                default:                        return "a token";
            }
        case PatternElement::Kind::AnyOf: {
            string s;
            for(size_t i = 0; i < elem.anyOfValues.size(); i++) {
                if(i > 0) s += (i == elem.anyOfValues.size()-1) ? " or " : ", ";
                s += "'" + elem.anyOfValues[i] + "'";
            }
            return s;
        }
        case PatternElement::Kind::Semantic:  return "a valid token";
        case PatternElement::Kind::Wildcard:  return "any token";
    }
    return "?";
}

GrammarMatch bglParser::matchGrammar(token& firstToken) {
    GrammarMatch result;

    // Phase 1: filter to candidates whose first element matches firstToken
    struct Candidate {
        int ruleIndex;
        int patternPos;  // how far into the pattern we've matched
    };
    vector<Candidate> alive;
    for(int i = 0; i < (int)grammarRules.size(); i++) {
        if(!grammarRules[i].pattern.empty() && matchElement(grammarRules[i].pattern[0], firstToken))
            alive.push_back({i, 1});
    }

    if(alive.empty()) return result;

    // Helper: populate result from a winning rule and consume tokens
    auto setWinner = [&](GrammarRule& winner, int tokensToConsume) {
        result.success = true;
        result.ruleName = winner.name;
        result.handler = winner.handler;
        result.matchedTokens.push_back(firstToken);
        for(int i = 0; i < tokensToConsume; i++)
            result.matchedTokens.push_back(file.getToken());
    };

    // Separate complete vs incomplete candidates
    // `bestComplete` tracks the best rule that has completed its pattern at some depth.
    // We preserve it across peek iterations so a shorter rule that completed early isn't lost
    // when a longer rule (still incomplete) gets eliminated in a later phase.
    struct BestComplete { int ruleIndex = -1; int depthAtComplete = 0; };
    BestComplete bestComplete;
    vector<Candidate> incomplete;
    for(auto& c : alive) {
        if(c.patternPos >= (int)grammarRules[c.ruleIndex].pattern.size()) {
            if(bestComplete.ruleIndex < 0) bestComplete = {c.ruleIndex, 0};
        } else {
            incomplete.push_back(c);
        }
    }

    // All complete, none incomplete — first rule wins (table order = priority)
    if(incomplete.empty() && bestComplete.ruleIndex >= 0) {
        setWinner(grammarRules[bestComplete.ruleIndex], 0);
        return result;
    }

    // Phase 2: peek ahead to disambiguate
    for(int peekDepth = 1; peekDepth <= 10; peekDepth++) {
        token peek = file.peekToken(peekDepth);

        vector<Candidate> stillAlive;
        for(auto& c : incomplete) {
            auto& rule = grammarRules[c.ruleIndex];
            if(c.patternPos < (int)rule.pattern.size() && matchElement(rule.pattern[c.patternPos], peek))
                stillAlive.push_back({c.ruleIndex, c.patternPos + 1});
            else
                result.failedCandidates.push_back({rule.name, c.patternPos});
        }

        incomplete.clear();
        for(auto& c : stillAlive) {
            if(c.patternPos >= (int)grammarRules[c.ruleIndex].pattern.size()) {
                // This candidate just completed — longer matches beat shorter ones, so it wins
                // over any earlier bestComplete (we've matched more tokens).
                bestComplete = {c.ruleIndex, peekDepth};
            } else {
                incomplete.push_back(c);
            }
        }

        // Single candidate remains — verify any remaining elements then consume
        if(stillAlive.size() == 1 && incomplete.size() == 1) {
            auto& cand = incomplete[0];
            auto& winner = grammarRules[cand.ruleIndex];
            int totalPeek = peekDepth;
            bool verified = true;
            while(cand.patternPos < (int)winner.pattern.size()) {
                totalPeek++;
                token p = file.peekToken(totalPeek);
                if(!matchElement(winner.pattern[cand.patternPos], p)) {
                    verified = false;
                    result.failedCandidates.push_back({winner.name, cand.patternPos});
                    break;
                }
                cand.patternPos++;
            }
            if(verified) { setWinner(winner, totalPeek); return result; }
            // Verification failed — fall through: bestComplete (if any) still wins
            break;
        }

        // No incomplete candidates left — bestComplete wins (if any)
        if(incomplete.empty()) break;
    }

    // Final: bestComplete wins if any rule completed during the walk
    if(bestComplete.ruleIndex >= 0) {
        setWinner(grammarRules[bestComplete.ruleIndex], bestComplete.depthAtComplete);
        return result;
    }

    return result;
}

// Grammar handler methods — standard GrammarHandler signature
bool bglParser::processEnum(vector<token>& t, Qualifiers& q, abstractObject&)
    { return processEnumDeclaration(t[0], q.isExtern, t[1]); }
bool bglParser::processClass(vector<token>& t, Qualifiers& q, abstractObject&)
    { return processClassDeclaration(t[0], q.isExtern, q.isExtend, q.isEmitter, q.isAlias, t[1]); }
bool bglParser::processGrammar(vector<token>& t, Qualifiers&, abstractObject&)
    { return processGrammarDeclaration(t[1]); }
bool bglParser::processArray(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processArrayDeclarationFromGeneric(t[0], q, c); }
bool bglParser::processRoutine(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processRoutineDeclaration(t[0], t[1], c, q.isExtern, q.isEmitter, q.isReplace); }
bool bglParser::processObject(vector<token>& t, Qualifiers& q, abstractObject&)
    { t[0] = consumeTypeToken(t[0]); return processObjectDeclaration(t[0], t[1], q.isExtern, "", "", true, q.isEmitter); }
bool bglParser::processVariable(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processVariableDeclaration(t[0], t[1], t[2], c, q.isExtern, q.isConst); }
bool bglParser::processTypedObject(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processTypedObjectDeclaration(t[0], t[1], t[3], q, c); }
bool bglParser::processAliased(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processAliasedDeclaration(t[0], t[1], t[3], q, c); }

// ── Statement handlers (code-block scope) ───────────────────────────────────
bool bglParser::processBreak(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'break' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    i6RawNode& brk = *(new i6RawNode());
    brk.text = "break;";
    if(body != nullptr) body->statements.push_back(&brk);
    return false;
}

bool bglParser::processContinue(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'continue' is not valid at global scope");
    if(loopDepth == 0)
        parsingError("'continue' is only valid inside a loop (for, while, or do)");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    i6RawNode& cont = *(new i6RawNode());
    cont.text = "continue;";
    if(body != nullptr) body->statements.push_back(&cont);
    return false;
}

bool bglParser::processRtrue(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'rtrue' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot use 'rtrue' in void routine '{0}'", func->name));
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    returnStatement& rt = *(new returnStatement());
    rt.src = file.currentLocation();
    rt.returnExpression = "rtrue";
    if(body != nullptr) body->statements.push_back(&rt);
    return false;
}

bool bglParser::processRfalse(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'rfalse' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot use 'rfalse' in void routine '{0}'", func->name));
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    returnStatement& rf = *(new returnStatement());
    rf.src = file.currentLocation();
    rf.returnExpression = "rfalse";
    if(body != nullptr) body->statements.push_back(&rf);
    return false;
}

bool bglParser::processReturnVoid(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'return' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    if(body != nullptr) body->statements.push_back(&rs);
    return false;
}

bool bglParser::processReturnExpr(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'return' is not valid at global scope");
    // ctx is the synthetic functionDef for the current block (carries the body the return
    // should land in — possibly a nested if/else block, not the outer function root).
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    // For diagnostics, prefer currentFunc's name — the synthetic block contexts have no name.
    const string& funcName = (currentFunc && !currentFunc->name.empty()) ? currentFunc->name
                              : (func ? func->name : string());
    // Read the return expression first so we can see its type. A `return <void-typed expr>;`
    // from a void function is the C/I6 tail-call idiom (`return f();` ≡ `f(); return;`); we
    // accept it because the wrapping return adds nothing observable. Any other value-bearing
    // return from a void function remains an error. The allowVoidReturnExpr flag suppresses
    // the parseExpression-level guard that would otherwise reject void calls in expressions.
    bool savedAllowVoid = allowVoidReturnExpr;
    allowVoidReturnExpr = (func != nullptr && func->returnType.name == "void");
    token first = file.getToken();
    expression* retExpr = parseExpression(first, {token::endStatement}, func, body);
    allowVoidReturnExpr = savedAllowVoid;
    if(func != nullptr && func->returnType.name == "void"){
        // Allow `return <void-typed expr>;` as the C/I6 idiom shorthand. In loose-mode
        // contexts (`#bgl{}` islands and `.inf` precompiler mode), unresolved identifiers
        // resolve to `var`; treat `var` as compatible too so `return f()` ports verbatim
        // when `f` was declared in I6 and isn't visible to the Beguile resolver.
        string retType = retExpr ? retExpr->resolvedType : "";
        if(retType != "void" && retType != "var")
            parsingError(format("Cannot return a value from void routine '{0}'", funcName));
    }
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    rs.returnExpression = retExpr ? retExpr->text() : "";
    // Drain any pre-statements the expression created (e.g. ternary lowering into _bgl_temp)
    // BEFORE appending the return, so they execute in the current function rather than leaking
    // into the next one parsed.
    if(body != nullptr){
        for(statement* inj : pendingInjections) body->statements.push_back(inj);
        pendingInjections.clear();
        body->statements.push_back(&rs);
    }
    return false;
}

bool bglParser::processIf(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'if' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    ifStatement& ifStmt = *(new ifStatement());
    ifStmt.src = stmtLoc;
    // Caller already consumed "if" "(" — read condition
    ifStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    // Drain any ternary injections from the condition BEFORE the if statement
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    ifStmt.thenBlock = new statementBlock();
    functionDef thenCtx;
    if(func != nullptr){ thenCtx.returnType = func->returnType; thenCtx.params = func->params; }
    thenCtx.body = ifStmt.thenBlock;
    token next = file.getToken();
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, ifStmt.thenBlock);
        while(processNextStatement(thenCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processStatementDispatch(next, thenCtx);
    }
    if(file.peekToken().is("else")){
        file.getToken();
        ifStmt.elseBlock = new statementBlock();
        functionDef elseCtx;
        if(func != nullptr){ elseCtx.returnType = func->returnType; elseCtx.params = func->params; }
        elseCtx.body = ifStmt.elseBlock;
        token elseNext = file.getToken();
        if(elseNext.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, ifStmt.elseBlock);
            while(processNextStatement(elseCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(elseNext, elseCtx);
        }
    }
    if(body != nullptr) body->statements.push_back(&ifStmt);
    return false;
}

bool bglParser::processWhile(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'while' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    whileStatement& whileStmt = *(new whileStatement());
    whileStmt.src = stmtLoc;
    // Caller already consumed "while" "(" — read condition
    whileStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    // Drain any ternary injections from the condition BEFORE the while statement
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    whileStmt.body = new statementBlock();
    functionDef whileCtx;
    if(func != nullptr){ whileCtx.returnType = func->returnType; whileCtx.params = func->params; }
    whileCtx.body = whileStmt.body;
    token next = file.getToken();
    loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, whileStmt.body);
        while(processNextStatement(whileCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processStatementDispatch(next, whileCtx);
    }
    loopDepth--;
    if(body != nullptr) body->statements.push_back(&whileStmt);
    return false;
}

bool bglParser::processFor(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'for' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller already consumed "for" "("

    bool isForIn = false;
    string elemVarName, elemVarType;

    token peek = file.peekToken();
    if(peek.isDataType()){
        token typeTok = file.getToken(eTokenType::dataType);
        // Accept both identifier and dataType for the loop variable name. A dataType here means
        // the user chose a name that collides with a registered class (e.g. 'Counter'); the
        // shadow check below produces a cleaner error than a raw token-type mismatch.
        token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        // Shadow check: disallow loop variable names that collide with a global, a class member,
        // or an object member. Matches the parameter/local-variable shadow checks elsewhere.
        auto checkShadow = [&](const string& name) {
            for(typeDef* g : languageService.globals)
                if(g->name == name){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                        const string& t = vd->type.name;
                        if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                    }
                    parsingWarning("Loop variable '" + name + "' shadows global of the same name; the global is unreachable from this loop body.");
                }
            if(currentClass != nullptr){
                for(typeMember* m : currentClass->members)
                    if(m->name == name)
                        parsingWarning("Loop variable '" + name + "' shadows a member of class '" + currentClass->name + "'.");
                // Walk base class hierarchy for inherited members (vars and functions) — warning only
                function<void(classDef*)> checkBases = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(m->name == name)
                            if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                                parsingWarning("Loop variable '" + name + "' shadows inherited member '" + name + "' from class '" + c->dName() + "'.");
                    for(classDef* base : c->baseClasses) checkBases(base);
                };
                for(classDef* base : currentClass->baseClasses) checkBases(base);
            }
            if(currentObject != nullptr)
                for(typeMember* m : currentObject->members)
                    if(m->name == name)
                        parsingWarning("Loop variable '" + name + "' shadows a member of object '" + currentObject->name + "'.");
        };
        checkShadow(nameTok.value);
        if(file.peekToken().is("in")){
            isForIn = true;
            elemVarName = nameTok.value;
            elemVarType = typeTok.value;
            bool alreadyDeclared = false;
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == elemVarName){
                            if(vd->type.name != elemVarType)
                                parsingError(format("Loop variable '{0}' redeclared with different type '{1}' (was '{2}')",
                                    elemVarName, elemVarType, vd->type.name));
                            alreadyDeclared = true;
                            break;
                        }
            if(!alreadyDeclared){
                variableDeclaration& elemDecl = *(new variableDeclaration());
                elemDecl.name = elemVarName;
                elemDecl.type = languageService.getType(elemVarType);
                if(body != nullptr) body->statements.push_back(&elemDecl);
            }
        } else {
            // C-style for with typed init
            bool alreadyDeclared = false;
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == nameTok.value){
                            if(vd->type.name != typeTok.value)
                                parsingError(format("Loop variable '{0}' redeclared with different type '{1}' (was '{2}')",
                                    nameTok.value, typeTok.value, vd->type.name));
                            alreadyDeclared = true;
                            break;
                        }
            if(!alreadyDeclared){
                variableDeclaration& loopVar = *(new variableDeclaration());
                loopVar.name = nameTok.value;
                loopVar.type = languageService.getType(typeTok.value);
                if(body != nullptr) body->statements.push_back(&loopVar);
            }
            forStatement& forStmt = *(new forStatement());
            forStmt.src = stmtLoc;
            string initText = nameTok.value;
            token tt = file.getToken();
            while(tt.isNot(token::endStatement)){
                if(!initText.empty()) initText += " ";
                initText += tt.value;
                tt = file.getToken();
            }
            forStmt.initText = initText;
            forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
            pendingInjections.clear();
            expression* incrExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
            vector<statement*> incrInjections = pendingInjections;
            pendingInjections.clear();
            string incrText = incrExpr ? incrExpr->text() : "";
            if(!incrInjections.empty()) forStmt.incrementText = "";
            else forStmt.incrementText = incrText;
            forStmt.body = new statementBlock();
            functionDef forCtx;
            if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
            forCtx.body = forStmt.body;
            token next = file.getToken();
            currentLoopVars.insert(nameTok.value); loopDepth++;
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock, forStmt.body);
                while(processNextStatement(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatementDispatch(next, forCtx);
            }
            // If ternary in increment: append injections + increment as last body statements
            if(!incrInjections.empty()){
                for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
                i6RawNode* incrStmt = new i6RawNode();
                incrStmt->text = incrText + ";";
                forStmt.body->statements.push_back(incrStmt);
            }
            loopDepth--; currentLoopVars.erase(nameTok.value);
            if(body != nullptr) body->statements.push_back(&forStmt);
            return false;
        }
    } else if(peek.is(eTokenType::identifier)){
        token nameTok = file.getToken(eTokenType::identifier);
        if(file.peekToken().is("in")){
            isForIn = true;
            elemVarName = nameTok.value;
            if(func != nullptr)
                for(paramDef* p : func->params)
                    if(p->name == elemVarName){ elemVarType = p->type.name; break; }
            if(elemVarType.empty() && body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == elemVarName){ elemVarType = vd->type.name; break; }
            if(elemVarType.empty())
                for(typeDef* g : languageService.globals)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == elemVarName){ elemVarType = vd->type.name; break; }
            if(elemVarType.empty())
                parsingError(format("'for in': iteration variable '{0}' is not declared", elemVarName));
        } else {
            // C-style for — nameTok was the first init token
            forStatement& forStmt = *(new forStatement());
            forStmt.src = stmtLoc;
            string initText = nameTok.value;
            token tt = file.getToken();
            while(tt.isNot(token::endStatement)){
                if(!initText.empty()) initText += " ";
                initText += tt.value;
                tt = file.getToken();
            }
            forStmt.initText = initText;
            forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
            pendingInjections.clear();
            expression* incrExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
            vector<statement*> incrInjections = pendingInjections;
            pendingInjections.clear();
            string incrText = incrExpr ? incrExpr->text() : "";
            if(!incrInjections.empty()) forStmt.incrementText = "";
            else forStmt.incrementText = incrText;
            forStmt.body = new statementBlock();
            functionDef forCtx;
            if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
            forCtx.body = forStmt.body;
            token next = file.getToken();
            currentLoopVars.insert(nameTok.value); loopDepth++;
            if(next.is(token::braceOpen)){
                openCompileContext(eCompileContext::codeBlock, forStmt.body);
                while(processNextStatement(forCtx) == false){}
                closeCompileContext(eCompileContext::codeBlock);
            } else {
                processStatementDispatch(next, forCtx);
            }
            // If ternary in increment: append injections + increment as last body statements
            if(!incrInjections.empty()){
                for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
                i6RawNode* incrStmt = new i6RawNode();
                incrStmt->text = incrText + ";";
                forStmt.body->statements.push_back(incrStmt);
            }
            loopDepth--; currentLoopVars.erase(nameTok.value);
            if(body != nullptr) body->statements.push_back(&forStmt);
            return false;
        }
    }

    if(!isForIn){
        // C-style for — init starts with non-identifier or empty
        forStatement& forStmt = *(new forStatement());
        forStmt.src = stmtLoc;
        string initText;
        token tt = file.getToken();
        while(tt.isNot(token::endStatement)){
            if(!initText.empty()) initText += " ";
            initText += tt.value;
            tt = file.getToken();
        }
        forStmt.initText = initText;
        forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        expression* incrExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
        vector<statement*> incrInjections = pendingInjections;
        pendingInjections.clear();
        string incrText = incrExpr ? incrExpr->text() : "";
        if(!incrInjections.empty()) forStmt.incrementText = "";
        else forStmt.incrementText = incrText;
        forStmt.body = new statementBlock();
        functionDef forCtx;
        if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
        forCtx.body = forStmt.body;
        token next = file.getToken();
        loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, forStmt.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        if(!incrInjections.empty()){
            for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
            i6RawNode* incrStmt = new i6RawNode();
            incrStmt->text = incrText + ";";
            forStmt.body->statements.push_back(incrStmt);
        }
        loopDepth--;
        if(body != nullptr) body->statements.push_back(&forStmt);
        return false;
    }

    // for-in shared (Form 1 and Form 2)
    file.getToken("in");

    // Inline initializer list: for(int j in {1, 2, 3})
    if(file.peekToken(1).is(token::braceOpen)){
        file.getToken(); // consume '{'
        vector<expression*> elements;
        token et = file.getToken();
        while(!et.is(token::braceClose) && !et.is(eTokenType::eof)){
            expression* elem = parseExpression(et, {",", token::braceClose}, func, body);
            elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            et = file.getToken();
        }
        file.getToken(token::parenClose);

        string arrName = format("_bglfia{0}", forInCounter++);
        variableDeclaration& tmpDecl = *(new variableDeclaration());
        tmpDecl.name = arrName;
        tmpDecl.type = languageService.getType("var");
        if(body != nullptr) body->statements.push_back(&tmpDecl);

        string counterName = format("_bglfi{0}", forInCounter++);
        variableDeclaration& counterDecl = *(new variableDeclaration());
        counterDecl.name = counterName;
        counterDecl.type = languageService.getType("var");
        if(body != nullptr) body->statements.push_back(&counterDecl);

        if(elemVarType == "auto" && !elements.empty() && !elements[0]->resolvedType.empty()){
            elemVarType = elements[0]->resolvedType;
            classDef* elemCls = dynamic_cast<classDef*>(&languageService.getType(elemVarType));
            if(elemCls)
                for(typeMember* m : elemCls->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == "auto"){ elemVarType = fd->returnType.name; break; }
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == elemVarName){ vd->type = languageService.getType(elemVarType); break; }
        } else if(elemVarType == "auto"){
            elemVarType = "var";
        } else if(elemVarType != "var" && !elements.empty()){
            for(size_t i = 0; i < elements.size(); i++){
                string et = elements[i]->resolvedType;
                if(!et.empty() && !isTypeCompatible(et, elemVarType))
                    parsingError(format("'for in': inline list element {0} has type '{1}', incompatible with loop variable type '{2}'",
                        i, et, elemVarType));
            }
        }

        forInStatement& fi = *(new forInStatement());
        fi.src = stmtLoc;
        fi.elementVar = elemVarName;
        fi.arrayVar   = arrName;
        fi.counterVar = counterName;
        fi.isByteArray = (elemVarType == "char");
        fi.inlineElements = elements;
        fi.body = new statementBlock();
        functionDef forCtx;
        if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
        forCtx.body = fi.body;
        paramDef& elemParam = *(new paramDef());
        elemParam.name = elemVarName;
        elemParam.type = languageService.getType(elemVarType);
        forCtx.params.push_back(&elemParam);
        token next = file.getToken();
        currentLoopVars.insert(elemVarName); loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, fi.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        loopDepth--; currentLoopVars.erase(elemVarName);
        if(body != nullptr) body->statements.push_back(&fi);
        return false;
    }

    // Range for-in: for(int i in 1 to 10)
    expression* arrExpr = parseExpression(file.getToken(), {token::parenClose, "to"}, func, body);
    if(arrExpr->terminator == "to"){
        expression* rangeEnd = parseExpression(file.getToken(), {token::parenClose}, func, body);
        forStatement& forStmt = *(new forStatement());
        forStmt.src = stmtLoc;
        forStmt.initText = elemVarName + " = " + arrExpr->text();
        expression* cond = new expression();
        cond->tokens.push_back(elemVarName);
        cond->tokens.push_back("<=");
        cond->tokens.push_back(rangeEnd->text());
        forStmt.condition = cond;
        forStmt.incrementText = elemVarName + "++";
        forStmt.body = new statementBlock();
        functionDef forCtx;
        if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
        forCtx.body = forStmt.body;
        token next = file.getToken();
        currentLoopVars.insert(elemVarName); loopDepth++;
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, forStmt.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processStatementDispatch(next, forCtx);
        }
        loopDepth--; currentLoopVars.erase(elemVarName);
        if(body != nullptr) body->statements.push_back(&forStmt);
        return false;
    }

    // Array for-in
    string arrExprText = arrExpr ? arrExpr->text() : "";
    string arrName;
    string arrElemType = "";

    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                if(ad->name == arrExprText){ arrElemType = ad->elementType; arrName = arrExprText; break; }
    if(arrName.empty())
        for(typeDef* g : languageService.globals)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(g))
                if(ad->name == arrExprText){ arrElemType = ad->elementType; arrName = arrExprText; break; }
    if(arrName.empty() && currentObject != nullptr){
        string memberName = (arrExprText.rfind("self.", 0) == 0) ? arrExprText.substr(5) : arrExprText;
        for(typeMember* m : currentObject->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == memberName){ arrElemType = ad->elementType; arrName = arrExprText; break; }
    }
    if(arrName.empty() && func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == arrExprText){
                string tn = p->type.name;
                arrElemType = (tn.size() > 6 && tn.substr(0,6) == "array<") ? tn.substr(6, tn.size()-7) : "var";
                arrName = arrExprText;
                break;
            }

    if(arrName.empty()){
        arrName = format("_bglfia{0}", forInCounter++);
        variableDeclaration& tmpDecl = *(new variableDeclaration());
        tmpDecl.name = arrName;
        tmpDecl.type = languageService.getType("var");
        if(body != nullptr) body->statements.push_back(&tmpDecl);
        i6RawNode& assign = *(new i6RawNode());
        assign.text = arrName + " = " + arrExprText + ";";
        if(body != nullptr) body->statements.push_back(&assign);
        arrElemType = "var";
    }

    if(elemVarType == "auto"){
        if(arrElemType.empty() || arrElemType == "var")
            elemVarType = "var";
        else {
            elemVarType = arrElemType;
            classDef* elemCls = dynamic_cast<classDef*>(&languageService.getType(elemVarType));
            if(elemCls)
                for(typeMember* m : elemCls->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == "auto"){ elemVarType = fd->returnType.name; break; }
        }
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == elemVarName){ vd->type = languageService.getType(elemVarType); break; }
    }

    if(elemVarType != arrElemType && elemVarType != "var" && arrElemType != "var"
       && !isTypeCompatible(arrElemType, elemVarType))
        parsingError(format("'for in': variable '{0}' has type '{1}' but '{2}' has element type '{3}'",
            elemVarName, elemVarType, arrName, arrElemType));

    string counterName = format("_bglfi{0}", forInCounter++);
    variableDeclaration& counterDecl = *(new variableDeclaration());
    counterDecl.name = counterName;
    counterDecl.type = languageService.getType("var");
    if(body != nullptr) body->statements.push_back(&counterDecl);

    forInStatement& fi = *(new forInStatement());
    fi.src = stmtLoc;
    fi.elementVar = elemVarName;
    fi.arrayVar   = arrName;
    fi.counterVar = counterName;
    fi.isByteArray = (arrElemType == "char");
    fi.body = new statementBlock();
    functionDef forCtx;
    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
    paramDef& elemParam = *(new paramDef());
    elemParam.name = elemVarName;
    elemParam.type = languageService.getType(elemVarType);
    forCtx.params.push_back(&elemParam);
    forCtx.body = fi.body;
    token next = file.getToken();
    currentLoopVars.insert(elemVarName); loopDepth++;
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, fi.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processStatementDispatch(next, forCtx);
    }
    loopDepth--; currentLoopVars.erase(elemVarName);
    if(body != nullptr) body->statements.push_back(&fi);
    return false;
}

bool bglParser::processDo(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'do' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    doStatement& doStmt = *(new doStatement());
    doStmt.src = stmtLoc;
    doStmt.body = new statementBlock();
    functionDef doCtx;
    if(func != nullptr){ doCtx.returnType = func->returnType; doCtx.params = func->params; }
    doCtx.body = doStmt.body;
    // Caller already consumed "do" "{" — parse body
    loopDepth++;
    openCompileContext(eCompileContext::codeBlock, doStmt.body);
    while(processNextStatement(doCtx) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    loopDepth--;
    // Expect 'while' or 'until'
    token keyword = file.getToken({eTokenType::identifier, eTokenType::dataType});
    if(keyword.is("while")) doStmt.isWhile = true;
    else if(!keyword.is("until")) parsingError(format("Expected 'while' or 'until' after do block, got '{0}'", keyword.value));
    file.getToken(token::parenOpen);
    doStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    if(file.peekToken().is(token::endStatement)) file.getToken();
    if(body != nullptr) body->statements.push_back(&doStmt);
    return false;
}

bool bglParser::processSwitch(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'switch' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller already consumed "switch" "("
    switchStatement& swStmt = *(new switchStatement());
    swStmt.src = stmtLoc;
    swStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    string conditionType = swStmt.condition->resolvedType;
    classDef* condCls = !conditionType.empty() ? dynamic_cast<classDef*>(&languageService.getType(conditionType)) : nullptr;
    if(condCls != nullptr){
        std::function<void(classDef*)> findSwitchOps = [&](classDef* c){
            for(typeMember* m : c->members)
                if(auto* fn = dynamic_cast<functionDef*>(m))
                    if(fn->name == "switch" && fn->isEmitter && fn->params.size() == 1)
                        if(auto* blk = dynamic_cast<i6Block*>(fn->body)){
                            string paramType = fn->params[0]->type.name;
                            if(swStmt.switchEmitters.find(paramType) == swStmt.switchEmitters.end()){
                                string b = processBglConditionals(blk->i6Body);
                                swStmt.switchEmitters[paramType] = fn->params[0]->name + "\t" + b;
                            }
                        }
            for(classDef* base : c->baseClasses) findSwitchOps(base);
        };
        findSwitchOps(condCls);
        if(!swStmt.switchEmitters.empty()) swStmt.needsIfChain = true;
    }
    file.getToken(token::braceOpen);
    while(true){
        token tt = file.getToken();
        if(tt.is(token::braceClose)) break;
        switchCase& sc = *(new switchCase());
        if(tt.is("default")){
            file.getToken(":");
        } else {
            tt.assert("case", "Expected 'case' or 'default' inside switch.");
            auto parseCaseExpr = [&]() -> expression* {
                expression* val = parseExpression(file.getToken(), {":", ",", "to"}, func, body);
                if(!conditionType.empty() && !val->resolvedType.empty()
                   && !isTypeCompatible(val->resolvedType, conditionType)
                   && val->resolvedType != "verb")
                    parsingError(format("Switch case type '{0}' does not match condition type '{1}'",
                                       val->resolvedType, conditionType));
                return val;
            };
            auto parseNextEntry = [&](expression*& lastExpr) {
                token peek = file.peekToken(1);
                if(peek.is(eTokenType::oper) && (peek.value==">"||peek.value==">="||peek.value=="<"||peek.value=="<=")){
                    token op = file.getToken();
                    expression* val = parseCaseExpr();
                    caseEntry e;
                    e.guardCondition = "_bgl_sw " + op.value + " " + val->text();
                    sc.entries.push_back(e);
                    swStmt.needsIfChain = true;
                    lastExpr = val;
                    return;
                }
                expression* val = parseCaseExpr();
                if(val->terminator == "to"){
                    expression* high = parseCaseExpr();
                    caseEntry e;
                    e.rangeLow = val;
                    e.rangeHigh = high;
                    sc.entries.push_back(e);
                    lastExpr = high;
                } else {
                    caseEntry e;
                    e.value = val;
                    sc.entries.push_back(e);
                    lastExpr = val;
                }
            };
            expression* lastExpr = nullptr;
            parseNextEntry(lastExpr);
            while(lastExpr->terminator == ",")
                parseNextEntry(lastExpr);
        }
        sc.body = new statementBlock();
        functionDef caseCtx;
        if(func != nullptr){ caseCtx.returnType = func->returnType; caseCtx.params = func->params; }
        caseCtx.body = sc.body;
        while(true){
            token peek = file.peekToken();
            if(peek.is(token::braceClose) || peek.is("case") || peek.is("default")) break;
            token st = file.getToken();
            if(st.is("break")){ file.getToken(token::endStatement); continue; }
            processStatementDispatch(st, caseCtx);
        }
        swStmt.cases.push_back(&sc);
    }
    if(swStmt.needsIfChain) languageService.switchTempNeeded = true;
    if(body != nullptr) body->statements.push_back(&swStmt);
    return false;
}

bool bglParser::processTry(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'try' is not valid at global scope");
    if(beguilerSettings.target == "z3")
        parsingError("try/catch/throw requires Z-machine v5 or later (current target is Z3)");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    languageService.tryCatchNeeded = true;
    tryCatchStatement& tcStmt = *(new tryCatchStatement());
    tcStmt.id = languageService.tryCatchCounter++;
    tcStmt.src = stmtLoc;
    // Caller already consumed "try" "{"
    tcStmt.tryBody = new statementBlock();
    {
        functionDef tryCtx;
        if(func != nullptr){ tryCtx.returnType = func->returnType; tryCtx.params = func->params; }
        tryCtx.body = tcStmt.tryBody;
        openCompileContext(eCompileContext::codeBlock, tcStmt.tryBody);
        while(processNextStatement(tryCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    }
    token catchTok = file.getToken();
    if(!catchTok.is("catch"))
        parsingError("Expected 'catch' after try block");
    file.getToken(token::parenOpen);
    token catchType = file.getToken(eTokenType::dataType);
    token catchName = file.getToken(eTokenType::identifier);
    file.getToken(token::parenClose);
    tcStmt.catchVarType = (string)catchType;
    tcStmt.catchVarName = (string)catchName;
    file.getToken(token::braceOpen);
    tcStmt.catchBody = new statementBlock();
    {
        variableDeclaration& catchVar = *(new variableDeclaration());
        catchVar.name = tcStmt.catchVarName;
        catchVar.type = languageService.getType(tcStmt.catchVarType);
        statementBlock* funcBody = currentFunc ? dynamic_cast<statementBlock*>(currentFunc->body) : body;
        if(funcBody != nullptr) funcBody->statements.push_back(&catchVar);
        tcStmt.catchBody->statements.push_back(&catchVar);
        functionDef catchCtx;
        if(func != nullptr){ catchCtx.returnType = func->returnType; catchCtx.params = func->params; }
        catchCtx.body = tcStmt.catchBody;
        openCompileContext(eCompileContext::codeBlock, tcStmt.catchBody);
        while(processNextStatement(catchCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    }
    if(body != nullptr) body->statements.push_back(&tcStmt);
    return false;
}

// Single handler for all directive rules — delegates to the existing processDirective switch.
bool bglParser::processDirectiveDispatch(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    return processDirective(t[0], ctx);
}

bool bglParser::processFunc(vector<token>& t, Qualifiers& q, abstractObject& ctx) {
    // Caller already consumed "func" "<" — parse the remaining <..., ...> body
    token typeTok = t[0];  // the "func" keyword
    string result = "func<";
    bool first = true;
    while(true){
        token tt = file.getToken({eTokenType::dataType, eTokenType::identifier});
        string typeName = tt.value;
        if(typeName == "func") typeName = parseFuncType();  // nested func<>
        if(!first) result += ",";
        result += typeName;
        first = false;
        token sep = file.getToken();
        if(sep.is(">")) break;
    }
    result += ">";
    typeTok.value = result;

    // Now continue like the dataType branch — read name and dispatch
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    string objectClassName;
    if(file.peekToken().is(":")) {
        file.getToken();
        objectClassName = file.getToken(eTokenType::dataType).value;
    }
    string i6alias;
    if(file.peekToken().is("as")) {
        file.getToken();
        token aliasTok = file.getToken(eTokenType::identifier);
        i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    }
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, name, ctx, q.isExtern, q.isEmitter, q.isReplace);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, name, q.isExtern, objectClassName, i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, name, symbol, ctx, q.isExtern, q.isConst, i6alias);
}

bool bglParser::processThrow(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'throw' is not valid at global scope");
    if(beguilerSettings.target == "z3")
        parsingError("try/catch/throw requires Z-machine v5 or later (current target is Z3)");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    languageService.tryCatchNeeded = true;
    throwStatement& throwStmt = *(new throwStatement());
    throwStmt.src = stmtLoc;
    // Caller consumed "throw" — read expression up to ;
    token valTok = file.getToken();
    throwStmt.value = parseExpression(valTok, {token::endStatement}, func, body);
    if(body != nullptr) body->statements.push_back(&throwStmt);
    return false;
}

bool bglParser::processDelete(vector<token>& t, Qualifiers& q, abstractObject& ctx){
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'delete' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller consumed "delete" — read identifier (the variable holding the pool reference) and ;
    token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
    string varName = (string)nameTok;
    // Resolve the variable's type — must be a pooled class.
    string varTypeName = resolveIdentifierType(varName, func, body);
    if(varTypeName.empty())
        parsingError(format("'delete {0}': unknown variable", nameTok.originalValue.empty() ? varName : nameTok.originalValue));
    classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varTypeName));
    if(cls == nullptr || cls->poolSize == 0)
        parsingError(format("'delete {0}': '{1}' is not a pooled class. delete is only valid for instances of classes declared with `[N]` or `extern[]`.",
            nameTok.originalValue.empty() ? varName : nameTok.originalValue, varTypeName));
    file.getToken(token::endStatement);
    // Emit as `ClassName.destroy(varName);`
    string qualifiedVar = func != nullptr ? qualifyIdentifier(varName, func, body) : varName;
    if(qualifiedVar.empty()) qualifiedVar = varName;
    i6RawNode& node = *(new i6RawNode());
    node.text = cls->i6Name() + ".destroy(" + qualifiedVar + ");";
    node.src = stmtLoc;
    if(body != nullptr) body->statements.push_back(&node);
    return false;
}

bool bglParser::processNextStatement(abstractObject& contextObject) {
    if(!grammarInitialized) initGrammarTable();
    token tok = file.getToken();
    // Tolerate stray ';' between statements: treat as an empty statement.
    // This makes the trailing semicolon optional/repeatable on directives,
    // declarations, and code blocks (#include "x";  class Foo {...};  {...};)
    // without each handler needing its own "consume optional ;" logic.
    while(tok.is(token::endStatement)) tok = file.getToken();
    return processStatementDispatch(tok, contextObject);
}

// Dispatches a single statement given a pre-read token. Used by processNextStatement
// for the main loop, and by statement handlers (if/while/for/etc.) for unbraced single-
// statement bodies like `if(x) doSomething();`.
bool bglParser::processStatementDispatch(token tok, abstractObject& contextObject) {
    if(!grammarInitialized) initGrammarTable();
    sourceLocation stmtLoc = file.currentLocation();

    // Early exits
    if(tok.is(token::braceClose)) return true;
    if(tok.is(eTokenType::eof)) {
        if(getCurrentCompileContext() == eCompileContext::codeBlock)
            parsingError("Unexpected end of file — missing closing '}'");
        return true;
    }
    // Directives flow through the grammar table (each #xxx is a rule).

    // Parse qualifiers
    Qualifiers q = parseQualifiers(tok);
    if(q.isExplicit && getCurrentCompileContext() == eCompileContext::global)
        parsingError("'explicit' is only valid inside a class or object body");
    if(q.isStatic && getCurrentCompileContext() == eCompileContext::global)
        parsingError("'static' is only valid inside a class body");

    // Static member access: ClassName.member — route to processStatement
    // Object instances no longer need special handling here because they're lexed as identifiers.
    if(!q.isExtern && getCurrentCompileContext() != eCompileContext::global && tok.isDataType()) {
        if(file.peekToken(1).is(token::period) || file.peekToken(1).is("?.")) {
            processStatement(tok, contextObject);
            return false;
        }
    }

    // Qualifier-dependent patterns — must be checked before the grammar match
    // because the grammar matcher consumes tokens and can't put them back
    if(q.isEmitter && (tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)) && file.peekToken().is(token::braceOpen))
        return processClassDeclaration(tok, false, q.isExtend, true, false, tok);

    if(q.isExtend && !q.isExtern && (tok.is(eTokenType::identifier) || tok.isDataType()) && !tok.is(token::classDeclaration))
        return processObjectExtension(tok);

    if(!q.isExtern && tok.is("grammar")) return processGrammarDeclaration();

    // Namespace-scoped type path: identifier.identifier...identifier resolving to a type.
    // Consume the dotted path, resolve to a flat type, then re-dispatch with the synthetic token.
    if(tok.is(eTokenType::identifier) && file.peekToken().is(token::period) && isNamespacedTypePath(tok)){
        tok = consumeTypeToken(tok);
        // tok is now a synthetic dataType — fall through to grammar matching with the resolved type
    }

    // #using type import: if tok is an identifier that matches an alias member (isAlias=true)
    // of any imported scope, resolve it to the flat type name.
    // e.g., after `#using bgl.glulx;`, bare `window` resolves to `glulxWindow`.
    if(tok.is(eTokenType::identifier) && !tok.isDataType()){
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isAlias && vd->name == tok.value && languageService.isClassType(vd->type.name)){
                        tok.value = vd->type.name;
                        tok.tokenType = eTokenType::dataType;
                        goto usingResolved;
                    }
        for(classDef* imp : usingImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isAlias && vd->name == tok.value && languageService.isClassType(vd->type.name)){
                        tok.value = vd->type.name;
                        tok.tokenType = eTokenType::dataType;
                        goto usingResolved;
                    }
        usingResolved:;
    }

    // Try grammar-driven matching
    GrammarMatch match = matchGrammar(tok);

    if(match.success && match.handler) {
        return (this->*match.handler)(match.matchedTokens, q, contextObject);
    }

    // ── FALLTHROUGH: grammar match failed ──
    // All declaration, statement, and directive patterns are in the grammar table.
    // Anything reaching here is either an expression statement (code block) or an error (global).

    if(getCurrentCompileContext() == eCompileContext::global) {
        // Grammar-improved error: report what patterns came closest
        if(!match.failedCandidates.empty()) {
            auto best = max_element(match.failedCandidates.begin(), match.failedCandidates.end(),
                [](const GrammarMatch::FailedCandidate& a, const GrammarMatch::FailedCandidate& b) {
                    return a.matchedUpTo < b.matchedUpTo;
                });
            if(best->matchedUpTo > 0) {
                for(auto& rule : grammarRules) {
                    if(rule.name == best->ruleName && best->matchedUpTo < (int)rule.pattern.size()) {
                        string expected = describeExpected(rule.pattern[best->matchedUpTo]);
                        parsingError(format("Near '{0}': expected {1} (in {2})", (string)tok, expected, best->ruleName));
                    }
                }
            }
        }
        // Common shape: an identifier (not a type) followed by `=`, `;`, or a literal —
        // user wrote a variable declaration without a type. Give a targeted message instead
        // of the generic "Illegal global identifier" fallback.
        if(tok.is(eTokenType::identifier) && !tok.isDataType()) {
            token nextTok = file.peekToken(1);
            bool looksLikeDeclaration =
                   nextTok.is(token::endStatement)
                || nextTok.is(token::assignment)
                || nextTok.is(eTokenType::integer)
                || nextTok.is(eTokenType::quote)
                || nextTok.is(eTokenType::rawQuote)
                || nextTok.is(eTokenType::charLiteral);
            if(looksLikeDeclaration) {
                string name = tok.originalValue.empty() ? (string)tok : tok.originalValue;
                string example = q.isConst
                    ? "const int " + name + " = ...;"
                    : "int " + name + " = ...;";
                parsingError(format("Missing type before identifier '{0}'. Variable declarations require a type — e.g. `{1}`", name, example));
            }
        }
        parsingError(format("Illegal global identifier:'{0}'", (string)tok));
    }

    if(q.isExtern) parsingError(format("Extern declaration only valid for global variables, classes, routines, and enums:'{0}'", (string)tok));

    processStatement(tok, contextObject);
    return false;
}

//===============================================================================================================================
// Routines to parser larger blocks of the source code
//-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
#pragma region Parsing functions
// Returns true if the block contains a returnStatement on any path (used for lambda return-type inference).
static bool hasReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* is = dynamic_cast<ifStatement*>(s))
            if(hasReturn(is->thenBlock) || hasReturn(is->elseBlock)) return true;
        if(auto* ws = dynamic_cast<whileStatement*>(s))  if(hasReturn(ws->body))  return true;
        if(auto* ds = dynamic_cast<doStatement*>(s))     if(hasReturn(ds->body))  return true;
        if(auto* fs = dynamic_cast<forStatement*>(s))    if(hasReturn(fs->body))  return true;
        if(auto* sw = dynamic_cast<switchStatement*>(s))
            for(switchCase* c : sw->cases) if(hasReturn(c->body)) return true;
    }
    return false;
}

// Returns true if ALL execution paths through the block are guaranteed to return.
// Criteria:
//   - An unconditional returnStatement at the top level of the block
//   - An if-else where both the then-block and else-block all-paths-return
//   - A switch that has a default case and every case body all-paths-returns
// Loops (for/while/do) are NOT treated as guaranteed — the body may not execute.
static bool allPathsReturn(statementBlock* blk){
    if(blk == nullptr) return false;
    for(statement* s : blk->statements){
        if(dynamic_cast<returnStatement*>(s)) return true;
        if(auto* is = dynamic_cast<ifStatement*>(s)){
            if(is->elseBlock != nullptr &&
               allPathsReturn(is->thenBlock) && allPathsReturn(is->elseBlock))
                return true;
        }
        if(auto* sw = dynamic_cast<switchStatement*>(s)){
            bool hasDefault = false;
            bool allReturn = true;
            for(switchCase* c : sw->cases){
                if(c->entries.empty()) hasDefault = true;  // default: case
                if(!allPathsReturn(c->body)) allReturn = false;
            }
            if(hasDefault && allReturn) return true;
        }
        // Loops do not guarantee return — body may not execute
    }
    return false;
}

bool bglParser::processEnumDeclaration(token tok, bool isExternal, token nameOverride){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration)) isBnum=true;
    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType}); //enum name
    enumDef& newEnum=languageService.registerEnum((string)name, isExternal, name.originalValue);
    newEnum.isBnum = isBnum;
    if(!tok.docComment.empty())          newEnum.docComment = tok.docComment;
    else if(!name.docComment.empty())    newEnum.docComment = name.docComment;
    // Optional shared-base clause (mirrors pre-scanner); see bglPreScanner.cpp for semantics.
    if(file.peekToken().is(":")){
        file.getToken();
        token baseTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(!isBnum)
            parsingError(format("enum '{0}': shared-base inheritance is only valid for bnum declarations", newEnum.dName()));
        enumDef* base = dynamic_cast<enumDef*>(&languageService.getType(baseTok.value));
        if(!base || !base->isBnum)
            parsingError(format("bnum '{0}': base '{1}' is not a declared bnum", newEnum.dName(), baseTok.originalValue));
        newEnum.baseBnum = base;
    }
    bool hasBase = newEnum.baseBnum != nullptr;
    tok=file.getToken(token::braceOpen);
    // If the pre-scanner already populated values, consume the body and return
    bool alreadyPopulated = !newEnum.namedValues.empty();
    int val=1;
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside enum — missing closing '}'");
        tok=file.getToken();
        if(tok.is(token::braceClose)) break;
        if(alreadyPopulated){
            // Just drain remaining tokens until closing brace
            while(tok.isNot(token::braceClose) && tok.isNot(eTokenType::eof)) tok=file.getToken();
            break;
        }
        enumValueDef& newVal=*new enumValueDef();
        newVal.name=tok.value;
        newVal.displayName=tok.originalValue;
        newVal.docComment=tok.docComment;  // doc-comment attached to the value's name token
        tok=file.getToken({token::braceClose, token::comma, token::assignment});
        if(tok.is(token::assignment)){
            bool negate = false;
            if(file.peekToken().is("-")){ file.getToken(); negate = true; }
            token numTok=file.getToken(eTokenType::integer);
            val=stoi(numTok.value);
            if(negate) val = -val;
            if(isBnum && negate)
                parsingError(format("bnum '{0}': negative value {1} is not allowed", newEnum.dName(), val));
            if(isBnum && !hasBase && val != 0 && (val & (val - 1)) != 0)
                parsingError(format("bnum '{0}': explicit value {1} is not a power of 2", newEnum.dName(), val));
            tok=file.getToken({token::braceClose, token::comma});
        }
        newVal.value=val;
        if(isBnum)
            val<<=1;
        else
            val++;
        newEnum.namedValues.push_back(&newVal);
    }
    return false;
}

bool bglParser::processClassDeclaration(token tok, bool isExternal, bool isExtend, bool isEmitterClass, bool isAlias, token nameOverride){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Class declarations are only allowed in global context:'{0}'", (string) tok));

    token nameTok = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType});
    classDef* classPtr = nullptr;
    if(isExtend){
        classPtr = dynamic_cast<classDef*>(&languageService.getType((string)nameTok));
        if(classPtr == nullptr) parsingError(format("extend class '{0}': no previously defined class with that name", (string)nameTok));
    } else {
        //create an empty class definition object and register it immediately, so that we can refer to this type within its own definition (e.g. comparison operators)
        classPtr = &languageService.registerClass((string)nameTok, isExternal, nameTok.originalValue);
    }
    classDef& newClass = *classPtr;
    // Doc-comment: prefer the doc on the leading `class` keyword (or the first qualifier
    // token forwarded through parseQualifiers); fall back to anything attached to the name token.
    if(!tok.docComment.empty())            newClass.docComment = tok.docComment;
    else if(!nameTok.docComment.empty())   newClass.docComment = nameTok.docComment;

    if(!isExtend){
        if(isEmitterClass) newClass.isEmitterClass = true;
        if(isAlias)        newClass.isAlias = true;
    }

    classDef* savedClass = currentClass;
    currentClass = &newClass;
    openCompileContext(eCompileContext::objectDef);

    // Type parameter clause (optional, before pool/inheritance):
    //   `class Foo<T> { … }` — declares T as a type parameter scoped to the class body.
    //   Parameters are stored on the classDef but NOT registered globally — collisions
    //   with same-named instances (e.g. `Temperature t;`) would otherwise occur. Member
    //   signatures parse T as an identifier-typed token; substitution at method-lookup
    //   time replaces T with the use-site binding before any type query runs.
    //   Skipped on `extend class Foo<…>` — type parameters are part of the original decl.
    if(file.peekToken().is("<")){
        if(isExtend) parsingError(format("extend class '{0}': type parameters cannot be added by extend — they are part of the original declaration", (string)nameTok));
        if(isAlias)  parsingError(format("alias class '{0}': type parameters are not supported on alias classes", (string)nameTok));
        file.getToken(); // consume '<'
        while(true){
            token paramTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            // Don't double-add when claiming a pre-pass stub that already populated this list.
            bool already = false;
            for(const string& tp : newClass.typeParameters) if(tp == paramTok.value){ already = true; break; }
            if(!already) newClass.typeParameters.push_back(paramTok.value);
            token sep = file.getToken({token::comma, ">"});
            if(sep.value == ">") break;
        }
    }

    // Pool size clause (optional, before any inheritance):
    //   `class Foo[N]`              — sized pool, N statically-allocated instances (emit `Class Foo(N)`)
    //   `extern class Foo[]`        — marker: I6-pooled type, size opaque to Beguile
    //   `class Foo[N]` on emitter/alias — error (no I6 backing / type aliasing)
    //   `extern class Foo[N]`       — error (size belongs to I6)
    //   `extend class Foo[...]`     — error (pool size is part of original declaration)
    tok = file.getToken();
    if(tok.is(token::bracketOpen)){
        token inner = file.getToken();
        if(inner.is(token::bracketClose)){
            // `[]` empty marker form
            if(!isExternal)
                parsingError(format("class '{0}': empty pool brackets '[]' are only valid on `extern class` (marker for an I6-defined pooled type)", (string)nameTok));
            if(isExtend)
                parsingError(format("extend class '{0}': pool brackets cannot be added by extend — pool status is part of the original declaration", (string)nameTok));
            newClass.poolSize = -1; // extern marker
        } else if(inner.is(eTokenType::integer)){
            // `[N]` sized form
            if(isExternal)
                parsingError(format("extern class '{0}': pool size cannot be specified — extern declarations describe I6-defined types, and the I6 declaration owns the pool size. Use 'extern class {0}[]' as a marker that the type is pooled, or omit the brackets.", (string)nameTok));
            if(isExtend)
                parsingError(format("extend class '{0}': pool size cannot be modified — pool size is part of the original declaration's contract", (string)nameTok));
            if(isEmitterClass)
                parsingError(format("emitter class '{0}': pool size is not valid (emitter classes have no I6 backing for instances)", (string)nameTok));
            if(isAlias)
                parsingError(format("alias class '{0}': pool size is not valid (alias classes dissolve to another type)", (string)nameTok));
            int n = stoi(inner.value);
            if(n <= 0)
                parsingError(format("class '{0}': pool size must be a positive integer (got {1})", (string)nameTok, n));
            newClass.poolSize = n;
            file.getToken(token::bracketClose);
        } else {
            parsingError(format("class '{0}': expected integer or ']' after '[' in pool clause, got '{1}'", (string)nameTok, (string)inner));
        }
        tok = file.getToken();
    }

    // Inheritance clause:
    //   alias class requires 'for Parent'
    //   other classes use optional ': Parent [, Parent2 ...]'
    if(isAlias && !tok.is("for"))
        parsingError(format("'alias {0}' requires a parent class: use 'alias {0} for ParentClass'", (string)nameTok));
    // Detect circular inheritance: walking `parent`'s ancestry must not reach newClass.
    auto checkInheritanceCycle = [&](classDef* parent, const string& parentDisplay){
        function<bool(classDef*, set<classDef*>&)> reachesSelf = [&](classDef* c, set<classDef*>& visited) -> bool {
            if(!c || !visited.insert(c).second) return false;
            if(c == &newClass) return true;
            for(classDef* b : c->baseClasses) if(reachesSelf(b, visited)) return true;
            return false;
        };
        set<classDef*> visited;
        if(reachesSelf(parent, visited))
            parsingError(format("class '{0}': circular inheritance — '{1}' transitively inherits from '{0}'",
                                newClass.dName(), parentDisplay));
    };
    if(tok.is("for")){
        // alias class single-parent clause
        token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
        classDef* parent = dynamic_cast<classDef*>(&languageService.getType(parentTok.value));
        if(!parent) parsingError(format("Unknown base class '{0}'", parentTok.value));
        else { checkInheritanceCycle(parent, parentTok.originalValue); newClass.baseClasses.push_back(parent); }
        tok = file.getToken();
    } else if(tok.is(":")){
        do {
            token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            classDef* parent = dynamic_cast<classDef*>(&languageService.getType(parentTok.value));
            if(!parent) parsingError(format("Unknown base class '{0}'", parentTok.value));
            else { checkInheritanceCycle(parent, parentTok.originalValue); newClass.baseClasses.push_back(parent); }
            tok = file.getToken();
        } while(tok.is(","));
    }
    // Marker-form extern pool class (`extern class Foo[];`) allows no body — it's purely a
    // declaration that the type exists and is pooled in I6. Skip body parsing and return.
    if(isExternal && newClass.poolSize == -1 && tok.is(token::endStatement)){
        closeCompileContext(eCompileContext::objectDef);
        currentClass = savedClass;
        return false;
    }
    tok.assert(token::braceOpen);
    tok=file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof))
            parsingError(format("Unexpected end of file inside class '{0}' — missing closing '}}'", newClass.dName()));
        Qualifiers q = parseQualifiers(tok);
        bool isEmitter = q.isEmitter;
        bool isReplace = q.isReplace;
        bool isExplicitConversion = q.isExplicit;
        bool isMemberConst = q.isConst;
        bool isMemberStatic = q.isStatic;
        bool isOperator = false;
        token returnType;
        token name;
        // emitter class: 'emitter' keyword is optional — all members are implicitly emitters
        if(newClass.isEmitterClass && !isEmitter) isEmitter = true;
        // Context-specific validation
        if(q.isExtern && !isExtend)
            parsingError("'extern' is not valid inside a class body (use on the class declaration itself)");
        if(q.isExtend)
            parsingError("'extend' is not valid inside a class body");
        if(q.isAlias)
            parsingError("'alias' is not valid inside a class body");
        // array<T> member: same handler as object bodies (refactored to take members vector).
        // Class is never verb-derived, so no grammarRule downcast applies.
        if(tok.is("array") && file.peekToken().is("<")){
            processArrayMember(newClass.members, newClass.dName(), nullptr, &newClass, &q);
            tok = file.getToken();
            continue;
        }
        // Check for inherited member type inference: if the token is an identifier (not a data type)
        // followed by '=' or ';', look up the member name in base classes to infer the type.
        if(tok.is(eTokenType::identifier) && !tok.isDataType()){
            token peek = file.peekToken(1);
            if(peek.is(token::assignment) || peek.is(token::endStatement)){
                string memberName = tok.value;
                string inferredType;
                // Search base classes for a member with this name. For arrayDeclaration members,
                // reconstruct the full templated typeName (`array<dictionaryWord>`) so downstream
                // element-type validation has the T to check against — `vd->type.name` alone is
                // just `array` and loses the elementType.
                std::function<void(classDef*)> searchBases = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == memberName){
                                if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
                                    inferredType = "array<" + arr->elementType + ">";
                                else
                                    inferredType = vd->type.name;
                                return;
                            }
                    for(classDef* base : c->baseClasses){ searchBases(base); if(!inferredType.empty()) return; }
                };
                for(classDef* base : newClass.baseClasses){ searchBases(base); if(!inferredType.empty()) break; }
                if(!inferredType.empty()){
                    // Inferred: treat tok as the member name, use the inherited type
                    returnType.value = inferredType;
                    returnType.tokenType = eTokenType::dataType;
                    name = tok;
                } else {
                    tok.assertDataType(); // will error with a meaningful message
                    returnType = tok;
                    name = file.getToken({eTokenType::identifier, eTokenType::dataType});
                }
            } else {
                tok.assertDataType();
                returnType = tok;
                name = file.getToken({eTokenType::identifier, eTokenType::dataType});
            }
        } else {
            returnType=tok.assertDataType();
            name=file.getToken({eTokenType::identifier, eTokenType::dataType});
        }
        if(name.is("operator")){
            isOperator=true;
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)){
                // conversion operator: emitter <type> operator()
                name.value = "operator()";
                tok = opTok; // the ( is already consumed; reuse it
            } else if(opTok.is(token::bracketOpen)){
                // subscript operator: operator[] (read) or operator[]= (write)
                file.getToken(token::bracketClose);
                token maybeAssign = file.getToken();
                if(maybeAssign.is(token::assignment)){
                    name.value = "[]=";
                    tok = file.getToken(eTokenType::symbol);
                } else {
                    name.value = "[]";
                    tok = maybeAssign;  // already consumed the next symbol
                }
            } else if(opTok.is("switch")){
                // switch comparison operator: operator switch(type v)
                name.value = "switch";
                tok = file.getToken(eTokenType::symbol);
            } else if(opTok.is("auto")){
                // auto inference operator: operator auto() — return type only, no body
                name.value = "auto";
                tok = file.getToken(eTokenType::symbol);  // should be ( or ;
            } else if(opTok.is(eTokenType::identifier)){
                // qualified operator: e.g. "prefix++" — read qualifier then oper symbol
                token opSym = file.getToken(eTokenType::oper);
                name.value = opTok.value + opSym.value;  // e.g. "prefix++"
                tok = file.getToken(eTokenType::symbol);
            } else if(opTok.is("?")){
                // unary query operator: operator ?()
                name.value = "?";
                tok = file.getToken(eTokenType::symbol);
            } else {
                opTok.assert(eTokenType::oper);
                name = opTok;
                tok=file.getToken(eTokenType::symbol);
            }
        } else {
            tok=file.getToken({eTokenType::symbol, eTokenType::oper});
        }

        if(tok.is(token::parenOpen))  { //this is a function
            functionDef& funcDef=*(new functionDef());
            funcDef.name=(string) name; funcDef.displayName=name.originalValue;
            funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
            funcDef.returnType=languageService.getType((string) returnType);
            funcDef.isEmitter=isEmitter;
            funcDef.isExplicit=isExplicitConversion;
            funcDef.isDefault=q.isDefault;
            if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
            else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
            if(isExplicitConversion && funcDef.name != "operator()")
                parsingError("'explicit' is only valid on conversion operators (operator())");
            processParameterList(funcDef);
            // operator auto: no params, no body, max one per class
            if(funcDef.name == "auto"){
                if(!funcDef.params.empty())
                    parsingError("operator auto() cannot have parameters");
                // Check for duplicate (ignore pre-pass stubs, which the full pass replaces)
                for(typeMember* m : newClass.members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == "auto" && !fd->isPrePassStub)
                            parsingError(format("class '{0}' already has an operator auto()", newClass.dName()));
                // Must be followed by ; (no body)
                token afterAuto = file.getToken();
                if(!afterAuto.is(token::endStatement))
                    parsingError("operator auto() cannot have a body — declare return type only");
                newClass.members.push_back(&funcDef);
                tok = file.getToken();
                continue;
            }
            if(isEmitter && !funcDef.params.empty() && (funcDef.name == "init" || funcDef.name == "deinit"))
                parsingError(format("Emitter '{0}' cannot accept parameters", funcDef.name));
            if(!isEmitter && (funcDef.name == "switch" || funcDef.name == "?"))
                parsingError(format("operator {0}() must be declared as an emitter", funcDef.name));
            if(!isEmitter && (funcDef.name == "init" || funcDef.name == "deinit"))
                parsingError(format("'{0}' must be declared as an emitter", funcDef.name));
            if((isExternal || newClass.isExternal || newClass.isAlias) && !isEmitter){
                // extern/alias class non-emitter methods not allowed
                parsingError(format("Non-emitter function '{0}' is not allowed in an extern or alias class", funcDef.name));
            } else if(funcDef.isEmitter && file.peekToken().is(token::endStatement)){
                // Semicolon-terminated emitter: pass-through (value unchanged)
                file.getToken(); // consume ';'
                i6Block& rawblock=*(new i6Block());
                rawblock.i6Body=" $self";
                funcDef.body=&rawblock;
            } else {
                file.getToken(token::braceOpen); //consume the open brace;
                if(funcDef.isEmitter){
                    i6Block& rawblock=*(new i6Block());
                    rawblock.i6Body=file.getRawTextThroughClosingBrace();
                    funcDef.body=&rawblock;
                } else {
                    funcDef.body = new statementBlock();
                    functionDef* savedFunc = currentFunc;
                    currentFunc = &funcDef;
                    openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
                    while(processNextStatement(funcDef) == false){}
                    closeCompileContext(eCompileContext::codeBlock);
                    currentFunc = savedFunc;
                    if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
                        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
                }
            }
            if(isExtend){
                // find an existing member with the same name and parameter signature
                // Pre-scan stubs match by name only (they have no params)
                typeMember* existing = nullptr;
                bool alreadyReplaced = false;
                for(typeMember* m : newClass.members){
                    functionDef* fd = dynamic_cast<functionDef*>(m);
                    if(!fd || fd->name != funcDef.name) continue;
                    if(fd->isPrePassStub){ existing = m; break; }
                    if(fd->params.size() == funcDef.params.size()){
                        bool match = true;
                        for(size_t i=0; i<funcDef.params.size(); i++){
                            if(fd->params[i]->type.name != funcDef.params[i]->type.name){ match=false; break; }
                        }
                        // operator(), operator[], and operator[]= can have multiple overloads with
                        // different return types; mirror the rule from the primary-class path so
                        // extend blocks can add conversion operators alongside existing ones.
                        if(match && fd->returnType.name != funcDef.returnType.name &&
                               (funcDef.name == "operator()" || funcDef.name == "[]" || funcDef.name == "[]="))
                            match = false;
                        if(match){ existing = m; break; }
                    }
                }
                // If the existing member is a pre-scan stub, replace it silently
                if(existing){
                    auto* existFd = dynamic_cast<functionDef*>(existing);
                    if(existFd && existFd->isPrePassStub){
                        for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                            if(*it==existing){ *it = &funcDef; break; }
                        alreadyReplaced = true;
                    }
                }
                if(existing && !alreadyReplaced && !isReplace)
                    parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.dName(), funcDef.dName()));
                if(!existing && isReplace)
                if(existing && !alreadyReplaced){
                    for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                        if(*it==existing){ *it = &funcDef; break; }
                    alreadyReplaced = true;
                }
                if(alreadyReplaced){ tok = file.getToken(); continue; }
            }
            if(funcDef.isEmitter && funcDef.name == "_bglglobaldeclaration"){
                // declaration emitter — store raw body on the class, not as a member
                i6Block* body = dynamic_cast<i6Block*>(funcDef.body);
                if(body) newClass.globalDeclarationBody = body->i6Body;
            } else {
                if(!isExtend){
                    // Silently replace pre-scan stubs (may have been seeded by an 'extend class' that
                    // appears later in the file — pre-scan pushes stubs into this class's member list).
                    if(replaceStubMember(newClass.members, funcDef)) { tok = file.getToken(); continue; }
                    bool replacedExisting = false;
                    for(size_t i = 0; i < newClass.members.size(); i++){
                        functionDef* fd = dynamic_cast<functionDef*>(newClass.members[i]);
                        if(fd && fd->name == funcDef.name && fd->params.size() == funcDef.params.size()){
                            bool match = true;
                            for(size_t j=0; j<funcDef.params.size(); j++)
                                if(fd->params[j]->type.name != funcDef.params[j]->type.name){ match=false; break; }
                            // operator(), operator[], and operator[]= can have multiple overloads with
                            // different return types. operator() uses them for conversion operators;
                            // operator[] / operator[]= use them for element-type-aware array subscripts
                            // so the library can declare one overload per supported element type.
                            if(match && fd->returnType.name != funcDef.returnType.name &&
                                   (funcDef.name == "operator()" || funcDef.name == "[]" || funcDef.name == "[]="))
                                match = false;
                            if(match){
                                if(isReplace){
                                    newClass.members[i] = &funcDef;
                                    replacedExisting = true;
                                    break;
                                }
                                parsingError(format("class '{0}': method '{1}' with the same signature is already defined (originally at {2}:{3}); use 'replace' to override",
                                    newClass.dName(), funcDef.dName(), fd->src.file, fd->src.line));
                            }
                        }
                    }
                    if(replacedExisting) { tok = file.getToken(); continue; }
                    // Check base class hierarchy for shadowed methods — warn if 'replace' not specified
                    if(!isReplace && !funcDef.name.empty()){
                        string shadowedFrom;
                        bool shadowedIsDefault = false;
                        function<void(classDef*)> searchBases = [&](classDef* c){
                            if(!shadowedFrom.empty()) return;
                            for(typeMember* m : c->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == funcDef.name){ shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault; return; }
                            for(classDef* base : c->baseClasses) searchBases(base);
                        };
                        for(classDef* base : newClass.baseClasses) searchBases(base);
                        if(!shadowedFrom.empty() && !shadowedIsDefault)
                            parsingWarning(format("class '{0}': method '{1}' shadows definition in base class '{2}'; use 'replace' to suppress this warning",
                                newClass.dName(), funcDef.dName(), shadowedFrom));
                    }
                }
                newClass.members.push_back(&funcDef);
            }
        }
        else if(tok.is(token::braceOpen) && isEmitter && !isOperator){
            // Emitter value in class body: emitter Type name { body }
            functionDef& funcDef = *(new functionDef());
            funcDef.name = (string)name; funcDef.displayName = name.originalValue;
            funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
            funcDef.returnType = languageService.getType((string)returnType);
            funcDef.isEmitter = true;
            funcDef.isValueEmitter = true;
            funcDef.isDefault = q.isDefault;
            funcDef.src = file.currentLocation();
            i6Block& rawblock = *(new i6Block());
            rawblock.i6Body = file.getRawTextThroughClosingBrace();
            funcDef.body = &rawblock;
            if(!replaceStubMember(newClass.members, funcDef))
                newClass.members.push_back(&funcDef);
        }
        else{
            if(isOperator==true) parsingError("Operators must be functions.");
            // Emitter class: allow alias members (typed reference to another class) but not variable declarations
            if(isEmitter || newClass.isEmitterClass){
                string aliasTypeName = (string)returnType;
                // auto inference: require = classReference to infer type
                if(aliasTypeName == "auto"){
                    if(!tok.is(token::assignment))
                        parsingError(format("'auto' alias member '{0}' requires an initializer to infer the type", (string)name));
                    token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
                    classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhs.value));
                    if(!rhsCls)
                        parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                            (string)name, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
                    aliasTypeName = rhs.value;
                    tok = file.getToken();  // consume ;
                }
                classDef* aliasCls = dynamic_cast<classDef*>(&languageService.getType(aliasTypeName));
                if(!aliasCls)
                    parsingError(format("Emitter class '{0}' only supports emitter functions, emitter values, and class alias members; '{1}' is not a class",
                        newClass.dName(), aliasTypeName));
                // Alias member: register as a variableDeclaration with the class type
                // No I6 backing — resolved at compile time for dot-access
                variableDeclaration& aliasDef = *(new variableDeclaration());
                aliasDef.name = (string)name;
                aliasDef.type = languageService.getType(aliasTypeName);
                aliasDef.isExternal = true;  // no I6 emission
                if(tok.is(token::assignment) && aliasTypeName != "auto")
                    parsingError(format("Alias member '{0}' on emitter class cannot have an initializer; use 'auto' to infer type", (string)name));
                // Consume ; if present
                if(tok.isNot(token::endStatement))
                    parsingError(format("Expected ';' after alias member '{0}'", (string)name));
                newClass.members.push_back(&aliasDef);
                tok = file.getToken();
                continue;
            }
            if(tok.isNot(token::endStatement) && tok.isNot(token::assignment))
                parsingError(format("Expected '=' or ';' after member '{0}'", (string)name));
            // Note: alias-class members may carry a default value (e.g. `int priority = 10;` on
            // `class verb`). The value is a compile-time default consulted by the emitter when
            // lifting the field from an instance body; it is not emitted as an I6 property.
            variableDeclaration& varDef=*(new variableDeclaration());
            varDef.name=(string) name;
            varDef.displayName = name.originalValue;
            varDef.src = name.src.line > 0 ? name.src : file.currentLocation();
            varDef.type=languageService.getType((string) returnType);
            if(isMemberConst) varDef.isConst = true;
            varDef.isStatic = isMemberStatic;
            if(!returnType.docComment.empty())   varDef.docComment = returnType.docComment;
            else if(!name.docComment.empty())    varDef.docComment = name.docComment;
            if(tok.is(token::assignment)){
                token first = file.getToken();
                if(first.is(token::braceOpen)){
                    // initializer list: { expr, expr, ... } with optional nesting
                    initializerList* list = new initializerList();
                    token t2 = file.getToken();
                    while(!t2.is(token::braceClose) && !t2.is(eTokenType::eof)){
                        if(t2.is(token::braceOpen)){
                            initializerList* inner = new initializerList();
                            token t3 = file.getToken();
                            while(!t3.is(token::braceClose) && !t3.is(eTokenType::eof)){
                                expression* elem = parseExpression(t3, {",", token::braceClose}, nullptr, nullptr);
                                inner->elements.push_back(elem);
                                if(elem->terminator == token::braceClose) break;
                                t3 = file.getToken();
                            }
                            list->elements.push_back(inner);
                            t2 = file.getToken({token::comma, token::braceClose});
                            if(t2.is(token::braceClose)) break;
                            t2 = file.getToken();
                            continue;
                        }
                        expression* elem = parseExpression(t2, {",", token::braceClose}, nullptr, nullptr);
                        list->elements.push_back(elem);
                        if(elem->terminator == token::braceClose) break;
                        t2 = file.getToken();
                    }
                    file.getToken(token::endStatement);
                    // Element-type validation for inherited array members reassigned in this
                    // class body. Mirrors parsePropertyValue's check — `returnType` here is the
                    // full templated form (`array<dictionaryWord>`) when the inferred base member
                    // is an arrayDeclaration, so we can extract T and reject sibling/supertype
                    // values (e.g. `name = {.gadget, noun}` where `noun` is a grammarToken).
                    {
                        string rtStr = (string)returnType;
                        string expectedElemType;
                        if(rtStr.size() > 6 && rtStr.substr(0, 6) == "array<" && rtStr.back() == '>')
                            expectedElemType = rtStr.substr(6, rtStr.size() - 7);
                        if(!expectedElemType.empty())
                            for(size_t i = 0; i < list->elements.size(); i++){
                                expression* elem = list->elements[i];
                                if(elem->resolvedType.empty())
                                    parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                                else if(!isTypeCompatible(elem->resolvedType, expectedElemType))
                                    parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
                            }
                    }
                    varDef.declaredExpressionValue = list;
                } else {
                    varDef.declaredExpressionValue = parseExpression(first, {token::endStatement}, nullptr, nullptr);
                }
            }
            if(isExtend){
                typeMember* existing = nullptr;
                for(typeMember* m : newClass.members){
                    variableDeclaration* vd = dynamic_cast<variableDeclaration*>(m);
                    if(vd && vd->name == varDef.name){ existing = m; break; }
                }
                if(existing && !isReplace)
                    parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.dName(), varDef.dName()));
                if(!existing && isReplace)
                if(existing)
                    for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                        if(*it==existing){ newClass.members.erase(it); break; }
            } else {
                // Check for an existing stub from pre-scan (static member variables) and replace it.
                bool replacedStub = false;
                for(size_t i = 0; i < newClass.members.size(); i++){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(newClass.members[i])){
                        if(vd->name == varDef.name){
                            if(vd->isPrePassStub){
                                newClass.members[i] = (typeMember*)&varDef;
                                replacedStub = true;
                            } else {
                                parsingError(format("class '{0}': member '{1}' is already defined", newClass.dName(), varDef.dName()));
                            }
                            break;
                        }
                    }
                }
                if(!replacedStub) newClass.members.push_back((typeMember*)&varDef);
                tok=file.getToken();
                continue;
            }
            newClass.members.push_back((typeMember*)&varDef);
        }
        tok=file.getToken();
    }   
    //if...
    //file.getToken("emitter");
    //token retval=file.getToken(eTokenType::dataType);

    // Pool class validation — enforce single create/destroy and signature rules.
    // I6's create/destroy mechanism invokes a SINGLE property of each name; multiple
    // overloads would collide in I6. v1 restricts pool classes to one create and one destroy.
    if(newClass.poolSize != 0){
        functionDef* createFn = nullptr;
        functionDef* destroyFn = nullptr;
        for(typeMember* m : newClass.members){
            auto* fd = dynamic_cast<functionDef*>(m);
            if(!fd) continue;
            if(fd->name == "create"){
                if(createFn != nullptr)
                    parsingError(format("class '{0}': only one 'create' method is allowed on a pooled class (I6's create veneer dispatches to a single property)", newClass.dName()));
                createFn = fd;
            } else if(fd->name == "destroy"){
                if(destroyFn != nullptr)
                    parsingError(format("class '{0}': only one 'destroy' method is allowed on a pooled class", newClass.dName()));
                destroyFn = fd;
            }
        }
        if(createFn != nullptr){
            if(createFn->returnType.name != "void")
                parsingError(format("class '{0}': 'create' must have void return type — I6 ignores the return value", newClass.dName()));
            if(createFn->params.size() > 3)
                parsingError(format("class '{0}': 'create' is limited to 3 parameters on Z-machine (I6's class-message veneer caps it). Got {1}.", newClass.dName(), createFn->params.size()));
        }
        if(destroyFn != nullptr){
            if(destroyFn->returnType.name != "void")
                parsingError(format("class '{0}': 'destroy' must have void return type — I6 ignores the return value", newClass.dName()));
            if(!destroyFn->params.empty())
                parsingError(format("class '{0}': 'destroy' must take no parameters", newClass.dName()));
        }
    }

    currentClass = savedClass;
    closeCompileContext(eCompileContext::objectDef);
    return false;
}
// Reads <ReturnType, ParamType, ...> from stream and returns the full "func<...>" type string.
// Called after the "func" token has been consumed.
string bglParser::parseFuncType(){
    file.getToken("<");
    string result = "func<";
    bool first = true;
    while(true){
        token t = file.getToken({eTokenType::dataType, eTokenType::identifier});
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        if(!first) result += ",";
        result += typeName;
        first = false;
        token sep = file.getToken();
        if(sep.is(">")) break;
        // else sep is "," — continue
    }
    result += ">";
    return result;
}

// Parse a lambda expression. The opening '(' has already been consumed.
// Builds a lifted global functionDef, adds it to languageService.globals, returns its name.
string bglParser::parseLambdaExpr(functionDef* outerFunc, statementBlock* outerBody){
    functionDef& fd = *(new functionDef());
    fd.name = format("_bglLambda_{0}", lambdaCounter++);
    fd.isEmitter = false;
    fd.isExternal = false;
    fd.src = file.currentLocation();

    // Parse parameter list: (type name, type name, ...) or ()
    token t = file.getToken(); // first type token or ')'
    while(t.isNot(token::parenClose)){
        paramDef& p = *(new paramDef());
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        p.type = languageService.getType(typeName);
        if(p.type.name.empty()) p.type.name = typeName; // for func<...> or unknown types
        token nameTok = file.getToken(eTokenType::identifier);
        p.name = nameTok.value;
        fd.params.push_back(&p);
        t = file.getToken(); // ',' or ')'
        if(t.is(",")) t = file.getToken(); // advance to next type
    }

    // Consume =>
    file.getToken("=>");

    statementBlock* lambdaBody = new statementBlock();
    fd.body = lambdaBody;

    // Set up outer scope context for capture detection.
    // Use currentFunc (the real enclosing function) rather than outerFunc (which may be a
    // synthetic context created by for/while/if body processing). currentFunc's body contains
    // all locals at every nesting level; outerBody is the immediate enclosing block.
    functionDef* savedOuterFunc = lambdaOuterFunc;
    statementBlock* savedOuterBody = lambdaOuterBody;
    lambdaOuterFunc = currentFunc ? currentFunc : outerFunc;
    lambdaOuterBody = outerBody;
    // Push the outer function onto the stack so nested lambdas can chain through multiple levels
    if(lambdaOuterFunc != nullptr) lambdaOuterFuncStack.push_back(lambdaOuterFunc);
    // Save and clear the activeBlockStack — the lambda is a new function scope.
    // Outer-function locals should go through Tier 7 (capture) not Tier 1c (bare name).
    vector<statementBlock*> savedBlockStack = activeBlockStack;
    activeBlockStack.clear();

    token bodyStart = file.getToken();
    if(bodyStart.is(token::braceOpen)){
        // Block body
        functionDef* savedFunc = currentFunc;
        currentFunc = &fd;
        openCompileContext(eCompileContext::codeBlock, lambdaBody);
        while(processNextStatement(fd) == false){}
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        fd.returnType.name = hasReturn(lambdaBody) ? "var" : "void";
    } else {
        // Single-expression body. Terminates at ';' (assignment RHS), ',' (next function
        // argument), or ')' (end of enclosing argument list). parseExpression's paren
        // tracking ensures inner `,`/`)` inside the body don't terminate prematurely.
        // For arg-position lambdas, the `,` or `)` terminator must be visible to the
        // enclosing arg parser — we stash it on the parser-level token slot, which the
        // caller's next getNext() will pick up before reading more from the lexer.
        expression* retExpr = parseExpression(bodyStart, {token::endStatement, token::comma, token::parenClose}, &fd, lambdaBody);
        if(retExpr->terminator == "," || retExpr->terminator == ")"){
            token t;
            t.value = retExpr->terminator;
            t.tokenType = eTokenType::symbol;
            stashedToken = t;
        }
        returnStatement& ret = *(new returnStatement());
        ret.src = fd.src;
        ret.returnExpression = retExpr->text();
        lambdaBody->statements.push_back(&ret);
        fd.returnType.name = retExpr->resolvedType.empty() ? "var" : retExpr->resolvedType;
    }

    // Restore outer scope context
    lambdaOuterFunc = savedOuterFunc;
    lambdaOuterBody = savedOuterBody;
    activeBlockStack = savedBlockStack;
    if(!lambdaOuterFuncStack.empty()) lambdaOuterFuncStack.pop_back();

    // Emit capture globals — insert at front so they appear before functions in the I6 output.
    // If `self` was captured, inject `self = _bglCapN;` at the top of the lambda body so
    // all self.member references inside the lambda work via I6's assignable self pseudo-variable.
    for(auto& cap : fd.captures){
        variableDeclaration& capGlobal = *(new variableDeclaration());
        capGlobal.name = cap.globalName;
        capGlobal.type.name = cap.typeName;
        languageService.globals.insert(languageService.globals.begin(), &capGlobal);
        if(cap.outerName == "self"){
            i6RawNode* selfAssign = new i6RawNode();
            selfAssign->text = "self = " + cap.globalName + ";";
            lambdaBody->statements.insert(lambdaBody->statements.begin(), selfAssign);
        }
    }

    // Lift: append to globals — I6 doesn't require routines to precede call sites,
    // and inserting at front would place lambdas before _bgl_temp (I6 compile error)
    languageService.globals.push_back(&fd);

    // If there are captures, emit load/unload assignments around the enclosing statement.
    // Load (pendingInjections): copy locals → globals BEFORE the lambda is used.
    // Unload (postInjections): copy globals → locals AFTER the call returns, so
    // modifications inside the lambda are visible to the enclosing scope.
    // For stored lambdas (declaration, not immediate call), the unload is a no-op
    // since the lambda hasn't been called yet — the globals become canonical storage.
    for(auto& cap : fd.captures){
        i6RawNode& load = *(new i6RawNode());
        load.text = cap.globalName + " = " + cap.outerName + ";";
        pendingInjections.push_back(&load);
        i6RawNode& unload = *(new i6RawNode());
        unload.text = cap.outerName + " = " + cap.globalName + ";";
        postInjections.push_back(&unload);
    }

    return fd.name;
}

bool bglParser::processParameterList(functionDef& funcDef){
    token tok=file.getToken(); // first type, or ")" for empty list
    while(tok.isNot(token::parenClose)){
        paramDef& param=*new paramDef();
        param.docComment = tok.docComment;  // doc-comment attached to this param's leading type token
        tok = consumeTypeToken(tok);
        if(!tok.is(eTokenType::dataType))
            tok.assertDataType(); // original error path for non-type tokens
        string paramTypeName = tok.value;
        if(paramTypeName == "func") paramTypeName = parseFuncType();
        else if(paramTypeName == "array") {
            // consume <ElementType> and store full "array<ElementType>" for for-in/type-compat resolution
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            // array<char> maps to bytearray (byte-access operators); others keep array<T> format
            if(elemType == "char" || elemType == "charliteral")
                paramTypeName = "bytearray";
            else
                paramTypeName = format("array<{0}>", elemType);
        }
        param.type=languageService.getType(paramTypeName);
        if(param.type.name.empty()) param.type.name = paramTypeName; // for func<...> types
        tok=file.getToken(); // name, "=", ",", or ")"
        // Accept both identifier and dataType tokens for the parameter name. A dataType here
        // means the name collides with a registered class (e.g., parameter 'b' when 'class B'
        // exists). The parameter declaration is the authoritative use of the name in the
        // function's scope, so we allow the shadow.
        if(tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)){
            param.name=(string) tok;
            param.displayName=tok.originalValue;
            // Disallow parameter names that shadow a global, a class member, or an object member.
            // Emitters are skipped: their parameter names are template substitution keys, not I6 locals.
            // Symbolic-constant globals (grammartoken, attribute, property, verb) are skipped:
            // they're I6 constants used in grammar/give/provides contexts, not writable storage,
            // so a parameter of the same name doesn't actually shadow anything reachable in code.
            if(!funcDef.isEmitter){
                for(typeDef* g : languageService.globals)
                    if(g->name == param.name){
                        if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                            const string& t = vd->type.name;
                            if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                        }
                        parsingWarning("Parameter '" + param.name + "' shadows global of the same name; the global is unreachable from this routine's scope.");
                    }
                if(currentClass != nullptr){
                    for(typeMember* m : currentClass->members)
                        if(m->name == param.name)
                            parsingWarning("Parameter '" + param.name + "' shadows a member of class '" + currentClass->name + "'.");
                    // Walk base class hierarchy for inherited members (vars and functions) — warning only
                    function<void(classDef*)> checkBases = [&](classDef* c){
                        for(typeMember* m : c->members)
                            if(m->name == param.name)
                                if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                                    parsingWarning("Parameter '" + param.name + "' shadows inherited member '" + param.name + "' from class '" + c->dName() + "'.");
                        for(classDef* base : c->baseClasses) checkBases(base);
                    };
                    for(classDef* base : currentClass->baseClasses) checkBases(base);
                }
                if(currentObject != nullptr)
                    for(typeMember* m : currentObject->members)
                        if(m->name == param.name)
                            parsingWarning("Parameter '" + param.name + "' shadows a member of object '" + currentObject->name + "'.");
            }
            tok=file.getToken(); // "=", ",", or ")"
        }
        if(tok.is(token::assignment)){
            // Set expected type to the parameter's declared type so name resolution can
            // disambiguate (e.g. an enum value name shared by multiple enums).
            string savedExpectedDef = currentExpectedType;
            currentExpectedType = param.type.name;
            expression* defExpr = parseExpression(file.getToken(), {token::comma, token::parenClose}, nullptr, nullptr);
            currentExpectedType = savedExpectedDef;
            param.defaultValue = defExpr->text();
            funcDef.params.push_back(&param);
            if(defExpr->terminator == token::parenClose) break; // ")" consumed by parseExpression
            tok=file.getToken(); // terminator was ","; read next param's type
        } else {
            tok.assertOneOf({token::comma, token::parenClose});
            funcDef.params.push_back(&param);
            if(tok.is(token::parenClose)) break;
            tok=file.getToken(); // tok was ","; read next param's type
        }
    }
    return false;
}

// Depth-first search through a class and its base classes (first-listed wins).
// Returns the first typeMember* for which pred returns true, or nullptr.
typeMember* bglParser::findMemberInHierarchy(classDef* cls, std::function<bool(typeMember*)> pred){
    for(typeMember* m : cls->members)
        if(pred(m)) return m;
    for(classDef* base : cls->baseClasses){
        typeMember* found = findMemberInHierarchy(base, pred);
        if(found) return found;
    }
    return nullptr;
}

// Unified method resolution. Searches:
//   1. Class hierarchy (via typeName → classDef)
//   2. ObjectDef instance members (via objPath in globals, or currentObject if objPath is "self")
// Handles: default parameters, var-fallback, pre-scan stub name-only matching.
// Apply type-parameter bindings to a method's signature. Returns either:
//   • the original `fd` unchanged, when no substitution applies, OR
//   • a freshly-allocated functionDef* clone with substituted return / param types.
// Body is shared (raw I6 emitter bodies are type-erased so substitution doesn't affect them).
// Caller treats the result as the canonical method to bind against. Clones are never freed —
// matches the existing findArraySubscriptOp leak convention.
static functionDef* substituteMethodForBindings(functionDef* fd, const unordered_map<string,string>& bindings){
    if(!fd || bindings.empty()) return fd;
    auto sub = [&](const string& t) -> string {
        // Direct match: the entire type name IS a type parameter (e.g., return type T).
        auto it = bindings.find(t);
        if(it != bindings.end()) return it->second;
        // Wrapped form: substitute each comma-separated piece inside the angle brackets
        // so `array<T>`, `func<bool, T>`, `func<var, T>`, etc. all rewrite when T is bound.
        size_t lt = t.find('<');
        if(lt != string::npos && !t.empty() && t.back() == '>'){
            string base = t.substr(0, lt);
            string body = t.substr(lt + 1, t.size() - lt - 2);
            string out;
            size_t i = 0;
            bool anyChange = false;
            while(i < body.size()){
                size_t comma = body.find(',', i);
                size_t end = (comma == string::npos) ? body.size() : comma;
                // Trim leading whitespace; keep trailing-whitespace preservation simple.
                size_t s = i;
                while(s < end && (body[s] == ' ' || body[s] == '\t')) s++;
                string piece = body.substr(s, end - s);
                auto pit = bindings.find(piece);
                if(pit != bindings.end()){ piece = pit->second; anyChange = true; }
                if(!out.empty()) out += ",";
                out += piece;
                if(comma == string::npos) break;
                i = comma + 1;
            }
            if(anyChange) return base + "<" + out + ">";
        }
        return t;
    };
    string newRet = sub(fd->returnType.name);
    bool anyChange = (newRet != fd->returnType.name);
    vector<string> newParamTypes;
    newParamTypes.reserve(fd->params.size());
    for(paramDef* p : fd->params){
        string nt = sub(p->type.name);
        if(nt != p->type.name) anyChange = true;
        newParamTypes.push_back(nt);
    }
    if(!anyChange) return fd;
    functionDef* clone = new functionDef();
    clone->name = fd->name;
    clone->displayName = fd->displayName;
    clone->src = fd->src;
    clone->isEmitter = fd->isEmitter;
    clone->isExplicit = fd->isExplicit;
    clone->isDefault = fd->isDefault;
    clone->isValueEmitter = fd->isValueEmitter;
    clone->returnType.name = newRet;
    clone->body = fd->body; // shared
    clone->cleanups = fd->cleanups;
    clone->replacedTarget = fd->replacedTarget;
    clone->replacedFunc = fd->replacedFunc;
    clone->captures = fd->captures;
    clone->isPrePassStub = fd->isPrePassStub;
    for(size_t i = 0; i < fd->params.size(); i++){
        paramDef* pc = new paramDef();
        pc->name = fd->params[i]->name;
        pc->displayName = fd->params[i]->displayName;
        pc->type.name = newParamTypes[i];
        pc->defaultValue = fd->params[i]->defaultValue;
        clone->params.push_back(pc);
    }
    return clone;
}

bglParser::MethodMatch bglParser::resolveMethod(const string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args, const string& elementType){
    MethodMatch result;
    functionDef* varFallback = nullptr;

    // Build type-parameter bindings if the receiver class is generic and the caller supplied
    // an element-type binding. Single-parameter case only — sufficient for array<T>; multi-
    // param classes (map<K,V> etc.) will need the parameter list extended.
    // Use getDispatchClass so templated receiver types (`array<int>` from a parametric param)
    // strip down to the generic class.
    classDef* cls = getDispatchClass(typeName);
    unordered_map<string,string> bindings;
    if(cls && !cls->typeParameters.empty() && !elementType.empty())
        bindings[cls->typeParameters[0]] = elementType;

    // Helper: check one function definition against the method name and args.
    // If type-parameter bindings apply, substitute on a clone before signature checking; the
    // caller's MethodMatch carries the substituted clone so downstream type queries (return
    // type, param types, isTypeCompatible, emission) all see concrete types.
    auto checkMethod = [&](functionDef* fdIn) {
        if(fdIn->name != methodName) return;
        functionDef* fd = substituteMethodForBindings(fdIn, bindings);
        if(!result.nameMatch) result.nameMatch = fd;
        result.nameFound = true;

        // Pre-scan stubs match by name only (params may be absent or partial); treat as arity-match too
        if(fd->isPrePassStub){ result.method = fd; if(!result.arityMatch) result.arityMatch = fd; return; }

        // Check arity (accounting for default params)
        size_t req = 0;
        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
        if(args.size() < req || args.size() > fd->params.size()) return;
        if(!result.arityMatch) result.arityMatch = fd;

        // Check arg types
        bool argsOk = true, usesVar = false;
        for(size_t i = 0; i < args.size() && argsOk; i++){
            string argType = args[i]->resolvedType;
            string paramType = fd->params[i]->type.name;
            if(paramType == "var") usesVar = true;
            else if(!argType.empty() && !isTypeCompatible(argType, paramType)) argsOk = false;
        }
        if(!argsOk) return;
        if(usesVar){ if(!varFallback) varFallback = fd; }
        else if(!result.method) result.method = fd;
    };

    // Step 1: search class hierarchy
    if(cls != nullptr){
        std::function<void(classDef*)> searchHierarchy = [&](classDef* c){
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    checkMethod(fd);
            if(!result.method)
                for(classDef* base : c->baseClasses){ searchHierarchy(base); if(result.method) return; }
        };
        searchHierarchy(cls);
    }
    if(!result.method) result.method = varFallback;

    // Step 2: fallback — search objectDef's own members, then walk its objectClass hierarchy.
    // An objectDef's identity is itself, so its own member overrides win over inherited ones.
    // After local members, walk objectClass + baseClasses so inherited methods (give/has/provides
    // on `object`, plus any explicit class members) are reachable. Mirrors Step 1's hierarchy walk.
    // Three lookup strategies for finding the target objectDef:
    //   a) typeName itself resolves to an objectDef.
    //   b) objPath names a global objectDef (covers receivers like `_glulx.method()`).
    //   c) self / current-object short-circuits.
    if(!result.method){
        string lowerPath = objPath;
        transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        objectDef* targetObj = nullptr;
        // (a) Type-name is itself an objectDef.
        if(auto* od = dynamic_cast<objectDef*>(&languageService.getType(typeName)))
            targetObj = od;
        // (c) self / "object" inside an object body
        else if(lowerPath == "self" && currentObject != nullptr)
            targetObj = currentObject;
        else if(typeName == "object" && currentObject != nullptr)
            targetObj = currentObject;
        // (b) objPath names a global objectDef
        else
            for(typeDef* g : languageService.globals)
                if(g->name == lowerPath)
                    if(auto* od = dynamic_cast<objectDef*>(g)){ targetObj = od; break; }
        if(targetObj != nullptr){
            varFallback = nullptr;
            // Local members first — overrides win over inherited.
            for(typeMember* m : targetObj->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    checkMethod(fd);
            // Then walk the objectClass hierarchy.
            if(!result.method && targetObj->objectClass){
                std::function<void(classDef*)> searchHierarchy = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            checkMethod(fd);
                    if(!result.method)
                        for(classDef* base : c->baseClasses){ searchHierarchy(base); if(result.method) return; }
                };
                searchHierarchy(targetObj->objectClass);
            }
            if(!result.method) result.method = varFallback;
        }
    }

    return result;
}

// Method resolution with non-explicit operator() conversion fallback. If resolveMethod fails to
// find a method on the requested type, walks that type's members for each non-explicit
// `operator()` emitter, retries resolveMethod on the converted return type, and returns the first
// successful match. On success, updates typeName in-place so the caller's type variable reflects
// the conversion target. On failure, returns the original (failed) match so the caller can use
// nameFound/nameMatch/arityMatch for error reporting.
bglParser::MethodMatch bglParser::resolveMethodWithConversion(string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args, const string& elementType){
    MethodMatch mm = resolveMethod(typeName, objPath, methodName, args, elementType);
    if(mm.method) return mm;
    classDef* srcCls = dynamic_cast<classDef*>(&languageService.getType(typeName));
    if(!srcCls) return mm;
    for(typeMember* m : srcCls->members){
        auto* convOp = dynamic_cast<functionDef*>(m);
        if(!convOp || convOp->name != "operator()" || !convOp->isEmitter || convOp->isExplicit) continue;
        MethodMatch mm2 = resolveMethod(convOp->returnType.name, objPath, methodName, args, elementType);
        if(mm2.method){
            typeName = convOp->returnType.name;
            return mm2;
        }
    }
    return mm;
}

// Replace a pre-scan stub in a member list with the real definition.
bool bglParser::replaceStubMember(vector<typeMember*>& members, functionDef& newDef){
    for(auto it = members.begin(); it != members.end(); ++it)
        if(auto* fd = dynamic_cast<functionDef*>(*it))
            if(fd->name == newDef.name && fd->isPrePassStub){
                *it = &newDef;
                return true;
            }
    return false;
}

// True if the named type exposes a member named `memberName`.
// For classes, walks the base hierarchy. For objectDefs, walks own members.
// Returns false for enums, primitives, and unknown types — they have no addressable members.
bool bglParser::typeHasMember(const string& typeName, const string& memberName){
    if(typeName.empty() || memberName.empty()) return false;
    typeDef& td = languageService.getType(typeName);
    if(auto* cls = dynamic_cast<classDef*>(&td)){
        return findMemberInHierarchy(cls, [&](typeMember* m){ return m->name == memberName; }) != nullptr;
    }
    if(auto* obj = dynamic_cast<objectDef*>(&td)){
        for(typeMember* m : obj->members) if(m->name == memberName) return true;
        return false;
    }
    return false;
}

std::string bglParser::resolveIdentifierType(std::string name, functionDef* func, statementBlock* body, const string& memberHint){
    if(name == "null") return "object";
    if(name == "self" && lambdaOuterFunc == nullptr){
        // `self` inside an objectDef is the objectDef's own identity, regardless of base class.
        // resolveMethod walks `objectClass`'s hierarchy for inherited methods, so this works
        // whether the base is `object`, a user class, or anything else — no special-casing.
        if(currentObject != nullptr) return currentObject->name;
        if(currentClass != nullptr) return currentClass->name;
        return "object";
    }

    // ── LEXICAL SCOPE — first match wins absolutely ────────────────────────────
    // Locals, params, and members of the enclosing object/class form the lexical scope of
    // the current code. Inner scope shadows outer by design, so first match wins without
    // any ambiguity check. memberHint is NOT applied here: if a local exists with this
    // name, it's the receiver — let dispatch produce a "no method on type X" error if the
    // local's type doesn't have the member, rather than silently looking past the shadow.
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return p->type.name;
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->type.name;
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
    if(currentClass != nullptr){
        for(typeMember* m : currentClass->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
        function<string(classDef*)> findVarInBases = [&](classDef* c) -> string {
            for(typeMember* m : c->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == name) return vd->type.name;
            for(classDef* base : c->baseClasses){
                string t = findVarInBases(base);
                if(!t.empty()) return t;
            }
            return "";
        };
        for(classDef* base : currentClass->baseClasses){
            string t = findVarInBases(base);
            if(!t.empty()) return t;
        }
    }

    // Closure capture: check outer function's scope for type. This is a lexical-scope walk
    // (the lambda's enclosing function), so it runs BEFORE file-scope candidates — standard
    // scoping rule that captures shadow same-named globals.
    if(lambdaOuterFunc != nullptr){
        for(paramDef* p : lambdaOuterFunc->params)
            if(p->name == name) return p->type.name;
        if(lambdaOuterBody != nullptr)
            for(statement* s : lambdaOuterBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
        statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
        if(rootBody != nullptr && rootBody != lambdaOuterBody){
            function<string(statementBlock*)> findInBlock = [&](statementBlock* blk) -> string {
                if(!blk) return "";
                for(statement* s : blk->statements){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name) return vd->type.name;
                    if(auto* ifs = dynamic_cast<ifStatement*>(s)){
                        string t = findInBlock(ifs->thenBlock); if(!t.empty()) return t;
                        t = findInBlock(ifs->elseBlock); if(!t.empty()) return t;
                    } else if(auto* fors = dynamic_cast<forStatement*>(s)){
                        string t = findInBlock(fors->body); if(!t.empty()) return t;
                    } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
                        string t = findInBlock(fis->body); if(!t.empty()) return t;
                    } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
                        string t = findInBlock(ws->body); if(!t.empty()) return t;
                    } else if(auto* ds = dynamic_cast<doStatement*>(s)){
                        string t = findInBlock(ds->body); if(!t.empty()) return t;
                    } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
                        string t = findInBlock(tc->tryBody); if(!t.empty()) return t;
                        t = findInBlock(tc->catchBody); if(!t.empty()) return t;
                    }
                }
                return "";
            };
            string t = findInBlock(rootBody);
            if(!t.empty()) return t;
        }
        // Capture chaining: walk the full lambda nesting stack
        for(int si = (int)lambdaOuterFuncStack.size() - 1; si >= 0; si--){
            functionDef* ancestor = lambdaOuterFuncStack[si];
            if(ancestor == lambdaOuterFunc) continue;
            for(paramDef* p : ancestor->params)
                if(p->name == name) return p->type.name;
            statementBlock* ancestorBody = dynamic_cast<statementBlock*>(ancestor->body);
            if(ancestorBody != nullptr)
                for(statement* s : ancestorBody->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name) return vd->type.name;
            for(auto& cap : ancestor->captures)
                if(cap.outerName == name) return cap.typeName;
        }
        // `self` type resolution for capture
        if(name == "self" && (currentClass != nullptr || currentObject != nullptr))
            return currentClass ? currentClass->name : "object";
    }

    // ── FILE SCOPE — collect all matches, then apply ambiguity rule ───────────
    // Enum values, globals, verbs, emitter objects, #defines, and #using imports all live
    // at file/module scope. They share one conceptual namespace — collisions there are
    // accidents, not deliberate shadowing. Collect every match and let the ambiguity logic
    // below decide.
    struct Candidate { string type; string origin; bool isEnum; };
    vector<Candidate> candidates;

    // Enum values: walk EVERY enum that contains this value name. Multiple enums may share
    // a value name (e.g. eColor.Red and eFlavor.Red); each is a distinct candidate that the
    // expected-type tie-breaker can disambiguate.
    for(typeDef* t : languageService.objectTypes){
        auto* ed = dynamic_cast<enumDef*>(t);
        if(!ed) continue;
        for(enumValueDef* v : ed->namedValues)
            if(v->name == name){
                candidates.push_back({ed->name, format("enum value of '{0}'", ed->name), true});
                break;
            }
    }
    // Globals (variable, function, objectDef, classDef, enumDef)
    for(typeDef* g : languageService.globals){
        if(g->name == name){
            string ct, origin;
            if(auto* vd = dynamic_cast<variableDeclaration*>(g)){ ct = vd->type.name; origin = format("global variable '{0}'", g->name); }
            else if(auto* fd = dynamic_cast<functionDef*>(g)){ ct = fd->returnType.name; origin = format("global function '{0}'", g->name); }
            else if(auto* od = dynamic_cast<objectDef*>(g)){
                // Type-identity rule: an objectDef IS its own type unless it explicitly
                // inherits from a non-`object` class. The implicit `object` parent doesn't
                // count — it's the universal supertype, not a specific identity.
                // (TODO: this `name != "object"` check is asymmetric and undermines the
                // "object is not special" principle. Removing it requires teaching the ~80
                // dynamic_cast<classDef*>(getType(...)) operator-dispatch sites to also walk
                // an objectDef's objectClass — out of scope for now; resolveMethod already
                // does this so unclassed `object Foo{}` reaches inherited methods.)
                bool explicitNonObjectClass = od->objectClass && od->objectClass->name != "object";
                ct = explicitNonObjectClass ? od->objectClass->name : od->name;
                origin = format("global object '{0}'", g->name);
            }
            else if(dynamic_cast<classDef*>(g) || dynamic_cast<enumDef*>(g)){
                // Class/enum type-name reference (e.g. ClassName.staticMember). The type
                // identifies itself so dot-path code can locate the member.
                ct = name;
                origin = format("type '{0}'", g->name);
            }
            if(!ct.empty()) candidates.push_back({ct, origin, false});
            break;
        }
    }
    // Action constants (extern verb Take etc.)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name){
            candidates.push_back({"verb", format("verb '{0}'", vd->name), false});
            break;
        }
    }
    // Global emitter objects
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject || cd->isEmitterClass)
                candidates.push_back({name, format("emitter object '{0}'", name), false});
    }
    // #define symbols
    {
        auto it = definedSymbols.find(name);
        if(it != definedSymbols.end() && !it->second.empty()){
            bool isNumeric = !it->second.empty() && (isdigit(it->second[0]) || (it->second[0] == '-' && it->second.size() > 1));
            string ct = isNumeric ? "intliteral" : "stringliteral";
            candidates.push_back({ct, format("#define symbol '{0}'", name), false});
        }
    }
    // #using class imports
    for(classDef* imp : usingImports){
        bool added = false;
        for(typeMember* m : imp->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == name){ candidates.push_back({fd->returnType.name, format("#using-imported method '{0}.{1}'", imp->dName(), name), false}); added = true; break; }
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name){ candidates.push_back({vd->type.name, format("#using-imported variable '{0}.{1}'", imp->dName(), name), false}); added = true; break; }
        }
        if(added) continue;
    }
    // #using object imports
    for(objectDef* imp : usingObjectImports){
        bool added = false;
        for(typeMember* m : imp->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == name){ candidates.push_back({fd->returnType.name, format("#using-imported method '{0}.{1}'", imp->name, name), false}); added = true; break; }
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name){ candidates.push_back({vd->type.name, format("#using-imported member '{0}.{1}'", imp->name, name), false}); added = true; break; }
        }
        if(added) continue;
    }

    // Dedupe: candidates resolving to the same type are the same thing seen via multiple lookup
    // paths (e.g. a verb registered both as a verbObjectDef and as a global object stub).
    {
        vector<Candidate> deduped;
        for(auto& c : candidates){
            bool dup = false;
            for(auto& d : deduped) if(d.type == c.type){ dup = true; break; }
            if(!dup) deduped.push_back(c);
        }
        candidates = deduped;
    }
    // Globals beat enum values: when a non-enum candidate exists alongside enum-value
    // candidates, drop the enum candidates. Lets a global like `extern object noun` win
    // over an enum value `grammarToken.noun` for bare `noun` references; the enum value
    // is still reachable via `grammarToken.noun` qualified access.
    {
        bool hasNonEnum = false;
        for(auto& c : candidates) if(!c.isEnum){ hasNonEnum = true; break; }
        if(hasNonEnum){
            vector<Candidate> filtered;
            for(auto& c : candidates) if(!c.isEnum) filtered.push_back(c);
            candidates = filtered;
        }
    }
    // Resolve the candidate set. Cases:
    //   • 1 candidate         → use it (let dispatch produce type-mismatch errors if any).
    //   • 2+ candidates       → try memberHint, then currentExpectedType as tie-breakers.
    //                           If a single candidate emerges, use it; otherwise error.
    if(candidates.size() == 1) return candidates[0].type;
    if(candidates.size() >= 2){
        if(!memberHint.empty()){
            vector<Candidate*> satisfying;
            for(auto& c : candidates)
                if(typeHasMember(c.type, memberHint)) satisfying.push_back(&c);
            if(satisfying.size() == 1) return satisfying[0]->type;
            // 0 or 2+ satisfy — fall through to expected-type filter, then ambiguity error.
        }
        if(!currentExpectedType.empty()){
            vector<Candidate*> compatible;
            for(auto& c : candidates)
                if(isTypeCompatible(c.type, currentExpectedType)) compatible.push_back(&c);
            if(compatible.size() == 1) return compatible[0]->type;
        }
        string msg = format("'{0}' is ambiguous: matches ", name);
        for(size_t i = 0; i < candidates.size(); i++){
            if(i > 0) msg += (i == candidates.size() - 1 ? " and " : ", ");
            msg += candidates[i].origin;
        }
        msg += ". Qualify the use explicitly to disambiguate.";
        parsingError(msg);
    }

    // Loose mode: see qualifyIdentifier for rationale. Unknown identifiers inside #bgl{}
    // blocks are typed `var` so the type checker passes them through.
    if(looseIdentifierMode) return "var";
    // Property-name fallback: a bare identifier that matches any class/object member or
    // free-standing `property foo;` decl resolves to type `property`. Lets `obj.provides(name)`
    // accept names that aren't otherwise in scope, including class members.
    if(languageService.isKnownPropertyName(name)) return "property";
    // Class-name fallback: a bare identifier that matches a registered class resolves to
    // type `bglclass`. Picks up `extern class Foo;` (which never enters `globals`) so
    // `obj.is(Foo)` accepts bare class names. Non-extern classes resolve earlier as their
    // own type and reach `bglclass` via the isTypeCompatible rule.
    if(languageService.isKnownClassName(name)) return "bglclass";
    return "";
}

// Return the declared element type of an array variable, or "" if `name` isn't an arrayDeclaration
// in any reachable scope. Walks the same scopes as resolveIdentifierType but looks specifically
// for arrayDeclaration subclass instances so the elementType field can be read.
string bglParser::resolveArrayElementType(const string& name, functionDef* func, statementBlock* body){
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) {
                // Parameter type is stored as "array<T>" for typed array params
                const string& tn = p->type.name;
                if(tn.size() > 6 && tn.substr(0,6) == "array<" && tn.back() == '>')
                    return tn.substr(6, tn.size() - 7);
                if(tn == "bytearray") return "char";
                return "";
            }
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                if(ad->name == name) return ad->elementType;
    if(currentFunc != nullptr && currentFunc->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(currentFunc->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                    if(ad->name == name) return ad->elementType;
    }
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == name) return ad->elementType;
    if(currentClass != nullptr)
        for(typeMember* m : currentClass->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == name) return ad->elementType;
    for(typeDef* g : languageService.globals)
        if(auto* ad = dynamic_cast<arrayDeclaration*>(g))
            if(ad->name == name) return ad->elementType;
    return "";
}

// Resolve the element type of `objName.propName` where propName is an array member of obj's class.
string bglParser::resolveArrayElementTypeDotted(const string& objName, const string& propName,
                                                  functionDef* func, statementBlock* body){
    // Find objName's class — walk objectInstances + globals
    classDef* cls = nullptr;
    for(typeDef* t : languageService.objectInstances)
        if(auto* od = dynamic_cast<objectDef*>(t))
            if(od->name == objName) {
                for(typeMember* m : od->members)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                        if(ad->name == propName) return ad->elementType;
                cls = od->objectClass;
                break;
            }
    if(!cls) {
        string objType = resolveIdentifierType(objName, func, body);
        cls = dynamic_cast<classDef*>(&languageService.getType(objType));
    }
    function<string(classDef*)> walk = [&](classDef* c) -> string {
        if(!c) return "";
        for(typeMember* m : c->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == propName) return ad->elementType;
        for(classDef* base : c->baseClasses) { string r = walk(base); if(!r.empty()) return r; }
        return "";
    };
    return walk(cls);
}

// Find operator[] or operator[]= on an array class hierarchy matching a specific element type.
// For read (isWrite=false), matches the operator's return type against elemType.
// For write (isWrite=true), matches the operator's second parameter type against elemType.
//
// When the receiving class declares a type parameter (e.g. `class array<T>`), the search applies
// type-parameter→element substitution so `T operator[](int)` resolves as if it were
// `<elemType> operator[](int)`. Subclasses that override with a concrete element type
// (e.g. `byteArray.operator[](int) → char`) win via Pass 1's exact match.
functionDef* bglParser::findArraySubscriptOp(classDef* arrCls, const string& elemType, bool isWrite){
    if(!arrCls) return nullptr;
    const string opName = isWrite ? "[]=" : "[]";
    const size_t expectedParams = isWrite ? 2u : 1u;
    auto matches = [&](functionDef* fd, const string& t) {
        if(!fd || fd->name != opName || fd->params.size() != expectedParams) return false;
        if(isWrite) return fd->params[1]->type.name == t;
        return fd->returnType.name == t;
    };
    // Walk the hierarchy. For each class along the way, build bindings if it declares a type
    // parameter (single-param case for now — array<T>), and apply substitution before matching.
    functionDef* found = nullptr;
    function<void(classDef*)> search = [&](classDef* c) {
        if(!c || found) return;
        unordered_map<string,string> bindings;
        if(!c->typeParameters.empty() && !elemType.empty())
            bindings[c->typeParameters[0]] = elemType;
        for(typeMember* m : c->members)
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                functionDef* effective = substituteMethodForBindings(fd, bindings);
                if(matches(effective, elemType)) { found = effective; return; }
            }
        for(classDef* base : c->baseClasses) { if(found) return; search(base); }
    };
    search(arrCls);
    return found;
}

// Returns the I6-qualified form of an identifier based on scope:
//   - found in local params/vars → name (unqualified)
//   - found in current object's members → "self.name"
//   - found in enum values or globals → name (unqualified)
//   - not found → "" (caller should report an error)
// Whole-word string replacement: replaces occurrences of 'from' in 'str' with 'to',
// only when bounded by non-identifier characters (respects word boundaries).
// Used throughout emitter body substitution ($self, $prop, parameter names).
static string replaceWord(string str, const string& from, const string& to){
    size_t pos = 0;
    while((pos = str.find(from, pos)) != string::npos){
        bool leftOk  = pos == 0 || !(isalnum(str[pos-1]) || str[pos-1] == '_' || str[pos-1] == '$');
        bool rightOk = pos + from.size() >= str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()] == '_');
        if(leftOk && rightOk){ str.replace(pos, from.size(), to); pos += to.size(); }
        else pos += from.size();
    }
    return str;
}

// Check if a variable is declared const (local, global, or class member).
static bool isConstVariable(const string& name, functionDef* func, statementBlock* body){
    // Check locals
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->isConst;
    // Check enclosing function's outer body
    if(func != nullptr && func->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(func->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->isConst;
    }
    // Check globals
    for(typeDef* g : languageService.globals)
        if(auto* vd = dynamic_cast<variableDeclaration*>(g))
            if(vd->name == name) return vd->isConst;
    return false;
}

// Look up a type's display name (original casing) from the language service.
static string typeDisplayName(const string& typeName){
    typeDef& td = languageService.getType(typeName);
    if(&td != &emptyTDef && !td.displayName.empty()) return td.displayName;
    return typeName;
}

// Format a function signature for error messages: "name(type1 p1, type2 p2) → returnType"
static string formatSignature(functionDef* fd){
    string sig = fd->dName() + "(";
    for(size_t i = 0; i < fd->params.size(); i++){
        if(i > 0) sig += ", ";
        sig += fd->params[i]->type.dName() + " " + fd->params[i]->dName();
        if(!fd->params[i]->defaultValue.empty())
            sig += " = " + fd->params[i]->defaultValue;
    }
    sig += ")";
    if(!fd->returnType.name.empty() && fd->returnType.name != "void")
        sig += " -> " + fd->returnType.name;
    return sig;
}

// ── Shared: parse a function call's argument list ────────────────────────────
// Assumes '(' has already been consumed.  Reads comma-separated expressions
// (including named arguments and interpolated strings) until ')'.
bglParser::ParsedArgList bglParser::parseCallArgList(functionDef* func, statementBlock* body){
    ParsedArgList result;
    // Function arguments are a fresh expression context — clear the outer expected-type so it
    // doesn't bleed into arg resolution. (Per-arg expected types from the callee's signature
    // would be ideal but require flipping overload-resolution order; deferred for now.)
    string savedExpectedArgs = currentExpectedType;
    currentExpectedType = "";
    token firstArgTok = file.getToken();
    while(firstArgTok.isNot(token::parenClose)){
        // Named argument detection: identifier followed by ':'
        string namedArgName;
        if(firstArgTok.is(eTokenType::name) && file.peekToken(1).is(":")){
            namedArgName = firstArgTok.value;
            file.getToken(); // consume ':'
            firstArgTok = file.getToken(); // read the value expression's first token
        }
        if(firstArgTok.is("$") && file.peekToken(1).is(eTokenType::quote)){
            // Interpolated string literal argument: func($"...")
            vector<interpolatedSegment> segs = parseInterpolatedSegments(func, body);
            expression* arg = new expression();
            arg->resolvedType = "interpolatedstringliteral";
            token sep = file.getToken();
            arg->terminator = sep.value;
            result.args.push_back(arg);
            result.namedArgNames.push_back(namedArgName);
            while(result.interpSegmentsPerArg.size() < result.args.size() - 1)
                result.interpSegmentsPerArg.push_back({});
            result.interpSegmentsPerArg.push_back(segs);
            if(sep.is(token::parenClose)) break;
            firstArgTok = file.getToken();
        } else {
            expression* arg = parseExpression(firstArgTok, {token::comma, token::parenClose}, func, body);
            result.args.push_back(arg);
            result.namedArgNames.push_back(namedArgName);
            if(arg->terminator == token::parenClose) { currentExpectedType = savedExpectedArgs; return result; }
            firstArgTok = file.getToken();
        }
    }
    currentExpectedType = savedExpectedArgs;
    return result;
}

// ── Shared: resolve a global function call ───────────────────────────────────
// Given a function name and parsed argument expressions, finds the best matching
// global function definition.  Priority: exact type > conversion > var fallback.
bglParser::GlobalCallMatch bglParser::resolveGlobalCall(const string& name, const vector<expression*>& args,
                                                         functionDef* func, statementBlock* body){
    GlobalCallMatch result;
    functionDef* conversionMatch = nullptr;
    functionDef* varFallback = nullptr;
    for(typeDef* g : languageService.globals){
        if(auto* fd = dynamic_cast<functionDef*>(g)){
            if(fd->name != name) continue;
            if(result.nameMatch == nullptr) result.nameMatch = fd;
            size_t req = 0;
            for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
            if(args.size() >= req && args.size() <= fd->params.size()){
                if(result.arityMatch == nullptr) result.arityMatch = fd;
                bool argsOk = true;
                bool usesVar = false;
                bool needsConversion = false;
                for(size_t i = 0; i < args.size() && argsOk; i++){
                    string argType = args[i]->resolvedType;
                    string paramType = fd->params[i]->type.name;
                    if(paramType == "var") usesVar = true;
                    else if(argType.empty() || argType == paramType) {} // exact or unknown
                    else if(isTypeCompatible(argType, paramType)) needsConversion = true;
                    else argsOk = false;
                }
                if(argsOk){
                    if(usesVar){ if(varFallback == nullptr) varFallback = fd; }
                    else if(!needsConversion){ result.match = fd; return result; }
                    else if(conversionMatch == nullptr) conversionMatch = fd;
                }
            }
        }
    }
    if(result.match == nullptr) result.match = conversionMatch;
    if(result.match == nullptr) result.match = varFallback;
    // If no global found, check #using imports for a method with the matching name.
    // This lets `#using bgl.asm` make `get_prop_len(x)` work as a bare call.
    auto checkImportMember = [&](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        if(!fd || fd->name != name) return;
        if(result.nameMatch == nullptr) result.nameMatch = fd;
        size_t req = 0;
        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
        if(args.size() >= req && args.size() <= fd->params.size()){
            if(result.arityMatch == nullptr) result.arityMatch = fd;
            if(result.match == nullptr) result.match = fd;
        }
    };
    if(result.match == nullptr){
        for(classDef* imp : usingImports)
            for(typeMember* m : imp->members) checkImportMember(m);
    }
    if(result.match == nullptr){
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members) checkImportMember(m);
    }
    // If no global found, check for func<> variable
    if(result.nameMatch == nullptr){
        string varType = resolveIdentifierType(name, func, body);
        if(varType.rfind("func<", 0) == 0){
            size_t lt = varType.find('<');
            size_t sep = varType.find(',', lt);
            size_t gt = varType.rfind('>');
            result.funcVarReturnType = (sep != string::npos && sep < gt)
                ? varType.substr(lt+1, sep-lt-1)
                : varType.substr(lt+1, gt-lt-1);
        }
    }
    return result;
}

// ── Shared: validate a global function call and report arity/type errors ─────
// Called after resolveGlobalCall; issues parsingError if validation fails.
// Returns the resolved function's return type name.
string bglParser::validateGlobalCall(GlobalCallMatch& gcm, const string& funcName, size_t argCount){
    if(!gcm.funcVarReturnType.empty()) return gcm.funcVarReturnType;
    if(gcm.nameMatch == nullptr){
        // Loose mode (#bgl{} content): unresolved function names are assumed to be I6
        // routines defined in the surrounding stream. Return "var" as the inferred type;
        // emission falls through to a literal `name(args)` call which I6 will resolve.
        if(looseIdentifierMode) return "var";
        parsingError(format("Undeclared function '{0}'", funcName));
    }
    string dispName = gcm.nameMatch ? gcm.nameMatch->dName() : funcName;
    if(gcm.arityMatch == nullptr){
        size_t req = 0; for(paramDef* p : gcm.nameMatch->params) if(p->defaultValue.empty()) req++;
        size_t tot = gcm.nameMatch->params.size();
        parsingError(format("Function '{0}' expects {1} argument(s), but {2} were supplied.\n  Expected: {3}",
            dispName,
            (req == tot) ? to_string(tot) : to_string(req) + "-" + to_string(tot),
            argCount, formatSignature(gcm.nameMatch)));
    }
    if(gcm.match == nullptr){
        // Loose mode (#bgl{}): bind to nameMatch when arity matches but types don't —
        // arguments pass through to I6 unchecked. See spec §15.6.
        if(looseIdentifierMode && gcm.nameMatch != nullptr){
            gcm.match = gcm.nameMatch;
            return gcm.match->returnType.name;
        }
        parsingError(format("No overload of function '{0}' accepts these argument types", dispName));
    }
    return gcm.match->returnType.name;
}

std::string bglParser::qualifyIdentifier(std::string name, functionDef* func, statementBlock* body, const string& memberHint){
    if(name == "null") return "nothing";
    if(name == "self" && lambdaOuterFunc == nullptr) return "self";
    // Handle dot-path: qualify the head, then check for value emitter on the tail
    size_t dot = name.find('.');
    if(dot != string::npos){
        string head = name.substr(0, dot);
        string tail = name.substr(dot + 1);
        string qualifiedHead = qualifyIdentifier(head, func, body);
        if(qualifiedHead.empty()) return "";
        // Check if head is a class/emitter-namespace with a value emitter or alias member
        string lowerTail = tail;
        transform(lowerTail.begin(), lowerTail.end(), lowerTail.begin(), ::tolower);
        // Split tail at first dot for multi-level paths: bgl.strings.init → head=bgl, firstSeg=strings, rest=init
        string firstSeg = lowerTail;
        string rest;
        size_t tailDot = lowerTail.find('.');
        if(tailDot != string::npos){
            firstSeg = lowerTail.substr(0, tailDot);
            rest = lowerTail.substr(tailDot + 1);
        }
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(head));
        if(cls){
            for(typeMember* m : cls->members){
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == firstSeg && fd->isValueEmitter && fd->isEmitter && rest.empty())
                        if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                            string b = processBglConditionals(blk->i6Body);
                            b = i6Emitter::replaceWord(b, "$self", qualifiedHead);
                            b = i6Emitter::replaceWord(b, "$val",  qualifiedHead);
                            size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                            size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                            return b;
                        }
                // Alias member: resolve through the alias type for the remaining path
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == firstSeg && !rest.empty()){
                        // Recurse: qualify "aliasType.rest"
                        return qualifyIdentifier(vd->type.name + "." + rest, func, body);
                    }
            }
        }
        return qualifiedHead + "." + tail;
    }
    // Tier 1a: params of current (possibly nested) context
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return name;
    // Tier 1b: locals in current block
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return name;
    // Tier 1c: locals in ancestor blocks (when inside nested if/for/while/etc.).
    // Walks the activeBlockStack — each block was pushed when its compile context opened.
    // This handles the case where the AST is still being built (parent statements haven't
    // been pushed to their body yet) by checking each in-progress block directly.
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return name;
    // Tier 2: current object/class members (variables and methods) → qualify with self
    // Walks the full class hierarchy (depth-first through baseClasses) so inherited members
    // from multiple bases are reachable as bare identifiers inside method bodies.
    // Uses the member's displayName (via dName()) so user case is preserved in I6 emission.
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(m->name == name)
                if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                    return "self." + m->dName();
    if(currentClass != nullptr){
        // Direct members: match both variables and functions (same as before).
        // Static variables resolve to their mangled global name, not `self.name` — they
        // live outside any instance's property table.
        for(typeMember* m : currentClass->members)
            if(m->name == name)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                    if(vd->isStatic) return "_bgl_" + currentClass->dName() + "_" + vd->dName();
                    return "self." + vd->dName();
                }
                else if(dynamic_cast<functionDef*>(m))
                    return "self." + m->dName();
        // Base class hierarchy: only match VARIABLES, not functions. Function/method resolution
        // has its own hierarchy-aware path (resolveMethod) that checks arity and types correctly.
        // Walking functions here would shadow global functions of the same name (e.g. `print`
        // on _bglObject shadowing the global `print(string)` emitter).
        // Returns the member's displayName when found in a base.
        std::function<string(classDef*)> findVarDisplayInBases = [&](classDef* c) -> string {
            for(typeMember* m : c->members)
                if(m->name == name)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) return vd->dName();
            for(classDef* base : c->baseClasses){
                string r = findVarDisplayInBases(base);
                if(!r.empty()) return r;
            }
            return "";
        };
        for(classDef* base : currentClass->baseClasses){
            string disp = findVarDisplayInBases(base);
            if(!disp.empty()) return "self." + disp;
        }
    }

    // Tier 7 (hoisted): closure capture — inside a lambda, check the outer function's scope
    // BEFORE file-scope candidates. Captures shadow same-named globals, matching standard
    // lexical-scoping semantics in any closure-bearing language.
    if(lambdaOuterFunc != nullptr && currentFunc != nullptr){
        // Check outer function's params
        for(paramDef* p : lambdaOuterFunc->params)
            if(p->name == name)
                return addCapture(name, p->type.name);
        // Check immediate enclosing body (e.g. for-loop body)
        if(lambdaOuterBody != nullptr)
            for(statement* s : lambdaOuterBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name)
                        return addCapture(name, vd->type.name);
        // Check function root body recursively — covers locals declared before/outside the
        // enclosing block (e.g. `int base = 10;` declared before a for-loop).
        statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
        if(rootBody != nullptr && rootBody != lambdaOuterBody){
            function<variableDeclaration*(statementBlock*)> findInBlock = [&](statementBlock* blk) -> variableDeclaration* {
                if(!blk) return nullptr;
                for(statement* s : blk->statements){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name) return vd;
                    if(auto* ifs = dynamic_cast<ifStatement*>(s)){
                        if(auto* r = findInBlock(ifs->thenBlock)) return r;
                        if(auto* r = findInBlock(ifs->elseBlock)) return r;
                    } else if(auto* fors = dynamic_cast<forStatement*>(s)){
                        if(auto* r = findInBlock(fors->body)) return r;
                    } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
                        if(auto* r = findInBlock(fis->body)) return r;
                    } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
                        if(auto* r = findInBlock(ws->body)) return r;
                    } else if(auto* ds = dynamic_cast<doStatement*>(s)){
                        if(auto* r = findInBlock(ds->body)) return r;
                    } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
                        if(auto* r = findInBlock(tc->tryBody)) return r;
                        if(auto* r = findInBlock(tc->catchBody)) return r;
                    }
                }
                return nullptr;
            };
            if(auto* vd = findInBlock(rootBody))
                return addCapture(name, vd->type.name);
        }
        // Capture chaining: walk the full lambda nesting stack.
        for(int si = (int)lambdaOuterFuncStack.size() - 1; si >= 0; si--){
            functionDef* ancestor = lambdaOuterFuncStack[si];
            if(ancestor == lambdaOuterFunc) continue;
            for(paramDef* p : ancestor->params)
                if(p->name == name)
                    return addCapture(name, p->type.name);
            statementBlock* ancestorBody = dynamic_cast<statementBlock*>(ancestor->body);
            if(ancestorBody != nullptr)
                for(statement* s : ancestorBody->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name)
                            return addCapture(name, vd->type.name);
            for(auto& cap : ancestor->captures)
                if(cap.outerName == name)
                    return addCapture(cap.globalName, cap.typeName);
        }
        // `self` capture: when a lambda is inside an object method, `self` refers to the
        // enclosing object. Capture it to a global.
        if(name == "self" && (currentClass != nullptr || currentObject != nullptr)){
            string selfType = currentClass ? currentClass->name : "object";
            return addCapture("self", selfType);
        }
    }

    // ── FILE SCOPE — collect all matches, then apply ambiguity rule ───────────
    // Same principle as resolveIdentifierType: enum values, globals, verbs, emitter objects,
    // #defines, and #using imports share one conceptual namespace. Multiple matches across
    // these tiers indicates a real ambiguity, not legitimate scope shadowing.
    struct Candidate { string qualified; string type; string origin; bool isEnum; };
    vector<Candidate> candidates;

    // Enum values: walk EVERY enum containing this name so multiple enums sharing a value
    // each become a candidate (the expected-type filter below disambiguates).
    for(typeDef* t : languageService.objectTypes){
        auto* ed = dynamic_cast<enumDef*>(t);
        if(!ed) continue;
        bool match = false; int matchedValue = 0;
        for(enumValueDef* ev : ed->namedValues)
            if(ev->name == name){ match = true; matchedValue = ev->value; break; }
        if(!match) continue;
        string enumQualified;
        if(ed->isExternal) enumQualified = name;
        else                enumQualified = to_string(matchedValue);
        candidates.push_back({enumQualified, ed->name, format("enum value of '{0}'", ed->name), true});
    }
    // Globals (variable, function, objectDef, classDef, enumDef)
    for(typeDef* g : languageService.globals){
        if(g->name == name){
            string qual, ct, origin;
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                ct = fd->returnType.name;
                origin = format("global function '{0}'", g->name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        qual = b;
                    } else qual = g->i6name.empty() ? name : g->i6name;
                } else qual = g->i6name.empty() ? name : g->i6name;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                ct = vd->type.name;
                qual = g->i6name.empty() ? name : g->i6name;
                origin = format("global variable '{0}'", g->name);
            }
            else if(auto* od = dynamic_cast<objectDef*>(g)){
                // Same type-identity rule as resolveIdentifierType: implicit `object`
                // parent is not a distinguishing class. (Same TODO applies — see above.)
                bool explicitNonObjectClass = od->objectClass && od->objectClass->name != "object";
                ct = explicitNonObjectClass ? od->objectClass->name : od->name;
                qual = g->i6name.empty() ? name : g->i6name;
                origin = format("global object '{0}'", g->name);
            }
            else if(dynamic_cast<classDef*>(g) || dynamic_cast<enumDef*>(g)){
                // Class/enum type-name reference (e.g. for ClassName.staticMember dot-paths).
                // The dot-path code in qualifyIdentifier consults this and resolves the tail.
                qual = g->i6name.empty() ? name : g->i6name;
                ct = name;  // type identifies itself
                origin = format("type '{0}'", g->name);
            }
            if(!qual.empty()) candidates.push_back({qual, ct, origin, false});
            break;
        }
    }
    // #using class imports
    for(classDef* imp : usingImports){
        for(typeMember* m : imp->members){
            string qual, ct, origin;
            bool matched = false;
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name != name) continue;
                ct = fd->returnType.name;
                origin = format("#using-imported method '{0}.{1}'", imp->dName(), name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = i6Emitter::replaceWord(b, "$self", imp->name);
                        b = i6Emitter::replaceWord(b, "$val",  imp->name);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        qual = b;
                    } else qual = imp->name + "." + name;
                } else qual = imp->name + "." + name;
                matched = true;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name != name) continue;
                ct = vd->type.name;
                qual = imp->name + "." + name;
                origin = format("#using-imported variable '{0}.{1}'", imp->dName(), name);
                matched = true;
            }
            if(matched){ candidates.push_back({qual, ct, origin, false}); break; }
        }
    }
    // #using object imports
    for(objectDef* imp : usingObjectImports){
        for(typeMember* m : imp->members){
            string qual, ct, origin;
            bool matched = false;
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name != name) continue;
                ct = fd->returnType.name;
                origin = format("#using-imported method '{0}.{1}'", imp->name, name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = i6Emitter::replaceWord(b, "$self", imp->name);
                        b = i6Emitter::replaceWord(b, "$val",  imp->name);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        qual = b;
                    } else qual = imp->name + "." + name;
                } else qual = imp->name + "." + name;
                matched = true;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name != name) continue;
                ct = vd->type.name;
                qual = imp->name + "." + name;
                origin = format("#using-imported member '{0}.{1}'", imp->name, name);
                matched = true;
            }
            if(matched){ candidates.push_back({qual, ct, origin, false}); break; }
        }
    }
    // Verb names (action constants)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name){
            candidates.push_back({vd->name, "verb", format("verb '{0}'", vd->name), false});
            break;
        }
    }
    // Global emitter objects
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject || cd->isEmitterClass)
                candidates.push_back({name, name, format("emitter object '{0}'", name), false});
    }
    // #define symbols (substitute literal value)
    {
        auto it = definedSymbols.find(name);
        if(it != definedSymbols.end() && !it->second.empty()){
            bool isNumeric = !it->second.empty() && (isdigit(it->second[0]) || (it->second[0] == '-' && it->second.size() > 1));
            string ct = isNumeric ? "intliteral" : "stringliteral";
            candidates.push_back({it->second, ct, format("#define symbol '{0}'", name), false});
        }
    }

    // Dedupe: candidates that resolve to the same I6 emission AND the same type are the same
    // thing seen via multiple lookup paths (e.g. a verb registered both as a verbObjectDef and
    // as a global object stub). They represent one resolution, not two — collapse them.
    {
        vector<Candidate> deduped;
        for(auto& c : candidates){
            bool dup = false;
            for(auto& d : deduped) if(d.qualified == c.qualified && d.type == c.type){ dup = true; break; }
            if(!dup) deduped.push_back(c);
        }
        candidates = deduped;
    }
    // Globals beat enum values: see resolveIdentifierType for rationale.
    {
        bool hasNonEnum = false;
        for(auto& c : candidates) if(!c.isEnum){ hasNonEnum = true; break; }
        if(hasNonEnum){
            vector<Candidate> filtered;
            for(auto& c : candidates) if(!c.isEnum) filtered.push_back(c);
            candidates = filtered;
        }
    }
    // Resolve the candidate set. Cases:
    //   • 1 candidate         → use it.
    //   • 2+ candidates       → try memberHint, then currentExpectedType as tie-breakers.
    //                           If still ambiguous, error and require explicit qualification.
    if(candidates.size() == 1) return candidates[0].qualified;
    if(candidates.size() >= 2){
        if(!memberHint.empty()){
            vector<Candidate*> satisfying;
            for(auto& c : candidates)
                if(typeHasMember(c.type, memberHint)) satisfying.push_back(&c);
            if(satisfying.size() == 1) return satisfying[0]->qualified;
        }
        if(!currentExpectedType.empty()){
            vector<Candidate*> compatible;
            for(auto& c : candidates)
                if(isTypeCompatible(c.type, currentExpectedType)) compatible.push_back(&c);
            if(compatible.size() == 1) return compatible[0]->qualified;
        }
        string msg = format("'{0}' is ambiguous: matches ", name);
        for(size_t i = 0; i < candidates.size(); i++){
            if(i > 0) msg += (i == candidates.size() - 1 ? " and " : ", ");
            msg += candidates[i].origin;
        }
        msg += ". Qualify the use explicitly to disambiguate.";
        parsingError(msg);
    }
    // Loose mode: when parsing #bgl{} content, unknown identifiers are assumed to refer to
    // names in the surrounding I6 stream (locals, globals, properties) rather than Beguile
    // declarations. Pass them through verbatim so the I6 compiler resolves them.
    if(looseIdentifierMode) return name;
    // Property-name fallback: see resolveIdentifierType for rationale. The bare name is
    // emitted verbatim so `obj.provides(weight)` becomes `obj provides weight` in I6.
    if(languageService.isKnownPropertyName(name)) return name;
    // Class-name fallback: see resolveIdentifierType for rationale. Bare class identifiers
    // emit verbatim so `obj.is(Container)` becomes `obj ofclass Container` in I6.
    if(languageService.isKnownClassName(name)) return name;
    return "";
}

// Register a closure capture: creates a global variable for the captured value.
// Returns the global name. Deduplicates: if the same outer variable is captured
// multiple times in the same lambda, returns the existing global.
string bglParser::addCapture(const string& outerName, const string& typeName){
    // Check if already captured in this lambda
    for(auto& cap : currentFunc->captures)
        if(cap.outerName == outerName) return cap.globalName;
    // Warn if capturing a for-loop variable — the round-trip (load/unload) means
    // modifications inside the lambda will affect the loop's progression.
    if(currentLoopVars.count(outerName))
        parsingWarning(format("Lambda captures loop variable '{0}'. Modifications inside the lambda will affect loop progression via capture round-trip.", outerName));
    string globalName = format("_bglCap{0}", languageService.captureCounter++);
    currentFunc->captures.push_back({outerName, globalName, typeName});
    return globalName;
}

classDef* bglParser::getDispatchClass(const string& typeName){
    typeDef& td = languageService.getType(typeName);
    if(auto* cls = dynamic_cast<classDef*>(&td)) return cls;
    if(auto* obj = dynamic_cast<objectDef*>(&td)) return obj->objectClass;

    // Generic specialization fallback: templated names like "array<int>" aren't
    // registered as their own typeDef — only the generic base ("array") is.
    // Strip the <...> suffix and look up the base so dispatch on parametric-typed
    // receivers (`array<int> arr; arr.method()` or `arr[i]`) reaches the generic
    // class. Element-type binding still happens via the elementType parameter
    // threaded through resolveMethod / findArraySubscriptOp.
    auto lt = typeName.find('<');
    if(lt != string::npos && lt > 0){
        typeDef& baseTd = languageService.getType(typeName.substr(0, lt));
        if(auto* cls = dynamic_cast<classDef*>(&baseTd)) return cls;
        if(auto* obj = dynamic_cast<objectDef*>(&baseTd)) return obj->objectClass;
    }
    return nullptr;
}

bool bglParser::isTypeCompatible(std::string argType, std::string paramType){
    if(paramType == "var") return true;  // var accepts any type without checking
    if(argType == "var") return true;    // var is assignable to any type (untyped source)
    if(argType == paramType) return true;
    // bglclass accepts any registered class as an operand. Lets `obj.is(Foo)` work for
    // every class regardless of whether `Foo` resolved as its own type (non-extern,
    // visible in `globals`) or fell through to the isKnownClassName fallback (extern).
    if(paramType == "bglclass" && languageService.isClassType(argType)) return true;
    // bnum → int implicit widening: bnums are int-valued bitmasks by definition.
    // Narrowing (int → bnum) still requires an explicit cast.
    if(paramType == "int"){
        auto* ed = dynamic_cast<enumDef*>(&languageService.getType(argType));
        if(ed && ed->isBnum) return true;
    }
    // bnum → ancestor bnum (via shared-base chain): a child bnum is compatible with any
    // ancestor in its baseBnum chain. Mirrors class subtype compatibility for bnum families.
    {
        auto* argEnum = dynamic_cast<enumDef*>(&languageService.getType(argType));
        auto* paramEnum = dynamic_cast<enumDef*>(&languageService.getType(paramType));
        if(argEnum && argEnum->isBnum && paramEnum && paramEnum->isBnum){
            for(enumDef* a = argEnum->baseBnum; a; a = a->baseBnum)
                if(a == paramEnum) return true;
        }
    }
    // func<...> compatibility: a func value is compatible with any func<...> param type
    if(argType == "func" && paramType.rfind("func<", 0) == 0) return true;
    if(argType.rfind("func<", 0) == 0 && paramType.rfind("func<", 0) == 0) return true;
    // array<T> compatibility: array is compatible with array<T> param
    if(argType == "array" && paramType.rfind("array<", 0) == 0) return true;
    if(argType.rfind("array<", 0) == 0 && paramType.rfind("array<", 0) == 0) return true;
    // ObjectDef → object: every objectDef is implicitly an object, so any objectDef-typed
    // value is assignable to a parameter of type 'object'. Mirrors class-hierarchy compatibility
    // for instance objects that don't have an explicit class declaration.
    if(paramType == "object" && dynamic_cast<objectDef*>(&languageService.getType(argType)) != nullptr)
        return true;
    // Object subtyping is handled by the class hierarchy check below —
    // only classes that actually inherit from 'object' are compatible with it.
    // Class hierarchy: argType is compatible with paramType if argType inherits from paramType
    {
        classDef* argCls2 = getDispatchClass(argType);
        classDef* paramCls2 = getDispatchClass(paramType);
        if(argCls2 && paramCls2){
            std::function<bool(classDef*)> inheritsFrom = [&](classDef* c) -> bool {
                for(classDef* base : c->baseClasses){
                    if(base == paramCls2 || inheritsFrom(base)) return true;
                }
                return false;
            };
            if(inheritsFrom(argCls2)) return true;
        }
    }
    // Block implicit upcast: if argType is an ancestor of paramType, object-level operator= inherited
    // from the base should not be used to allow assigning a less-specific type to a more-specific one.
    // e.g. assigning an 'object' to a 'room' variable must fail even though room inherits object's operator=.
    {
        classDef* argCls3  = getDispatchClass(argType);
        classDef* paramCls3 = getDispatchClass(paramType);
        if(argCls3 && paramCls3 && argCls3 != paramCls3){
            std::function<bool(classDef*)> isAncestorOf = [&](classDef* c) -> bool {
                for(classDef* base : c->baseClasses)
                    if(base == argCls3 || isAncestorOf(base)) return true;
                return false;
            };
            if(isAncestorOf(paramCls3)) return false;
        }
    }
    // Check target type's operator = (argType)
    // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all
    classDef* cls = getDispatchClass(paramType);
    if(cls != nullptr){
        if(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "=" && !fn->params.empty() && fn->params[0]->type.name == argType;
        })) return true;
        if(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "=" && !fn->params.empty() && fn->params[0]->type.name == "var";
        })) return true;
    }
    // Check source type's conversion operator: emitter <paramType> operator()
    classDef* argCls = getDispatchClass(argType);
    if(argCls != nullptr && findMemberInHierarchy(argCls, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty() && !fn->isExplicit && fn->returnType.name == paramType;
    })) return true;
    return false;
}

// Reorder named arguments to match parameter positions. Validates that all named args match valid parameter names.
// Reorder named-argument positions to match the parameter list. Inserts default expressions
// for slots not provided by the caller (named or positional). After this call, `args` and
// `interpSegmentsPerArg` are aligned to the parameter positions and `namedArgNames` is empty.
// Returns false and calls errorFn on any reorder error; returns true on success (including the
// no-named-args fast path which is a no-op).
static bool reorderNamedArgsImpl(vector<expression*>& args, vector<string>& namedArgNames,
                                  vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                  functionDef* fd, function<bool(string)> errorFn){
    bool hasNamed = false;
    for(auto& n : namedArgNames) if(!n.empty()){ hasNamed = true; break; }
    if(!hasNamed) return true;
    vector<expression*> reordered(fd->params.size(), nullptr);
    vector<vector<interpolatedSegment>> reorderedInterp(fd->params.size());
    size_t positionalIdx = 0;
    for(size_t i = 0; i < args.size(); i++){
        if(i < namedArgNames.size() && namedArgNames[i].empty()){
            while(positionalIdx < reordered.size() && reordered[positionalIdx] != nullptr) positionalIdx++;
            if(positionalIdx >= fd->params.size()) { errorFn("Too many positional arguments"); return false; }
            reordered[positionalIdx] = args[i];
            if(i < interpSegmentsPerArg.size()) reorderedInterp[positionalIdx] = interpSegmentsPerArg[i];
            positionalIdx++;
        }
    }
    for(size_t i = 0; i < args.size(); i++){
        if(i < namedArgNames.size() && !namedArgNames[i].empty()){
            bool found = false;
            for(size_t p = 0; p < fd->params.size(); p++){
                if(fd->params[p]->name == namedArgNames[i]){
                    if(reordered[p] != nullptr){ errorFn(format("Parameter '{0}' specified more than once", namedArgNames[i])); return false; }
                    reordered[p] = args[i];
                    if(i < interpSegmentsPerArg.size()) reorderedInterp[p] = interpSegmentsPerArg[i];
                    found = true; break;
                }
            }
            if(!found){ errorFn(format("No parameter named '{0}'", namedArgNames[i])); return false; }
        }
    }
    args.clear();
    interpSegmentsPerArg.clear();
    for(size_t i = 0; i < reordered.size(); i++){
        if(reordered[i] != nullptr){
            args.push_back(reordered[i]);
            interpSegmentsPerArg.push_back(reorderedInterp[i]);
        } else if(!fd->params[i]->defaultValue.empty()){
            expression* defExpr = new expression();
            defExpr->tokens.push_back(fd->params[i]->defaultValue);
            args.push_back(defExpr);
            interpSegmentsPerArg.push_back({});
        } else {
            errorFn(format("Required parameter '{0}' not provided", fd->params[i]->name)); return false;
        }
    }
    namedArgNames.clear();
    return true;
}

// Back-compat wrapper for statement-path callers that hold a functionCallStatement.
void reorderNamedArgs(functionCallStatement& cs, functionDef* fd, function<bool(string)> errorFn){
    reorderNamedArgsImpl(cs.args, cs.namedArgNames, cs.interpSegmentsPerArg, fd, errorFn);
}

void bglParser::applyArgConversions(std::vector<expression*>& args, functionDef* fd){
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++){
        string argType = args[i]->resolvedType;
        string paramType = fd->params[i]->type.name;
        // Verb arguments: prefix with ## for I6 action constant syntax.
        // qualifyIdentifier returns bare verb names (e.g. "examine"); the ## prefix is
        // normally only applied by the verb's operator== emitter, but function args need it too.
        if(argType == "verb" && (paramType == "verb" || paramType == "var")){
            string t = args[i]->text();
            if(t.rfind("##", 0) != 0){  // don't double-prefix
                args[i]->tokens.clear();
                args[i]->tokens.push_back("##" + t);
            }
            continue;
        }
        if(paramType == "var" || argType == paramType || argType.empty()) continue;
        // Look for conversion operator on arg's class
        classDef* argCls = getDispatchClass(argType);
        if(argCls == nullptr) continue;
        typeMember* found = findMemberInHierarchy(argCls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "operator()" && fn->params.empty() && fn->isEmitter && !fn->isExplicit && fn->returnType.name == paramType && dynamic_cast<i6Block*>(fn->body) != nullptr;
        });
        if(found){
            auto* fn = dynamic_cast<functionDef*>(found);
            auto* blk = dynamic_cast<i6Block*>(fn->body);
            string b = processBglConditionals(blk->i6Body);
            { size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
              size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1); }
            if(b.empty()){
                // Empty body = pass-through conversion; just update the type
                args[i]->resolvedType = paramType;
            } else {
                // $self = host of property access (parentProp's `parent($self)` etc.).
                // $val  = full receiver expression as written (`obj.parent`, `5`, `localInt`).
                // For non-property contexts the two coincide.
                string selfText = !args[i]->emitterSelf.empty() ? args[i]->emitterSelf : args[i]->text();
                string valText  = args[i]->text();
                b = i6Emitter::replaceWord(b, "$self", selfText);
                b = i6Emitter::replaceWord(b, "$val",  valText);
                args[i]->tokens.clear();
                args[i]->tokens.push_back(b);
                args[i]->resolvedType = paramType;
            }
        }
    }
}

// Canonicalize a parsed argument list against a resolved function signature.
// Runs the three steps that every call site needs after resolution: named-argument reordering,
// default-value fill, and source-type conversion via operator(). Keeping all three in one place
// prevents the drift that historically let each caller forget one step (e.g. default fill missing
// from the expression path, silently zeroing omitted args at runtime).
void bglParser::finalizeCallArgs(vector<expression*>& args, vector<string>& namedArgNames,
                                  vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                  functionDef* fd){
    if(fd == nullptr) return;
    // Ensure namedArgNames/interpSegmentsPerArg are sized to match args (callers that don't
    // track named args pass empty vectors — pad them so reorder and default-fill can zip).
    while(namedArgNames.size() < args.size()) namedArgNames.push_back("");
    while(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
    reorderNamedArgsImpl(args, namedArgNames, interpSegmentsPerArg, fd,
        [this](string msg){ parsingError(msg); return false; });
    // Default fill for any trailing positional params not yet supplied (reorder only fills
    // when named args are present; this handles the all-positional case).
    for(size_t i = args.size(); i < fd->params.size(); i++){
        if(fd->params[i]->defaultValue.empty())
            parsingError(format("Required parameter '{0}' not provided", fd->params[i]->name));
        expression* defExpr = new expression();
        defExpr->tokens.push_back(fd->params[i]->defaultValue);
        args.push_back(defExpr);
        if(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
    }
    applyArgConversions(args, fd);
}

// Unified dotted-method-call binding. Replaces the duplicated resolve-validate-finalize sequences
// at the statement-level method call, method call in expression, and chain-continuation sites.
functionDef* bglParser::bindMethodCall(string& objType, const string& objPath, const string& methodName,
                                        vector<expression*>& args, vector<string>& namedArgNames,
                                        vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                        const string& elementType){
    MethodMatch mm = resolveMethodWithConversion(objType, objPath, methodName, args, elementType);
    // If type-resolution failed but we have named args and a unique arity-matching candidate,
    // reorder against that candidate's parameter positions and retry. This covers the common
    // case where named args are bound out-of-positional-order: type-check needs the args
    // already aligned to params before isTypeCompatible can validate them.
    if(mm.method == nullptr && mm.arityMatch != nullptr){
        bool hasNamed = false;
        for(auto& n : namedArgNames) if(!n.empty()){ hasNamed = true; break; }
        if(hasNamed){
            while(namedArgNames.size() < args.size()) namedArgNames.push_back("");
            while(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
            bool reorderOk = reorderNamedArgsImpl(args, namedArgNames, interpSegmentsPerArg, mm.arityMatch,
                [](string){ return false; });  // silent — let the second pass produce diagnostics
            if(reorderOk)
                mm = resolveMethodWithConversion(objType, objPath, methodName, args, elementType);
        }
    }
    if(!mm.nameFound)
        parsingError(format("No method '{0}' on type '{1}'", methodName, typeDisplayName(objType)));
    if(mm.nameMatch && !mm.arityMatch){
        size_t req = 0; for(paramDef* p : mm.nameMatch->params) if(p->defaultValue.empty()) req++;
        size_t tot = mm.nameMatch->params.size();
        parsingError(format("Method '{0}' on type '{1}' expects {2} argument(s), but {3} were supplied.\n  Expected: {4}",
            methodName, typeDisplayName(objType),
            (req == tot) ? to_string(tot) : to_string(req) + "-" + to_string(tot),
            args.size(), formatSignature(mm.nameMatch)));
    }
    if(mm.method == nullptr){
        // Loose mode (#bgl{}): if there's exactly one name+arity match, bind to it without
        // type-checking the arguments. The trade-off is documented in spec §15.6 — Beguile
        // names are still found, but argument types pass through to I6 unchecked. Multiple
        // candidates remain ambiguous and fall through to the standard error.
        if(looseIdentifierMode){
            functionDef* sole = nullptr;
            int matchCount = 0;
            std::function<void(classDef*)> gatherLoose = [&](classDef* c){
                if(!c) return;
                for(typeMember* m : c->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == methodName && !fd->isPrePassStub){
                            size_t req = 0;
                            for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                            if(args.size() >= req && args.size() <= fd->params.size()){
                                sole = fd; matchCount++;
                            }
                        }
                for(classDef* base : c->baseClasses) gatherLoose(base);
            };
            if(auto* cls = getDispatchClass(objType))
                gatherLoose(cls);
            if(matchCount == 1){
                mm.method = sole;
                finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, mm.method);
                return mm.method;
            }
        }
        // Build the provided-argument types string and gather candidate overloads (same name,
        // same arity), then for each candidate point at the first mismatching parameter.
        string provided;
        for(size_t i = 0; i < args.size(); i++){
            if(i) provided += ", ";
            string at = args[i]->resolvedType;
            provided += at.empty() ? "?" : typeDisplayName(at);
        }
        vector<functionDef*> candidates;
        std::function<void(classDef*)> gather = [&](classDef* c){
            if(!c) return;
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == methodName && !fd->isPrePassStub){
                        size_t req = 0;
                        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                        if(args.size() >= req && args.size() <= fd->params.size())
                            candidates.push_back(fd);
                    }
            for(classDef* base : c->baseClasses) gather(base);
        };
        if(auto* cls = getDispatchClass(objType))
            gather(cls);
        string detail;
        for(functionDef* fd : candidates){
            detail += "\n  candidate: " + formatSignature(fd);
            for(size_t i = 0; i < args.size(); i++){
                string argType = args[i]->resolvedType;
                string paramType = fd->params[i]->type.name;
                if(paramType == "var" || argType.empty() || isTypeCompatible(argType, paramType)) continue;
                detail += format("\n    arg {0}: '{1}' is not compatible with parameter '{2} {3}'",
                                 i + 1, typeDisplayName(argType),
                                 typeDisplayName(paramType), fd->params[i]->dName());
                break;  // first mismatch is enough — fixing it reveals any later ones
            }
        }
        parsingError(format("No overload of method '{0}' on type '{1}' accepts these argument types.\n  provided: ({2}){3}",
            methodName, typeDisplayName(objType), provided, detail));
    }
    finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, mm.method);
    return mm.method;
}

// Unified global-function-call binding. Replaces the duplicated resolveGlobalCall/validate/finalize
// sequences at the statement-level call site and parseExprFunctionCall.
bglParser::GlobalCallBinding bglParser::bindGlobalCall(const string& name, vector<expression*>& args,
                                                         vector<string>& namedArgNames,
                                                         vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                                         functionDef* func, statementBlock* body){
    GlobalCallBinding out;
    GlobalCallMatch gcm = resolveGlobalCall(name, args, func, body);
    // validateGlobalCall returns the return type string; we discard it here (the caller derives
    // return type from the matched method). validateGlobalCall also throws on invalid calls.
    validateGlobalCall(gcm, name, args.size());
    out.funcVarReturnType = gcm.funcVarReturnType;
    if(gcm.funcVarReturnType.empty() && gcm.match){
        out.method = gcm.match;
        finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, gcm.match);
    }
    return out;
}

// Parses the segments of an interpolated string from the live stream.
// Assumes the '$' token has already been consumed; consumes the opening '"', segments, and closing '"'.
vector<interpolatedSegment> bglParser::parseInterpolatedSegments(functionDef* func, statementBlock* body){
    file.readChar();  // consume opening '"'
    vector<interpolatedSegment> segments;
    string currentStr;
    char c = file.readChar();
    while(c != '"' && c != EOF) {
        if(c == '\\') {
            char nc = file.readChar();
            if     (nc == 'n')  currentStr += '^';      // \n  → I6 newline
            else if(nc == '"')  currentStr += '~';      // \"  → I6 double-quote
            else if(nc == '\\') currentStr += "@@92";   // \\  → literal backslash
            else if(nc == '@')  currentStr += "@@64";   // \@  → literal at-sign
            else if(nc == '{')  currentStr += '{';       // \{  → literal brace (not an expression)
            else if(nc == '$') {                          // \$XX → @{XX} (hex character code)
                string hex;
                while(isxdigit(file.peekChar())) { hex += file.readChar(); }
                currentStr += "@{" + hex + "}";
            }
            else if(isdigit(nc)) {                        // \NNN → @{hex} (decimal character code)
                string dec; dec += nc;
                while(isdigit(file.peekChar())) { dec += file.readChar(); }
                char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "%X", stoi(dec));
                currentStr += "@{"; currentStr += hexBuf; currentStr += "}";
            }
            // ── Diacritical accent shorthands (same as fileLexer.cpp) ──
            // \^^ = forced literal caret, \~~ = forced literal tilde.
            else if(nc == '^') {
                char xc = file.peekChar();
                if(xc == '^') { file.readChar(); currentStr += "@@94"; }
                else if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@^"; currentStr += xc;
                } else { currentStr += "@@94"; }
            }
            else if(nc == '~') {
                char xc = file.peekChar();
                if(xc == '~') { file.readChar(); currentStr += "@@126"; }
                else if(string("anoANO").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@~"; currentStr += xc;
                } else { currentStr += "@@126"; }
            }
            else if(nc == '\'') {
                char xc = file.peekChar();
                if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@'"; currentStr += xc;
                } else { currentStr += '\''; }
            }
            else if(nc == '`') {
                char xc = file.peekChar();
                if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@`"; currentStr += xc;
                } else { currentStr += '`'; }
            }
            else if(nc == ':') {
                char xc = file.peekChar();
                if(string("aeiouyAEIOUY").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@:"; currentStr += xc;
                } else { currentStr += ':'; }
            }
            else if(nc == '/') {
                char xc = file.peekChar();
                if(string("oO").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@\\"; currentStr += xc;
                } else { currentStr += '/'; }
            }
            else if(nc == 'c') {
                char xc = file.peekChar();
                if(xc == 'c' || xc == 'C') {
                    file.readChar(); currentStr += "@c"; currentStr += xc;
                } else { currentStr += 'c'; }
            }
            else if(nc == 'o') {
                char xc = file.peekChar();
                if(xc == 'a' || xc == 'A') {
                    file.readChar(); currentStr += "@o"; currentStr += xc;
                } else { currentStr += 'o'; }
            }
            else if(nc == 's' && file.peekChar()=='s') { file.readChar(); currentStr += "@ss"; }
            else if(nc == 'a' && file.peekChar()=='e') { file.readChar(); currentStr += "@ae"; }
            else if(nc == 'A' && file.peekChar()=='E') { file.readChar(); currentStr += "@AE"; }
            else if(nc == 'O' && file.peekChar()=='E') { file.readChar(); currentStr += "@OE"; }
            else if(nc == 't' && file.peekChar()=='h') { file.readChar(); currentStr += "@th"; }
            else if(nc == 'e' && file.peekChar()=='t') { file.readChar(); currentStr += "@et"; }
            else if(nc == 'L' && file.peekChar()=='L') { file.readChar(); currentStr += "@LL"; }
            else if(nc == '!' && file.peekChar()=='!') { file.readChar(); currentStr += "@!!"; }
            else if(nc == '?' && file.peekChar()=='?') { file.readChar(); currentStr += "@??"; }
            else if(nc == '<' && file.peekChar()=='<') { file.readChar(); currentStr += "@<<"; }
            else if(nc == '>' && file.peekChar()=='>') { file.readChar(); currentStr += "@>>"; }
            else { currentStr += '\\'; currentStr += nc; }
        } else if(c == '{') {
            if(!currentStr.empty()) {
                interpolatedSegment seg;
                seg.isExpr = false;
                seg.text = currentStr;
                segments.push_back(seg);
                currentStr = "";
            }
            token exprFirst = file.getToken();
            expression* exprNode = parseExpression(exprFirst, {"}"}, func, body);
            interpolatedSegment seg;
            seg.isExpr = true;
            seg.expr = exprNode;
            seg.injections = pendingInjections;
            pendingInjections.clear();
            segments.push_back(seg);
        } else {
            currentStr += c;
        }
        c = file.readChar();
    }
    if(!currentStr.empty()) {
        interpolatedSegment seg;
        seg.isExpr = false;
        seg.text = currentStr;
        segments.push_back(seg);
    }
    return segments;
}

// C-style operator precedence: higher number = tighter binding.
// Operators not in this table return -1 and take the single-token RHS fallback path
// (used for compound-assign and other unmanaged ops).
static int operatorPrecedence(const string& op){
    if(op == "*" || op == "/" || op == "%")                                return 11;
    if(op == "+" || op == "-")                                             return 10;
    if(op == "<<" || op == ">>")                                           return  9;
    if(op == "<" || op == "<=" || op == ">" || op == ">=")                 return  8;
    if(op == "==" || op == "!=" || op == "?=" || op == "=~")               return  7;
    if(op == "&")                                                          return  6;
    if(op == "^")                                                          return  5;
    if(op == "|")                                                          return  4;
    if(op == "&&")                                                         return  3;
    if(op == "||")                                                         return  2;
    return -1;
}

// All operators registered in operatorPrecedence() — used when building RHS terminator lists
// so a sub-expression knows when to stop based on an encountered operator's level.
static const vector<string> kPrecedenceOps = {
    "*","/","%","+","-","<<",">>","<","<=",">",">=","==","!=","?=","=~","&","^","|","&&","||"
};

// Build a valid I6 property identifier from an operator symbol, e.g. "=" → "_opeq".
// Used so non-emitter operator routines can be defined as I6 methods and called as
// lhs._opXX(rhs). The mapping must be stable across all callers.
static string mangleOperatorName(const string& opName){
    string safe = "_op";
    for(char ch : opName){
        if     (ch == '=') safe += "eq";
        else if(ch == '~') safe += "tilde";
        else if(ch == '<') safe += "lt";
        else if(ch == '>') safe += "gt";
        else if(ch == '!') safe += "ne";
        else if(ch == '+') safe += "add";
        else if(ch == '-') safe += "sub";
        else if(ch == '*') safe += "mul";
        else if(ch == '/') safe += "div";
        else if(ch == '%') safe += "mod";
        else if(ch == '&') safe += "and";
        else if(ch == '|') safe += "or";
        else if(ch == '^') safe += "xor";
        else if(ch == '?') safe += "qry";
        else if(ch != ' ') safe += ch;
    }
    return safe;
}

// Binary operator resolution: read RHS, find matching emitter, inline.
// Returns true if handled; false if the operator should pass through as raw I6.
bool bglParser::applyBinaryOperator(expression* expr, const string& opName, classDef* cls,
    const vector<string>& terminators, int parenDepth,
    function<token()> getNext, optional<token>& prefetched,
    functionDef* func, statementBlock* body)
{

    // Postfix operators (++ and --): no RHS needed. Find the zero-param emitter and inline.
    if(opName == "++" || opName == "--"){
        functionDef* postfixOp = nullptr;
        if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == opName && fn->params.empty() && fn->isEmitter;
        })) postfixOp = dynamic_cast<functionDef*>(m);
        if(postfixOp != nullptr){
            if(auto* blk = dynamic_cast<i6Block*>(postfixOp->body)){
                string b = processBglConditionals(blk->i6Body);
                string lhsText = expr->text();
                string selfText = !expr->emitterSelf.empty() ? expr->emitterSelf : lhsText;
                b = i6Emitter::replaceWord(b, "$self", selfText);
                b = i6Emitter::replaceWord(b, "$val",  lhsText);
                if(cls != nullptr) b = i6Emitter::replaceWord(b, "$class", cls->i6Name());
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.clear();
                expr->tokens.push_back(b);
            }
            if(!postfixOp->returnType.name.empty()) expr->resolvedType = postfixOp->returnType.name;
            return true;
        }
        // No postfix emitter — pass through as raw I6
        expr->tokens.push_back(opName);
        return true;
    }

    // Step 1: Read RHS token and determine its type and text.
    // For operators with a registered precedence level, parse the RHS as a sub-expression that
    // terminates on any operator with level <= myPrec. This yields standard C-like precedence:
    // `a + b * c` parses as `a + (b * c)` because `*`'s level (11) is higher than `+`'s (10),
    // so `*` is NOT a terminator when we're parsing `+`'s RHS, and gets consumed by the sub-parse.
    // Equal-level ops (e.g. `a + b + c`) terminate, giving left-associative evaluation.
    int myPrec = operatorPrecedence(opName);
    token rhs = getNext();
    string rhsType, rhsText;

    if(myPrec >= 0){
        vector<string> rhsTerminators = terminators;
        for(const string& op : kPrecedenceOps)
            if(operatorPrecedence(op) <= myPrec)
                rhsTerminators.push_back(op);
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        rhsTerminators.push_back("?"); // ternary has lowest precedence — always a terminator
        // Set expected type for the RHS to the LHS class — most operators take same-type args,
        // so this is the right hint for disambiguating an enum-value reference like `fixed`.
        string savedExpectedOp = currentExpectedType;
        if(cls != nullptr) currentExpectedType = cls->name;
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
        currentExpectedType = savedExpectedOp;
        token terminatorTok;
        terminatorTok.value = rhsExpr->terminator;
        terminatorTok.tokenType = eTokenType::oper;
        // Some terminators aren't operators (;, ), }). Preserve their original token type so the
        // outer loop recognises them correctly (e.g. as end-of-statement or close-paren).
        if(rhsExpr->terminator == ";" || rhsExpr->terminator == ")" || rhsExpr->terminator == "?")
            terminatorTok.tokenType = eTokenType::symbol;
        prefetched = terminatorTok;
        rhsType = rhsExpr->resolvedType;
        rhsText = rhsExpr->text();
    }
    else if(rhs.is(eTokenType::integer))         { rhsType="intliteral";    rhsText=rhs.value; }
    else if(rhs.isString())                      { rhsType="stringliteral"; rhsText=rhs.value; }
    else if(rhs.is(eTokenType::charLiteral))     { rhsType="charliteral";   bool bare = (!rhs.value.empty() && all_of(rhs.value.begin(),rhs.value.end(),::isdigit)) || rhs.value.rfind("@",0)==0; rhsText = bare ? rhs.value : "'"+rhs.value+"'"; }
    else if(rhs.is(eTokenType::name) && (file.peekToken().is(token::period) || file.peekToken().is(token::parenOpen))){
        vector<string> rhsTerminators = terminators;
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        string savedExpectedOp2 = currentExpectedType;
        if(cls != nullptr) currentExpectedType = cls->name;
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
        currentExpectedType = savedExpectedOp2;
        token terminatorTok; terminatorTok.value = rhsExpr->terminator;
        prefetched = terminatorTok;
        rhsType = rhsExpr->resolvedType;
        rhsText = rhsExpr->text();
    }
    else if(rhs.is(eTokenType::name)) {
        // Bare identifier RHS — apply the same expected-type context for resolution.
        string savedExpectedOp3 = currentExpectedType;
        if(cls != nullptr) currentExpectedType = cls->name;
        rhsType = resolveIdentifierType(rhs.value, func, body);
        rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value, func, body) : rhs.value;
        currentExpectedType = savedExpectedOp3;
        if(rhsText.empty()) rhsText = rhs.value;
    }
    else if(rhs.is(token::parenOpen)){
        expression* rhsExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
        rhsText = "(" + rhsExpr->text() + ")";
    }
    else if(rhs.is(eTokenType::directive)){
        if(rhs.value.rfind("##", 0) == 0)
            parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly.", rhs.value.substr(2)));
        parsingError(format("Directive '{0}' is not valid in an expression.", rhs.value));
    }

    // Step 2: Find matching operator emitter
    // 'var' is the escape-hatch type — if either side is var, skip param-type checking
    // (treat the same as an empty/unknown rhsType: match by operator name alone).
    functionDef* matchedOp = nullptr;
    if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* m){
        auto* opFn = dynamic_cast<functionDef*>(m);
        if(!opFn || opFn->name != opName) return false;
        // Pre-scan stubs have no params — match by name only
        if(opFn->isPrePassStub) return true;
        return !opFn->params.empty() &&
               (rhsType.empty() || rhsType=="var" || opFn->params[0]->type.name==rhsType || opFn->params[0]->type.name=="var");
    })) matchedOp = dynamic_cast<functionDef*>(m);

    // LHS conversion fallback: if LHS has operator() → convertedType, retry operator search on that type
    if(!matchedOp){
        for(typeMember* m : cls->members){
            auto* convFn = dynamic_cast<functionDef*>(m);
            if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
            string convertedType = convFn->returnType.name;
            classDef* convCls = getDispatchClass(convertedType);
            if(!convCls) continue;
            if(typeMember* m2 = findMemberInHierarchy(convCls, [&](typeMember* m){
                auto* opFn = dynamic_cast<functionDef*>(m);
                if(!opFn || opFn->name != opName) return false;
                if(opFn->isPrePassStub) return true;
                return !opFn->params.empty() &&
                       (rhsType.empty() || rhsType=="var" || opFn->params[0]->type.name==rhsType || opFn->params[0]->type.name=="var"
                        || opFn->params[0]->type.name==convertedType);
            })){
                matchedOp = dynamic_cast<functionDef*>(m2);
                cls = convCls;
                break;
            }
        }
    }

    // Conversion fallback: LHS has operator() returning rhsType → raw I6 compatible
    bool useRawFallback = false;
    if(!matchedOp && !rhsType.empty()){
        if(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* opFn = dynamic_cast<functionDef*>(m);
            return opFn && opFn->name=="operator()" && opFn->params.empty() &&
                   opFn->isEmitter && !opFn->isExplicit && opFn->returnType.name==rhsType;
        })) useRawFallback = true;
    }

    // RHS conversion fallback: RHS type has operator() → type that LHS has the operator for
    if(!matchedOp && !useRawFallback && !rhsType.empty()){
        classDef* rhsCls = getDispatchClass(rhsType);
        if(rhsCls != nullptr){
            for(typeMember* rm : rhsCls->members){
                auto* convFn = dynamic_cast<functionDef*>(rm);
                if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
                string convertedType = convFn->returnType.name;
                if(typeMember* m2 = findMemberInHierarchy(cls, [&](typeMember* m){
                    auto* opFn = dynamic_cast<functionDef*>(m);
                    if(!opFn || opFn->name != opName) return false;
                    if(opFn->isPrePassStub) return true; // pre-scan stub: match by name only
                    return !opFn->params.empty() && opFn->params[0]->type.name == convertedType;
                })){
                    matchedOp = dynamic_cast<functionDef*>(m2);
                    if(auto* convBlk = dynamic_cast<i6Block*>(convFn->body)){
                        string convBody = processBglConditionals(convBlk->i6Body);
                        size_t s = convBody.find_first_not_of(" \t\n\r"); if(s != string::npos) convBody = convBody.substr(s);
                        size_t e = convBody.find_last_not_of(" \t\n\r;"); if(e != string::npos) convBody = convBody.substr(0, e+1);
                        if(!convBody.empty()){
                            convBody = i6Emitter::replaceWord(convBody, "$self", rhsText);
                            convBody = i6Emitter::replaceWord(convBody, "$val",  rhsText);
                            rhsText = convBody;
                        }
                    }
                    break;
                }
            }
        }
    }

    if(!matchedOp && !useRawFallback && !rhsType.empty())
        parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'", opName, cls->dName(), typeDisplayName(rhsType)));

    if(matchedOp && !matchedOp->returnType.name.empty())
        expr->resolvedType = matchedOp->returnType.name;
    else if(matchedOp && matchedOp->isPrePassStub){
        // Pre-scan stub: no return type info. Infer from operator category.
        static const vector<string> comparisonOps = {"==","!=","<",">","<=",">=","?=","=~"};
        static const vector<string> logicalOps = {"&&","||"};
        if(find(comparisonOps.begin(), comparisonOps.end(), opName) != comparisonOps.end()) expr->resolvedType = "ebool";
        else if(find(logicalOps.begin(), logicalOps.end(), opName) != logicalOps.end()) expr->resolvedType = "ebool";
    }

    // Step 3: Apply the result
    i6Block* blk = (matchedOp && matchedOp->isEmitter && !matchedOp->isPrePassStub) ? dynamic_cast<i6Block*>(matchedOp->body) : nullptr;
    if(useRawFallback){
        // Comparison operators produce eBool; others preserve the LHS type
        static const vector<string> comparisonOps = {"==","!=","<",">","<=",">=","?=","=~"};
        if(find(comparisonOps.begin(), comparisonOps.end(), opName) != comparisonOps.end())
            expr->resolvedType = "ebool";
        string lhsText = expr->text();
        expr->tokens.clear();
        expr->tokens.push_back(lhsText + opName + rhsText);
    } else if(blk != nullptr){
        // For identifier RHS, check if it's a function call and collect full text.
        // Skip this when the RHS was already fully resolved by the precedence sub-parse
        // (myPrec >= 0), since rhsText is complete and reading further would corrupt the stream.
        if(myPrec < 0 && rhs.is(eTokenType::name)){
            token rhsNext = getNext();
            if(rhsNext.is(token::parenOpen)){
                rhsText = rhs.value + token::parenOpen;
                int callDepth = 1;
                token argTok = file.getToken();
                while(callDepth > 0){
                    if(argTok.is(token::parenOpen)) callDepth++;
                    else if(argTok.is(token::parenClose)){ callDepth--; if(callDepth==0) break; }
                    rhsText += argTok.value;
                    if(callDepth > 0) argTok = file.getToken();
                }
                rhsText += token::parenClose;
            } else {
                prefetched = rhsNext;
            }
        }
        // Separate leading structural tokens (parens) from the actual operand
        vector<string> prefix;
        while(expr->tokens.size() > 1 && expr->tokens.front() == "(")
            { prefix.push_back(expr->tokens.front()); expr->tokens.erase(expr->tokens.begin()); }
        string lhsText = expr->text();
        string b = processBglConditionals(blk->i6Body);
        { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
          size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1); }
        // Substitute parameters before $self to avoid double-substitution when
        // a parameter name matches the LHS identifier (e.g. emitter body "$self>=c"
        // with $self=c and param c='0' would incorrectly become '0'>='0' if $self is first).
        b = replaceWord(b, "$" + matchedOp->params[0]->name, rhsText);
        // $self = host of property access (parentProp's `parent($self) == $v` etc.).
        // $val  = full receiver expression as written (`obj.parent`, `5`, `localInt`).
        // For non-property contexts the two coincide.
        string selfText = !expr->emitterSelf.empty() ? expr->emitterSelf : lhsText;
        b = replaceWord(b, "$self", selfText);
        b = replaceWord(b, "$val",  lhsText);
        if(cls != nullptr) b = replaceWord(b, "$class", cls->i6Name());
        expr->tokens.clear();
        for(auto& p : prefix) expr->tokens.push_back(p);
        expr->tokens.push_back(b);
    } else if(matchedOp && !matchedOp->isEmitter && !matchedOp->isPrePassStub){
        // Non-emitter operator: emit as a method call on the LHS using a mangled property name
        string lhsText = expr->text();
        if(matchedOp->i6name.empty()) matchedOp->i6name = mangleOperatorName(matchedOp->name);
        if(matchedOp->returnType.name.empty() || matchedOp->returnType.name == "void")
            expr->resolvedType = cls->name;
        else
            expr->resolvedType = matchedOp->returnType.name;
        expr->tokens.clear();
        expr->tokens.push_back(lhsText + "." + matchedOp->i6name + "(" + rhsText + ")");
    } else {
        // No operator found on this type
        parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'",
            opName, cls->dName(), typeDisplayName(rhsType.empty() ? "unknown" : rhsType)));
    }
    return true;
}

// ── Shared: parse qualifier keywords in any order ────────────────────────────
// Consumes qualifier tokens from the stream; leaves tok at the first non-qualifier.
// Validates nonsensical combinations after all qualifiers are collected.
Qualifiers bglParser::parseQualifiers(token& tok){
    Qualifiers q;
    // Forward any doc-comment from a consumed qualifier to the next token, so a doc that
    // preceded `extern class Foo` ends up on `class` rather than getting lost with `extern`.
    auto advance = [&]() {
        string carriedDoc = tok.docComment;
        tok = file.getToken();
        if(tok.docComment.empty()) tok.docComment = carriedDoc;
    };
    while(true){
        if     (tok.is(token::replace))            { q.isReplace  = true; advance(); }
        else if(tok.is("explicit"))                { q.isExplicit = true; advance(); }
        else if(tok.is(token::external))           { q.isExtern   = true; advance(); }
        else if(tok.is("emitter"))                 { q.isEmitter  = true; advance(); }
        else if(tok.is(token::constantDeclararion)){ q.isConst    = true; advance(); }
        else if(tok.is("static"))                  { q.isStatic   = true; advance(); }
        else if(tok.is(token::extend))             { q.isExtend   = true; advance(); }
        else if(tok.is("alias"))                   { q.isAlias    = true; advance(); }
        else if(tok.is("default"))                 { q.isDefault  = true; advance(); }
        else break;
    }
    // Validate nonsensical combinations
    if(q.isConst && q.isStatic)
        parsingError("A member cannot be both 'const' and 'static'");
    if(q.isStatic && q.isEmitter)
        parsingError("A static member cannot be an emitter");
    if(q.isExplicit && (q.isConst || q.isStatic))
        parsingError("'explicit' cannot be combined with 'const' or 'static'");
    if(q.isExtern && q.isEmitter && q.isExtend)
        parsingError("'extern', 'emitter', and 'extend' cannot all be combined");
    if(q.isAlias && q.isExtern)
        parsingError("'alias' and 'extern' are mutually exclusive");
    if(q.isAlias && q.isEmitter)
        parsingError("'alias' and 'emitter' are mutually exclusive");
    return q;
}

// ── parseExpression sub-functions ─────────────────────────────────────────────

// Ternary operator: condition ? trueExpr : falseExpr
// Lowers to if/else injection using a unique _bgl_tempN. Replaces expr contents and sets terminator.
void bglParser::parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body, int outerParenDepth){
    string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
    string condText = expr->text();
    expression* trueExpr  = parseExpression(file.getToken(), {":"}, func, body);
    expression* falseExpr = parseExpression(file.getToken(), terminators, func, body);
    string injText = "if (" + condText + ") " + tempName + " = " + trueExpr->text()
                   + "; else " + tempName + " = " + falseExpr->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    expr->tokens.push_back(tempName);
    expr->resolvedType = !trueExpr->resolvedType.empty() ? trueExpr->resolvedType : falseExpr->resolvedType;
    expr->terminator = falseExpr->terminator;
}

// Null coalescing: lhs ?? fallback
// Lowers to if injection using a unique _bgl_tempN and operator?(). Replaces expr contents and sets terminator.
void bglParser::parseExprNullCoalescing(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body){
    string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
    string lhsText = expr->text();
    string lhsType = expr->resolvedType;
    classDef* lhsCls = !lhsType.empty() ? getDispatchClass(lhsType) : nullptr;
    functionDef* nullTestFn = nullptr;
    if(lhsCls != nullptr)
        nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(lhsCls, [](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
        }));
    if(nullTestFn == nullptr)
        parsingError(format("Type '{0}' does not support null coalescing (no operator?() emitter)", lhsType));
    auto* blk = dynamic_cast<i6Block*>(nullTestFn->body);
    string nullTest = processBglConditionals(blk->i6Body);
    nullTest = replaceWord(nullTest, "$self", tempName);
    nullTest = replaceWord(nullTest, "$val",  tempName);
    { size_t s=nullTest.find_first_not_of(" \t\n\r"); if(s!=string::npos) nullTest=nullTest.substr(s);
      size_t e=nullTest.find_last_not_of(" \t\n\r;"); if(e!=string::npos) nullTest=nullTest.substr(0,e+1); }
    expression* fallback = parseExpression(file.getToken(), terminators, func, body);
    string injText = tempName + " = " + lhsText + "; if (~~(" + nullTest + ")) " + tempName + " = " + fallback->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    expr->tokens.push_back(tempName);
    if(!fallback->resolvedType.empty()) expr->resolvedType = fallback->resolvedType;
    expr->terminator = fallback->terminator;
}

// Function call in expression context: parses args, resolves global/self call,
// validates arity+types, inlines emitters. Returns true if an emitter was inlined
// (caller should 'continue' to skip getNext at loop bottom).
bool bglParser::parseExprFunctionCall(expression* expr, const string& callName, bool isSelfCall,
                                       functionDef* func, statementBlock* body){
    // Parse args as proper expressions (shared with statement-level path)
    ParsedArgList pal = parseCallArgList(func, body);
    // Resolve and validate
    string retType;
    if(isSelfCall){
        // Recursive self-call: currentFunc is the function being parsed and isn't yet in
        // currentObject->members. Use it directly for retType when names match.
        if(currentFunc != nullptr && currentFunc->name == callName)
            retType = currentFunc->returnType.name;
        if(retType.empty() && currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == callName){ retType = fd->returnType.name; break; }
    } else {
        GlobalCallBinding gcb = bindGlobalCall(callName, pal.args, pal.namedArgNames,
                                                 pal.interpSegmentsPerArg, func, body);
        if(!gcb.funcVarReturnType.empty())      retType = gcb.funcVarReturnType;
        else if(gcb.method != nullptr)          retType = gcb.method->returnType.name;
        else                                    retType = "var"; // loose mode: unresolved → opaque
        // Emitter inlining: substitute params and push as single token
        if(gcb.method && gcb.method->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
                string b = processBglConditionals(blk->i6Body);
                for(size_t i = 0; i < gcb.method->params.size() && i < pal.args.size(); i++){
                    // Raw bodies preserve original case; substitute both forms.
                    paramDef* p = gcb.method->params[i];
                    const string& display = p->displayName.empty() ? p->name : p->displayName;
                    b = replaceWord(b, "$" + display, pal.args[i]->text());
                    if(display != p->name)
                        b = replaceWord(b, "$" + p->name, pal.args[i]->text());
                }
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.push_back(b);
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                return true; // emitter inlined — caller should continue
            }
        }
    }
    if(retType == "void" && !allowVoidReturnExpr)
        parsingError(format("Cannot use void function '{0}' in an expression", callName));
    if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
    // Flatten parsed args back to tokens for the enclosing expression
    expr->tokens.push_back(isSelfCall ? "self." + callName : callName);
    expr->tokens.push_back(token::parenOpen);
    for(size_t i = 0; i < pal.args.size(); i++){
        if(i > 0) expr->tokens.push_back(",");
        expr->tokens.push_back(pal.args[i]->text());
    }
    expr->tokens.push_back(token::parenClose);
    return false;
}

// Prefix logical-not: handles !name, !name? (negated query), and fallback to ~~.
// operand is the token following '!'. Sets prefetched if the operand needs re-processing.
bool bglParser::parseExprPrefixNot(expression* expr, token operand, optional<token>& prefetched,
                                    functionDef* func, statementBlock* body){
    if(operand.is(eTokenType::name)){
        string opType = resolveIdentifierType(operand.value, func, body);
        string opText = (func != nullptr) ? qualifyIdentifier(operand.value, func, body) : operand.value;
        if(opText.empty()) opText = operand.value;
        // Check for !v? (negated postfix query) — only if type has operator?()
        if(file.peekToken(1).is("?")){
            classDef* cls = getDispatchClass(opType);
            functionDef* queryFn = nullptr;
            if(cls != nullptr)
                queryFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
            if(queryFn != nullptr){
                file.getToken(); // consume '?'
                auto* blk = dynamic_cast<i6Block*>(queryFn->body);
                string b = processBglConditionals(blk->i6Body);
                b = replaceWord(b, "$self", opText);
                b = replaceWord(b, "$val",  opText);
                { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                  size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1); }
                expr->tokens.push_back("~~(" + b + ")");
                if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
                return true;
            }
        }
        // Try operator! emitter on the type
        classDef* cls = getDispatchClass(opType);
        if(cls){
            if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* tm){
                auto* fn = dynamic_cast<functionDef*>(tm);
                return fn && fn->name == "!" && fn->params.empty() && fn->isEmitter;
            })){
                functionDef* notOp = dynamic_cast<functionDef*>(m);
                i6Block* blk = dynamic_cast<i6Block*>(notOp->body);
                if(blk){
                    string b = processBglConditionals(blk->i6Body);
                    size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                    size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
                    b = replaceWord(b, "$self", opText);
                    b = replaceWord(b, "$val",  opText);
                    expr->tokens.push_back(b);
                    if(expr->resolvedType.empty()) expr->resolvedType = notOp->returnType.name;
                    return true;
                }
            }
        }
    }
    // Fallback: emit ~~ (I6 NOT) and put operand back for normal processing
    expr->tokens.push_back("~~");
    prefetched = operand;
    return true; // always handled (either emitter or fallback)
}

// ── Namespace-scoped type resolution ─────────────────────────────────────────
// Walks a dotted path (e.g. "bgl.glulx.window") through namespace objects.
// At each intermediate step, follows value alias members (auto x = Obj;) to the
// next namespace object. At the final step, looks for an alias member (isAlias=true)
// whose type is a registered class. Returns the flat type name or "".
string bglParser::resolveNamespacedType(const string& dottedPath){
    size_t dot = dottedPath.find('.');
    if(dot == string::npos) return "";
    string head = dottedPath.substr(0, dot);
    string rest = dottedPath.substr(dot + 1);
    // Find starting object — check globals first, then #using imported scopes
    objectDef* curObj = nullptr;
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            if(od->name == head){ curObj = od; break; }
    // If not a global, check #using imports: the head might be a member of an imported scope.
    // e.g., after `#using bgl;`, head="glulx" is a member of the bgl object.
    if(!curObj){
        for(objectDef* imp : usingObjectImports){
            for(typeMember* m : imp->members){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(!vd || vd->name != head) continue;
                // Follow the member to find the target object by initializer name
                string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                if(!initName.empty())
                    for(typeDef* g : languageService.globals)
                        if(auto* od = dynamic_cast<objectDef*>(g))
                            if(od->name == initName){ curObj = od; break; }
                if(!curObj){
                    // Fall back to type name
                    for(typeDef* g : languageService.globals)
                        if(auto* od = dynamic_cast<objectDef*>(g))
                            if(od->name == vd->type.name){ curObj = od; break; }
                }
                if(curObj) break;
            }
            if(curObj) break;
        }
    }
    if(!curObj){
        // Also check class (for emitter namespace objects registered as classes)
        classDef* cls = getDispatchClass(head);
        if(cls != nullptr){
            // Walk class members for the next segment
            while(!rest.empty()){
                dot = rest.find('.');
                string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
                rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                for(typeMember* m : cls->members){
                    auto* vd = dynamic_cast<variableDeclaration*>(m);
                    if(vd && vd->name == seg){
                        if(rest.empty() && vd->isAlias && languageService.isClassType(vd->type.name))
                            return vd->type.name;
                        cls = getDispatchClass(vd->type.name);
                        break;
                    }
                }
                if(!cls) return "";
            }
            return "";
        }
        return "";
    }
    // Walk object members
    while(!rest.empty()){
        dot = rest.find('.');
        string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
        rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
        bool found = false;
        for(typeMember* m : curObj->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(!vd || vd->name != seg) continue;
            if(rest.empty()){
                // Final segment: only accept explicit type aliases (isAlias=true).
                // Without this guard, any member whose type is a class would match
                // (e.g., `obj.capacity` where capacity is int — int is a class).
                if(vd->isAlias && languageService.isClassType(vd->type.name))
                    return vd->type.name;
                return "";
            }
            // Intermediate: follow to next object or class.
            // For auto members on non-emitter objects, the type might be "object" (base class)
            // rather than the specific object type. Try matching by member's initializer name first,
            // then fall back to the type name.
            string nextType = vd->type.name;
            // If the member has a declared initializer pointing to an object, use that
            string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
            curObj = nullptr;
            // Try init name first (e.g., auto glulx = _bglGlulx → initName="_bglglulx")
            if(!initName.empty())
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == initName){ curObj = od; break; }
            // Fall back to type name
            if(!curObj)
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == nextType){ curObj = od; break; }
            if(!curObj){
                // Try class
                classDef* nextCls = getDispatchClass(nextType);
                if(nextCls){
                    // Switch to class-walking for the rest
                    while(!rest.empty()){
                        dot = rest.find('.');
                        string innerSeg = (dot == string::npos) ? rest : rest.substr(0, dot);
                        rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                        bool innerFound = false;
                        for(typeMember* im : nextCls->members){
                            auto* iv = dynamic_cast<variableDeclaration*>(im);
                            if(iv && iv->name == innerSeg){
                                if(rest.empty() && iv->isAlias && languageService.isClassType(iv->type.name))
                                    return iv->type.name;
                                nextCls = getDispatchClass(iv->type.name);
                                innerFound = true;
                                break;
                            }
                        }
                        if(!innerFound) return "";
                    }
                }
                return "";
            }
            found = true;
            break;
        }
        if(!found) return "";
    }
    return "";
}

// Peek ahead to check if the given token starts a valid namespace type path.
// Non-consuming — uses file.peekToken(N).
bool bglParser::isNamespacedTypePath(token firstTok){ 
    if(!firstTok.is(eTokenType::identifier)) return false;
    // Build the dotted path by peeking ahead
    string path = firstTok.value;
    int peekIdx = 1;
    while(file.peekToken(peekIdx).is(token::period)){
        token seg = file.peekToken(peekIdx + 1);
        if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
        path += "." + seg.value;
        peekIdx += 2;
    }
    if(path.find('.') == string::npos) return false;
    string resolved = resolveNamespacedType(path);
    return !resolved.empty();
}

// If first token is an identifier starting a namespace type path, consume the
// dot-delimited tokens and return a synthetic dataType token with the resolved flat name.
// If not a namespace path, return first unchanged.
token bglParser::consumeTypeToken(token first){
    if(!first.is(eTokenType::identifier))
        return first;
    // Check #using imported alias members (bare type from import)
    if(!file.peekToken().is(token::period)){
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isAlias && vd->name == first.value && languageService.isClassType(vd->type.name)){
                        first.value = vd->type.name;
                        first.tokenType = eTokenType::dataType;
                        return first;
                    }
        return first;
    }
    // Build the path
    string path = first.value;
    while(file.peekToken().is(token::period)){
        token dot = file.peekToken();
        token seg = file.peekToken(2);
        if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
        file.getToken(); // consume '.'
        file.getToken(); // consume segment
        path += "." + seg.value;
    }
    string resolved = resolveNamespacedType(path);
    if(resolved.empty())
        parsingError(format("'{0}' does not resolve to a type", path));
    token result = first;
    result.value = resolved;
    result.tokenType = eTokenType::dataType;
    return result;
}

// Try to resolve an expression-position namespaced enum value access, e.g.
// `bgl.glulx.winPlacement.above` where `bgl.glulx.winPlacement` namespace-resolves to the
// enum/bnum type `bglulxwindowplacement` and `above` is one of its named values.
// On success consumes the dot-delimited tokens after `first` and fills out params; on
// failure consumes nothing and returns false (caller falls through to normal handling).
bool bglParser::tryConsumeNamespacedEnumValue(token first, string& outFlatEmission, string& outEnumType){
    if(!first.is(eTokenType::identifier)) return false;
    if(!file.peekToken(1).is(token::period)) return false;  // must have at least one dot

    // Peek ahead to collect the full dotted path and its per-segment peek indices.
    // Segments alternate with periods: identifier . identifier . identifier …
    vector<string> segments;
    segments.push_back(first.value);
    int peekIdx = 1;
    while(file.peekToken(peekIdx).is(token::period)){
        token seg = file.peekToken(peekIdx + 1);
        if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
        segments.push_back(seg.value);
        peekIdx += 2;
    }
    if(segments.size() < 3) return false;  // need at least A.B.value — namespace + type + value

    // Strip the last segment and attempt namespace type resolution on the prefix.
    string valueName = segments.back();
    transform(valueName.begin(), valueName.end(), valueName.begin(), ::tolower);
    string prefixPath;
    for(size_t i = 0; i + 1 < segments.size(); i++){
        if(i) prefixPath += ".";
        prefixPath += segments[i];
    }
    string enumTypeName = resolveNamespacedType(prefixPath);
    if(enumTypeName.empty()){
        // Prefix didn't resolve as a namespace-to-type path. If the path *looks* like a
        // namespace access (head is a global object and the second segment is an alias /
        // nested-namespace auto member), walk segment-by-segment to locate the bad
        // segment and emit a specific diagnostic — much more useful than letting the
        // broken path flow into operator resolution and surface as 'no operator on object'.
        objectDef* rootObj = nullptr;
        for(typeDef* g : languageService.globals)
            if(auto* od = dynamic_cast<objectDef*>(g))
                if(od->name == first.value){ rootObj = od; break; }
        if(!rootObj) return false;
        // Second segment must be a member of rootObj — otherwise this isn't namespace-style;
        // let normal property-chain handling take over.
        string seg1Lower = segments[1]; transform(seg1Lower.begin(), seg1Lower.end(), seg1Lower.begin(), ::tolower);
        bool seg1IsMember = false;
        for(typeMember* m : rootObj->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == seg1Lower){ seg1IsMember = true; break; }
        if(!seg1IsMember) return false;
        // Walk: find the first segment that fails to resolve as a namespace step.
        objectDef* curObj = rootObj;
        string prefixSoFar = segments[0];
        for(size_t i = 1; i < segments.size(); i++){
            string segLower = segments[i]; transform(segLower.begin(), segLower.end(), segLower.begin(), ::tolower);
            variableDeclaration* foundMember = nullptr;
            functionDef* foundMethod = nullptr;
            for(typeMember* m : curObj->members){
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == segLower){ foundMember = vd; break; }
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == segLower){ foundMethod = fd; break; }
            }
            // If the segment is a method on this object, it can only appear as the final
            // segment (followed by a call). Hand off to normal expression handling — that's
            // already wired to dispatch namespace-method calls.
            if(foundMethod) return false;
            if(!foundMember)
                parsingError(format("'{0}' is not a member of '{1}'", segments[i], prefixSoFar));
            // If this is an alias member, the next segment should be an enum value — stop walking.
            if(foundMember->isAlias){
                auto* en = dynamic_cast<enumDef*>(&languageService.getType(foundMember->type.name));
                if(en && i + 1 < segments.size()){
                    string valLower = segments[i+1]; transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
                    bool valExists = false;
                    for(enumValueDef* ev : en->namedValues)
                        if(ev->name == valLower){ valExists = true; break; }
                    if(!valExists)
                        parsingError(format("'{0}' is not a named value of enum/bnum '{1}' (via {2}.{3})",
                                            segments[i+1], foundMember->type.name, prefixSoFar, segments[i]));
                }
                // Alias resolved — fall through to normal handling (unlikely if we got here).
                return false;
            }
            // Auto member / property — follow to its target object to keep walking.
            string initName = foundMember->declaredExpressionValue ? foundMember->declaredExpressionValue->text() : "";
            objectDef* next = nullptr;
            if(!initName.empty())
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == initName){ next = od; break; }
            if(!next)
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == foundMember->type.name){ next = od; break; }
            if(!next) return false;  // non-namespace member — let normal handling proceed
            curObj = next;
            prefixSoFar += "." + segments[i];
        }
        return false;
    }
    auto* ed = dynamic_cast<enumDef*>(&languageService.getType(enumTypeName));
    if(!ed) return false;  // prefix resolved to a class (e.g. window alias), not an enum — let normal handling take over

    // Confirm the final segment is a named value of this enum and capture its value
    int matchedValue = 0;
    bool matched = false;
    for(enumValueDef* ev : ed->namedValues)
        if(ev->name == valueName){ matched = true; matchedValue = ev->value; break; }
    if(!matched)
        parsingError(format("'{0}' is not a named value of enum/bnum '{1}' (via {2})",
                            segments.back(), enumTypeName, prefixPath));

    // Commit: consume all tokens after `first` (segments[1..end] each preceded by '.')
    for(size_t i = 1; i < segments.size(); i++){
        file.getToken();  // consume '.'
        file.getToken();  // consume segment
    }
    // Inline the integer value for non-extern enums; extern enums keep the name (I6 keyword semantics).
    outFlatEmission = ed->isExternal ? valueName : to_string(matchedValue);
    outEnumType = enumTypeName;
    return true;
}

expression* bglParser::parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body, int startParenDepth){
    expression* expr = new expression();
    int parenDepth = startParenDepth;
    token cur = firstToken;
    optional<token> prefetched = nullopt;
    string castType;  // set when a (TypeName) cast prefix is detected
    // RAII guard for currentExpectedType. parseExpression's body may set the expected type
    // (e.g. when entering an operator RHS). The guard restores it on any return path so
    // changes don't leak to the caller's scope.
    struct ExpectedTypeGuard {
        string& slot; string saved;
        ExpectedTypeGuard(string& s) : slot(s), saved(s) {}
        ~ExpectedTypeGuard() { slot = saved; }
    } _expectedTypeGuard(currentExpectedType);

    auto isTerminator = [&](const token& t) -> bool {
        if(parenDepth > startParenDepth) return false;  // inside our own parens — not a terminator
        for(const string& term : terminators)
            if(t.value == term) return true;
        return false;
    };
    auto getNext = [&]() -> token {
        if(prefetched.has_value()){ token t = *prefetched; prefetched = nullopt; return t; }
        if(stashedToken.has_value()){ token t = *stashedToken; stashedToken = nullopt; return t; }
        return file.getToken();
    };

    // Pending ternary state: when '?' is encountered, the condition and true branch are
    // captured, then the false branch is collected by continuing the main loop. This avoids
    // sub-parsing the false branch, which would lose the caller's paren tracking state.
    struct PendingTernary {
        string condText;
        string trueText;
        string trueType;
        string tempName;
        int parenDepthAtQuestion;  // paren depth when '?' was encountered
        vector<string> prefixParens;  // structural '(' tokens to restore after assembly
    };
    vector<PendingTernary> pendingTernaries;

    // Helper: assemble a pending ternary — creates injection, replaces expr with temp name
    auto assembleTernary = [&](){
        if(pendingTernaries.empty()) return;
        PendingTernary pt = pendingTernaries.back();
        pendingTernaries.pop_back();
        string falseText = expr->text();
        string falseType = expr->resolvedType;
        string injText = "if (" + pt.condText + ") " + pt.tempName + " = " + pt.trueText
                       + "; else " + pt.tempName + " = " + falseText + ";";
        i6RawNode* inj = new i6RawNode();
        inj->text = injText;
        pendingInjections.push_back(inj);
        expr->tokens.clear();
        // Restore structural prefix parens so the outer expression stays balanced
        for(auto& p : pt.prefixParens) expr->tokens.push_back(p);
        expr->tokens.push_back(pt.tempName);
        expr->resolvedType = !pt.trueType.empty() ? pt.trueType : falseType;
    };

    // ═══════════════════════════════════════════════════════════════════════
    // MAIN EXPRESSION LOOP — processes one token per iteration
    // Branches by token type: parens, literals, identifiers, operators, etc.
    // ═══════════════════════════════════════════════════════════════════════
    while(true){
        if(isTerminator(cur)){ expr->terminator = cur.value; break; }
        if(cur.is(eTokenType::eof)){
            parsingError("Unexpected end of file inside expression");
        }

        // ─── PARENS: open paren, lambda detection, cast prefix ───────────
        if(cur.is(token::parenOpen)){
            // Lambda detection: () => ... OR (type name, ...) => ...
            {
                bool isLambda = false;
                token p1 = file.peekToken(1);
                if(p1.is(")") && file.peekToken(2).is("=>"))
                    isLambda = true;
                else if((p1.is(eTokenType::dataType) || p1.is(eTokenType::identifier)) && file.peekToken(2).is(eTokenType::identifier))
                    isLambda = true;
                if(isLambda){
                    string lambdaName = parseLambdaExpr(func, body);
                    expr->tokens.push_back(lambdaName);
                    expr->resolvedType = "func";
                    cur = getNext();
                    continue;
                }
            }
            // Check for cast expression: (TypeName)expr
            if(file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
                castType = file.getToken(eTokenType::dataType).value;
                file.getToken(token::parenClose);
                cur = getNext();
                continue;  // re-process the token after the cast with castType set
            }
            parenDepth++;
            expr->tokens.push_back(cur.value);
        }
        // ─── CLOSE PAREN ──────────────────────────────────────────────────
        else if(cur.is(token::parenClose)){
            if(parenDepth > startParenDepth) parenDepth--;
            // If a pending ternary was opened at this depth, assemble it now.
            // This handles `(cond ? a : b)` — the ')' closes the false branch.
            if(!pendingTernaries.empty() && parenDepth <= pendingTernaries.back().parenDepthAtQuestion){
                assembleTernary();
            }
            expr->tokens.push_back(cur.value);
        }
        // ─── LITERALS: integer, string, char, dictionary word ─────────────
        else if(cur.is(eTokenType::integer)){
            if(!castType.empty()){
                expr->resolvedType = castType; castType = "";
            } else if(expr->resolvedType.empty()){
                // If the expression has a leading unary '-', this is a negative literal
                bool isNegated = (expr->tokens.size() == 1 && expr->tokens[0] == "-");
                expr->resolvedType = isNegated ? "negativeintliteral" : "intliteral";
            }
            expr->tokens.push_back(cur.value);
        }
        else if(cur.isString()){
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
            expr->tokens.push_back(cur.value);
        }
        // `new TypeName(args)` — pool-class allocation. Emits as `TypeName.create(args)`.
        // The type must be a pooled class: declared with `[N]` or marker `extern class Foo[]`.
        // Returns `nothing` at runtime if the pool is exhausted.
        else if(cur.is("new")){
            token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            classDef* cls = getDispatchClass(typeTok.value);
            if(cls == nullptr)
                parsingError(format("'new {0}': '{0}' is not a class", typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
            if(cls->poolSize == 0)
                parsingError(format("'new {0}': class is not pooled. Declare with `class {0}[N]` (sized pool) or `extern class {0}[]` (extern marker) to enable allocation.", cls->dName()));
            file.getToken(token::parenOpen);
            ParsedArgList pal = parseCallArgList(func, body);
            string call = cls->i6Name() + ".create(";
            for(size_t i = 0; i < pal.args.size(); i++){
                if(i > 0) call += ", ";
                call += pal.args[i]->text();
            }
            call += ")";
            expr->tokens.push_back(call);
            if(expr->resolvedType.empty()) expr->resolvedType = cls->name;
        }
        // ═── IDENTIFIER: the largest branch ═════════════════════════════
        // Handles: subscript name[i], function call name(args), optional
        // chain name?., dot-access name.member/name.method(), postfix
        // query name?, and plain identifier fallback.
        // ═════════════════════════════════════════════════════════════════
        else if(cur.is(eTokenType::name)){
            // Namespaced enum value: bgl.glulx.winPlacement.above — resolve the prefix as a
            // namespace-scoped type, then bind the tail segment as one of the enum's named values.
            {   string emission, enumType;
                if(tryConsumeNamespacedEnumValue(cur, emission, enumType)){
                    if(expr->resolvedType.empty()) expr->resolvedType = enumType;
                    expr->tokens.push_back(emission);
                    cur = getNext();  // advance past the consumed value — the loop's normal getNext() is skipped by our continue
                    continue;
                }
            }
            token next = getNext();
            // ── name[i]: subscript access ──
            if(next.is(token::bracketOpen)){
                string arrName = cur.value;
                string arrType = resolveIdentifierType(arrName, func, body);
                classDef* arrCls = getDispatchClass(arrType);
                expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                // Element-type-aware lookup: find operator[] whose return type matches the
                // array's declared element type. byteArray (array<char>) keeps its char operator[];
                // other arrays have int/object/bool/string overloads plus implicit synthesis for
                // user classes.
                string elemType = resolveArrayElementType(arrName, func, body);
                if(elemType.empty() && arrType == "bytearray") elemType = "char";
                functionDef* getMethod = nullptr;
                if(arrCls != nullptr && !elemType.empty())
                    getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
                if(getMethod == nullptr) {
                    if(elemType.empty())
                        parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrName));
                    parsingError(format("No operator[] returning '{0}' on type '{1}'. Add an overload or use a supported element type.",
                        typeDisplayName(elemType), typeDisplayName(arrType)));
                }
                if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                string subscriptText;
                if(getMethod->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                        string b = processBglConditionals(blk->i6Body);
                        string pv = (arrType == "array" || arrType == "bytearray") ? "0" : "<$prop undefined>";
                        b = replaceWord(b, "$self", arrName);
                        b = replaceWord(b, "$val",  arrName);
                        b = replaceWord(b, "$prop", pv);
                        if(!getMethod->params.empty())
                            b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                        subscriptText = b;
                        expr->tokens.push_back(b);
                    }
                // Continuation: if next token is '.', handle dot-access on the subscript result
                // (e.g. arr[0].field or arr[0].method()). Resolve against the element type's class.
                if(!subscriptText.empty() && file.peekToken().is(token::period)){
                    file.getToken(); // consume '.'
                    string elemType = expr->resolvedType;
                    token member = file.getToken();
                    if(file.peekToken().is(token::parenOpen)){
                        // Method call: arr[0].method(args) — resolve via bindMethodCall
                        file.getToken(); // consume '('
                        ParsedArgList pal = parseCallArgList(func, body);
                        vector<string> namedArgNames = pal.namedArgNames;
                        vector<vector<interpolatedSegment>> interpSegs = pal.interpSegmentsPerArg;
                        functionDef* method = bindMethodCall(elemType, subscriptText, member.value,
                            pal.args, namedArgNames, interpSegs);
                        if(!expr->tokens.empty()) expr->tokens.pop_back();
                        if(method->isEmitter && !method->isPrePassStub){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                for(size_t pi = 0; pi < method->params.size() && pi < pal.args.size(); pi++)
                                    b = replaceWord(b, "$" + method->params[pi]->name, pal.args[pi]->text());
                                b = replaceWord(b, "$self", subscriptText);
                                b = replaceWord(b, "$val",  subscriptText);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                expr->tokens.push_back(b);
                            }
                        } else {
                            // Non-emitter method: emit as subscriptText.method(args)
                            string call = subscriptText + "." + member.value + "(";
                            for(size_t pi = 0; pi < pal.args.size(); pi++){
                                if(pi > 0) call += ", ";
                                call += pal.args[pi]->text();
                            }
                            call += ")";
                            expr->tokens.push_back(call);
                        }
                        if(!method->returnType.name.empty()) expr->resolvedType = method->returnType.name;
                    } else {
                        // Property access: arr[0].field
                        classDef* elemCls = getDispatchClass(elemType);
                        if(elemCls != nullptr){
                            function<string(classDef*)> findPropType = [&](classDef* c) -> string {
                                for(typeMember* m : c->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->name == member.value) return vd->type.name;
                                for(classDef* base : c->baseClasses){
                                    string t = findPropType(base);
                                    if(!t.empty()) return t;
                                }
                                return "";
                            };
                            string propType = findPropType(elemCls);
                            if(!propType.empty()) expr->resolvedType = propType;
                        }
                        // Replace the subscript text with subscriptText.member
                        if(!expr->tokens.empty()) expr->tokens.pop_back();
                        expr->tokens.push_back(subscriptText + "." + member.value);
                    }
                }
            }
            // ── name(args): function call in expression ──
            else if(next.is(token::parenOpen)){
                string callName = cur.value;
                // replace chaining: replaced() resolves to the predecessor's mangled name
                if(callName == "replaced" && currentFunc && !currentFunc->replacedTarget.empty()){
                    callName = currentFunc->replacedTarget;
                    currentFunc->replacedWasCalled = true;
                }
                // Self-call detection for object member methods.
                // Checks direct members of currentObject AND walks the class hierarchy
                // (currentClass->baseClasses) so inherited methods resolve as self.method().
                // Inherited methods that share a name with a global function are skipped —
                // the global path has proper arity matching and should win (e.g. print).
                bool isSelfCall = false;
                if(func != nullptr && callName.find('.') == string::npos){
                    // Recursive self-call: the function being parsed isn't yet in
                    // currentObject->members (registered post-body), so match by name
                    // against currentFunc when we're inside an object/class method.
                    if((currentObject != nullptr || currentClass != nullptr) &&
                       currentFunc != nullptr && currentFunc->name == callName)
                        isSelfCall = true;
                    if(!isSelfCall && currentObject != nullptr)
                        for(typeMember* m : currentObject->members)
                            if(auto* fd = dynamic_cast<functionDef*>(m))
                                if(fd->name == callName){ isSelfCall = true; break; }
                    if(!isSelfCall && currentClass != nullptr){
                        // Check if name also exists as a global function — if so, defer to global resolution
                        bool isGlobalFunc = false;
                        for(typeDef* g : languageService.globals)
                            if(auto* fd = dynamic_cast<functionDef*>(g))
                                if(fd->name == callName){ isGlobalFunc = true; break; }
                        if(!isGlobalFunc){
                            function<bool(classDef*)> searchHierarchy = [&](classDef* c) -> bool {
                                for(typeMember* m : c->members)
                                    if(auto* fd = dynamic_cast<functionDef*>(m))
                                        if(fd->name == callName) return true;
                                for(classDef* base : c->baseClasses)
                                    if(searchHierarchy(base)) return true;
                                return false;
                            };
                            isSelfCall = searchHierarchy(currentClass);
                        }
                    }
                }
                // Note: '(' was already consumed by getNext() above
                if(parseExprFunctionCall(expr, callName, isSelfCall, func, body)) {
                    // Emitter was inlined — advance past the function call and continue
                    cur = getNext();
                    continue;
                }
            }
            // ── name?.: optional chaining (expression-level) ──
            else if(parenDepth == 0 && next.is("?.")){
                // Optional chaining: obj?.member or obj?.method(args)
                // Lowers to: _bgl_tempN = obj; if(nullTest) _bgl_tempN = _bgl_tempN.member;
                string optTemp = format("_bgl_temp{0}", languageService.ternaryTempCount++);
                string lhsName = cur.value;
                string lhsType = !castType.empty() ? castType : resolveIdentifierType(lhsName, func, body);
                castType = "";
                if(lhsType.empty()) parsingError(format("Unknown variable '{0}' in optional chain", lhsName));
                // Look up operator?() on LHS type for the null test
                classDef* lhsCls = getDispatchClass(lhsType);
                if(lhsCls == nullptr) parsingError(format("Type '{0}' does not support optional chaining (not a class)", lhsType));
                functionDef* nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(lhsCls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
                if(nullTestFn == nullptr)
                    parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", lhsType));
                auto getNullTest = [&](functionDef* fn, const string& selfText) -> string {
                    auto* blk = dynamic_cast<i6Block*>(fn->body);
                    string b = processBglConditionals(blk->i6Body);
                    b = replaceWord(b, "$self", selfText);
                    b = replaceWord(b, "$val",  selfText);
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                    return b;
                };
                // Build the guarded chain
                string injText = optTemp + " = " + lhsName + ";";
                string currentType = lhsType;
                // Process chain steps: each is ?.member, ?.method(), or a trailing .member/.method()
                while(true){
                    string nullTest = getNullTest(nullTestFn, optTemp);
                    injText += " if (" + nullTest + ") {";
                    token member = file.getToken(eTokenType::identifier);
                    token afterMember = getNext();
                    if(afterMember.is(token::parenOpen)){
                        // ?.method(args) — find method, inline emitter or call
                        string methName = member.value;
                        classDef* cls = getDispatchClass(currentType);
                        vector<expression*> callArgs;
                        token firstArg = file.getToken();
                        while(firstArg.isNot(token::parenClose)){
                            expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                            callArgs.push_back(arg);
                            if(arg->terminator == token::parenClose) break;
                            firstArg = file.getToken();
                        }
                        MethodMatch mm = resolveMethod(currentType, optTemp, methName, callArgs);
                        functionDef* method = mm.method;
                        if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                        if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                b = replaceWord(b, "$self", optTemp);
                                b = replaceWord(b, "$val",  optTemp);
                                for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                    b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                injText += " " + optTemp + " = " + b + ";";
                            }
                        } else {
                            string call = optTemp + "." + methName + "(";
                            for(size_t i = 0; i < callArgs.size(); i++){
                                if(i > 0) call += ", ";
                                call += callArgs[i]->text();
                            }
                            call += ")";
                            injText += " " + optTemp + " = " + call + ";";
                        }
                        currentType = method->returnType.name;
                        afterMember = getNext();
                    } else {
                        // ?.property — simple property access
                        string propType = resolvePathType("_x_." + member.value, func, body);
                        // We can't resolve the runtime path, so check the class for a member
                        classDef* cls = getDispatchClass(currentType);
                        if(cls != nullptr)
                            for(typeMember* m : cls->members)
                                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                    if(vd->name == member.value){ propType = vd->type.name; break; }
                        injText += " " + optTemp + " = " + optTemp + "." + member.value + ";";
                        currentType = propType;
                    }
                    // Check for continuation: another ?. or regular .
                    if(afterMember.is("?.")){
                        // Another optional step: look up operator?() on the current type
                        classDef* nextCls = getDispatchClass(currentType);
                        nullTestFn = nullptr;
                        if(nextCls != nullptr)
                            nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(nextCls, [](typeMember* m){
                                auto* fn = dynamic_cast<functionDef*>(m);
                                return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                            }));
                        if(nullTestFn == nullptr)
                            parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", currentType));
                        continue; // next chain step
                    } else if(afterMember.is(token::period)){
                        // Regular dot after optional chain: ?.parent().name — non-guarded step
                        token nextMember = file.getToken(eTokenType::identifier);
                        token afterNext = getNext();
                        if(afterNext.is(token::parenOpen)){
                            // .method(args) — build as non-guarded call
                            string methName = nextMember.value;
                            classDef* cls = getDispatchClass(currentType);
                            vector<expression*> callArgs;
                            token firstArg = file.getToken();
                            while(firstArg.isNot(token::parenClose)){
                                expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                                callArgs.push_back(arg);
                                if(arg->terminator == token::parenClose) break;
                                firstArg = file.getToken();
                            }
                            MethodMatch mm2 = resolveMethod(currentType, optTemp, methName, callArgs);
                            functionDef* method = mm2.method;
                            if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                            if(method->isEmitter){
                                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                    string b = processBglConditionals(blk->i6Body);
                                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                    b = replaceWord(b, "$self", optTemp);
                                    b = replaceWord(b, "$val",  optTemp);
                                    for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                        b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                    injText += " " + optTemp + " = " + b + ";";
                                }
                            } else {
                                string call = optTemp + "." + methName + "(";
                                for(size_t i = 0; i < callArgs.size(); i++){ if(i > 0) call += ", "; call += callArgs[i]->text(); }
                                call += ")";
                                injText += " " + optTemp + " = " + call + ";";
                            }
                            currentType = method->returnType.name;
                            afterMember = getNext();
                            // Could chain further — check again
                            if(afterMember.is("?.") || afterMember.is(token::period)) { prefetched = afterMember; /* TODO: loop */ }
                            else prefetched = afterMember;
                        } else {
                            // .property
                            classDef* cls = getDispatchClass(currentType);
                            string propType;
                            if(cls != nullptr)
                                for(typeMember* m : cls->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->name == nextMember.value){ propType = vd->type.name; break; }
                            injText += " " + optTemp + " = " + optTemp + "." + nextMember.value + ";";
                            currentType = propType;
                            prefetched = afterNext;
                        }
                        break; // end of chain
                    } else {
                        prefetched = afterMember;
                        break; // end of chain
                    }
                }
                // Close all open if-braces
                {
                    size_t opens = 0;
                    for(size_t i = 0; i < injText.size(); i++)
                        if(injText[i] == '{') opens++;
                        else if(injText[i] == '}') opens--;
                    for(size_t i = 0; i < opens; i++) injText += " }";
                }
                i6RawNode* inj = new i6RawNode();
                inj->text = injText;
                pendingInjections.push_back(inj);
                expr->tokens.clear();
                expr->tokens.push_back(optTemp);
                if(!currentType.empty()) expr->resolvedType = currentType;
            }
            // ── name.member: dot-access (property, method call, enum, static) ──
            else if(next.is(token::period)){
                token member = file.getToken(eTokenType::identifier);
                // Read afterMember here so both self and non-self paths share it.
                // For the self property-access case, put it back via prefetched.
                token afterMember = getNext();
                if(cur.value == "self" && !afterMember.is(token::parenOpen)){
                    // Property access: self.property — look up type in current class/object
                    prefetched = afterMember;
                    string memberType;
                    if(currentClass != nullptr)
                        for(typeMember* m : currentClass->members)
                            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                if(vd->name == member.value){ memberType = vd->type.name; break; }
                    if(memberType.empty() && currentObject != nullptr)
                        for(typeMember* m : currentObject->members)
                            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                if(vd->name == member.value){ memberType = vd->type.name; break; }
                    if(expr->resolvedType.empty() && !memberType.empty()) expr->resolvedType = memberType;
                    // Inside a lambda: qualify 'self' through capture if needed
                    string selfText = "self";
                    if(lambdaOuterFunc != nullptr){
                        string qualified = qualifyIdentifier("self", func, body);
                        if(!qualified.empty()) selfText = qualified;
                    }
                    expr->tokens.push_back(selfText + "." + member.value);
                    // Capture host for $self substitution in rvalue property-class operators
                    // (mirrors the non-self property-access path).
                    expr->emitterSelf = selfText;
                } else {
                    // Non-self identifier, or self.method(args).
                    // afterMember was already read above; resolveIdentifierType("self",...)
                    // now returns the current object/class type, so self.method() works here too.
                    if(afterMember.is(token::parenOpen)){
                        // method call in expression context: obj.method(args)
                        string objName = cur.value;
                        string methName = member.value;
                        // Pass memberHint=methName so the resolver prefers a candidate whose type
                        // exposes the method, breaking name-collision ties (e.g. enum value vs
                        // class instance with the same case-insensitive name).
                        string objType = !castType.empty() ? castType : resolveIdentifierType(objName, func, body, methName);
                        castType = "";  // consume the cast
                        if(objType.empty()) parsingError(format("Unknown variable '{0}'", objName));
                        // Qualify objName for I6 emission: a #using-imported member like `glulx`
                        // needs to emit as `bgl.glulx` (the actual property path) so I6 resolves
                        // it correctly. Locals/globals qualify to themselves.
                        if(func != nullptr){
                            string qualified = qualifyIdentifier(objName, func, body, methName);
                            if(!qualified.empty()) objName = qualified;
                        }
                        // The receiver may be a classDef or an objectDef (each unclassed objectDef
                        // is its own type). Both have addressable methods; bindMethodCall handles
                        // either via its Step-2 objectDef-member fallback.
                        typeDef& objTd = languageService.getType(objType);
                        bool opaqueRecv = (dynamic_cast<classDef*>(&objTd) == nullptr && dynamic_cast<objectDef*>(&objTd) == nullptr);
                        // Generic specialization fallback: a templated receiver name like
                        // "array<int>" (from a parametric param) isn't a registered type, but
                        // its base ("array") is. Treat as non-opaque if the base resolves to
                        // a class — bindMethodCall threads the element type for substitution.
                        if(opaqueRecv){
                            auto lt = objType.find('<');
                            if(lt != string::npos && lt > 0){
                                typeDef& baseTd = languageService.getType(objType.substr(0, lt));
                                if(dynamic_cast<classDef*>(&baseTd) != nullptr) opaqueRecv = false;
                            }
                        }
                        if(opaqueRecv && !looseIdentifierMode)
                            parsingError(format("Type '{0}' has no methods", objType));

                        // parse argument list (handles named args via name: value syntax)
                        ParsedArgList pal = parseCallArgList(func, body);
                        vector<expression*>& callArgs = pal.args;
                        functionDef* method = nullptr;
                        if(opaqueRecv){
                            // Loose mode: receiver opaque (likely an I6 symbol). Skip method
                            // binding; emit verbatim as objName.methName(args), result type var.
                            string call = objName + "." + methName + "(";
                            for(size_t i = 0; i < callArgs.size(); i++){
                                if(i > 0) call += ", ";
                                call += callArgs[i]->text();
                            }
                            call += ")";
                            expr->tokens.push_back(call);
                            expr->resolvedType = "var";
                        } else {
                        // Element-type binding for generic receivers (array<T>, etc.):
                        // look up the receiver path's element type so the method-resolver
                        // can substitute T → concrete type.
                        string recvElemType = resolveArrayElementType(objName, func, body);
                        method = bindMethodCall(objType, objName, methName,
                                                               callArgs, pal.namedArgNames, pal.interpSegmentsPerArg,
                                                               recvElemType);

                        expr->resolvedType = method->returnType.name;

                        // ofClass guard: validate first argument is a real I6-backed class or object
                        if(objType == "bglworldtype" && methName == "ofclass" && !callArgs.empty()){
                            string clsArgName = callArgs[0]->text();
                            typeDef* clsType = nullptr;
                            for(typeDef* g : languageService.globals)
                                if(g->name == clsArgName){ clsType = g; break; }
                            bool valid = false;
                            if(clsType){
                                if(auto* od = dynamic_cast<objectDef*>(clsType))
                                    valid = true; // user object declarations always have I6 backing
                                else if(auto* cd = dynamic_cast<classDef*>(clsType))
                                    valid = !cd->isEmitterClass && !cd->isAlias;
                            }
                            if(!valid)
                                parsingError(format("'{0}' cannot be used with bglWorld.ofClass() — it is not an I6-backed class or object", callArgs[0]->text()));
                        }

                        // Compute $prop for array method calls in expression context
                        string exprPropValue = (objType == "array") ? "0" : "<$prop undefined>";

                        string callText;
                        if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r"); if(e != string::npos) b = b.substr(0, e+1);
                                b = replaceWord(b, "$self", objName);
                                b = replaceWord(b, "$val",  objName);
                                // $class — declared type of the receiver. Ignores multiple inheritance:
                                // resolves to the variable's static type, not the type that owns the
                                // inherited emitter. Useful for emitters that emit class-message I6
                                // (e.g. `$class.copy($self, $src)` from the bglAllocated mixin).
                                if(auto* recvCls = getDispatchClass(objType))
                                    b = replaceWord(b, "$class", recvCls->i6Name());
                                for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++){
                                    // Raw bodies preserve original case; substitute both forms.
                                    paramDef* p = method->params[i];
                                    const string& display = p->displayName.empty() ? p->name : p->displayName;
                                    b = replaceWord(b, "$" + display, callArgs[i]->text());
                                    if(display != p->name)
                                        b = replaceWord(b, "$" + p->name, callArgs[i]->text());
                                }
                                // $prop fallback — fills any remaining $prop with the array-style
                                // receiver-path value. Done after param sub so emitters with a
                                // `prop` parameter (e.g. `provides(property prop)`) win.
                                b = replaceWord(b, "$prop", exprPropValue);
                                callText = b;
                                expr->tokens.push_back(b);
                            }
                        } else {
                            // non-emitter: emit verbatim as obj.method(args)
                            string call = objName + "." + methName + "(";
                            for(size_t i = 0; i < callArgs.size(); i++){
                                if(i > 0) call += ", ";
                                call += callArgs[i]->text();
                            }
                            call += ")";
                            callText = call;
                            expr->tokens.push_back(call);
                        }

                        // First-call + subscript: `arr.method(args)[i]` dispatches operator[] on
                        // the method's return type. Same shape as the chained-call subscript path
                        // below; without this, the raw `[i]` falls through and I6 chokes on
                        // method-call-result indexing.
                        if(!callText.empty() && file.peekToken().is(token::bracketOpen)){
                            string chainResultType = expr->resolvedType;
                            classDef* chainCls = getDispatchClass(chainResultType);
                            string chainElem;
                            size_t lt = chainResultType.find('<');
                            if(lt != string::npos && !chainResultType.empty() && chainResultType.back() == '>')
                                chainElem = chainResultType.substr(lt + 1, chainResultType.size() - lt - 2);
                            if(chainElem.empty() && chainResultType == "bytearray") chainElem = "char";
                            functionDef* getMethod = nullptr;
                            if(chainCls != nullptr && !chainElem.empty())
                                getMethod = findArraySubscriptOp(chainCls, chainElem, /*isWrite=*/false);
                            if(getMethod != nullptr && getMethod->isEmitter){
                                if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)){
                                    file.getToken(token::bracketOpen);
                                    expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                                    string b = processBglConditionals(blk->i6Body);
                                    string receiver = "(" + callText + ")";
                                    b = replaceWord(b, "$self", receiver);
                                    b = replaceWord(b, "$val",  receiver);
                                    if(!getMethod->params.empty())
                                        b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                                    expr->tokens.pop_back();  // drop bare call text
                                    expr->tokens.push_back(b);
                                    string retTypeName = getMethod->returnType.name;
                                    if(retTypeName == "t" || retTypeName == "T") retTypeName = chainElem;
                                    expr->resolvedType = retTypeName;
                                }
                            }
                        }
                        } // end of typed-receiver else
                    } else if(afterMember.is(token::bracketOpen)) {
                        // Property array subscript in expression: obj.prop[i]
                        string objName = cur.value;
                        string propName = member.value;
                        string propType = resolvePathType(objName + "." + propName, func, body);
                        classDef* arrCls = getDispatchClass(propType);
                        expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                        string elemType = resolveArrayElementTypeDotted(objName, propName, func, body);
                        if(elemType.empty() && propType == "bytearray") elemType = "char";
                        functionDef* getMethod = nullptr;
                        if(arrCls != nullptr && !elemType.empty())
                            getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
                        if(getMethod == nullptr) {
                            if(elemType.empty())
                                parsingError(format("Subscript on '{0}.{1}': property is not a declared array", objName, propName));
                            parsingError(format("No operator[] returning '{0}' on type '{1}'", typeDisplayName(elemType), typeDisplayName(propType)));
                        }
                        if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                        if(getMethod->isEmitter)
                            if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                                string b = processBglConditionals(blk->i6Body);
                                b = replaceWord(b, "$self", objName);
                                b = replaceWord(b, "$val",  objName);
                                b = replaceWord(b, "$prop", propName);
                                if(!getMethod->params.empty())
                                    b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                                expr->tokens.push_back(b);
                            }
                    } else {
                        prefetched = afterMember;
                        // Distinguish enum-qualified access (EnumType.value → _EnumType_value),
                        // static member access (ClassName.staticMember → _bgl_ClassName_memberName),
                        // from object/variable property access (obj.prop → obj.prop)
                        bool isEnum = dynamic_cast<enumDef*>(&languageService.getType(cur.value)) != nullptr;
                        classDef* maybeCls = getDispatchClass(cur.value);
                        bool isStaticAccess = false;
                        if(maybeCls != nullptr){
                            for(typeMember* m : maybeCls->members)
                                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                    if(vd->isStatic && vd->name == member.value){
                                        if(expr->resolvedType.empty()) expr->resolvedType = vd->type.name;
                                        expr->tokens.push_back("_bgl_" + maybeCls->name + "_" + member.value);
                                        isStaticAccess = true; break;
                                    }
                        }
                        // Value emitter member: expand body inline. Members can live on either
                        // a class (Cls.member) or an object instance (obj.member, e.g. bgl.wordsize),
                        // so we try classDef members first, then objectDef members.
                        bool isValueEmitterAccess = false;
                        auto tryInlineValueEmitter = [&](vector<typeMember*>& members) -> bool {
                            for(typeMember* m : members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == member.value && fd->isValueEmitter && fd->isEmitter){
                                        if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                                            string b = processBglConditionals(blk->i6Body);
                                            b = i6Emitter::replaceWord(b, "$self", cur.value);
                                            b = i6Emitter::replaceWord(b, "$val",  cur.value);
                                            size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                            size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                            expr->tokens.push_back(b);
                                            if(expr->resolvedType.empty()) expr->resolvedType = fd->returnType.name;
                                            return true;
                                        }
                                        return false;
                                    }
                            return false;
                        };
                        if(!isStaticAccess && maybeCls != nullptr)
                            isValueEmitterAccess = tryInlineValueEmitter(maybeCls->members);
                        if(!isStaticAccess && !isValueEmitterAccess){
                            objectDef* maybeObj = dynamic_cast<objectDef*>(&languageService.getType(cur.value));
                            if(maybeObj != nullptr)
                                isValueEmitterAccess = tryInlineValueEmitter(maybeObj->members);
                        }
                        // Alias member on emitter class: transparent resolution — continue chaining as the alias type
                        bool isAliasMember = false;
                        if(!isStaticAccess && !isValueEmitterAccess && maybeCls != nullptr && maybeCls->isEmitterClass){
                            for(typeMember* m : maybeCls->members)
                                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                    if(vd->name == member.value){
                                        // Alias: set cur to the alias type name, re-enter the loop
                                        cur.value = vd->type.name;
                                        cur.tokenType = eTokenType::identifier;  // ensure it enters identifier branch
                                        expr->resolvedType = "";  // reset so the alias target resolves fresh
                                        prefetched = afterMember;
                                        isAliasMember = true;
                                        break;
                                    }
                        }
                        // Alias member on object instance: same redirect as the class case
                        if(!isStaticAccess && !isValueEmitterAccess && !isAliasMember){
                            objectDef* instObj = dynamic_cast<objectDef*>(&languageService.getType(cur.value));
                            if(instObj != nullptr){
                                for(typeMember* m : instObj->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->isExternal && vd->name == member.value &&
                                           getDispatchClass(vd->type.name) != nullptr){
                                            cur.value = vd->type.name;
                                            cur.tokenType = eTokenType::identifier;
                                            expr->resolvedType = "";
                                            prefetched = afterMember;
                                            isAliasMember = true;
                                            break;
                                        }
                            }
                        }
                        // Auto member on object instance pointing to another object: redirect cur
                        // so chained access (e.g. bgl.glulx.method) continues walking. Only applies
                        // to namespace-style auto members — those whose initializer names a global
                        // object, OR whose declared type is an emitter class. Plain value-typed
                        // properties (int x; string s;) fall through to normal property handling.
                        if(!isStaticAccess && !isValueEmitterAccess && !isAliasMember){
                            objectDef* instObj = dynamic_cast<objectDef*>(&languageService.getType(cur.value));
                            if(instObj != nullptr){
                                for(typeMember* m : instObj->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->name == member.value){
                                            string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                                            objectDef* target = nullptr;
                                            if(!initName.empty())
                                                for(typeDef* g : languageService.globals)
                                                    if(auto* od = dynamic_cast<objectDef*>(g))
                                                        if(od->name == initName){ target = od; break; }
                                            if(target){
                                                cur.value = target->name;
                                                cur.tokenType = eTokenType::identifier;
                                                expr->resolvedType = "";
                                                prefetched = afterMember;
                                                isAliasMember = true;
                                            } else {
                                                // Auto pointing to an emitter class (e.g. emitter auto asm = bglOpCodes)
                                                auto* cd = getDispatchClass(vd->type.name);
                                                if(cd && cd->isEmitterClass){
                                                    cur.value = vd->type.name;
                                                    cur.tokenType = eTokenType::identifier;
                                                    expr->resolvedType = "";
                                                    prefetched = afterMember;
                                                    isAliasMember = true;
                                                }
                                            }
                                            break;
                                        }
                            }
                        }
                        if(isEnum){
                            if(expr->resolvedType.empty()) expr->resolvedType = cur.value;
                            // Inline the integer value (extern enums emit the name — I6 keyword).
                            auto* ed = dynamic_cast<enumDef*>(&languageService.getType(cur.value));
                            if(ed && !ed->isExternal){
                                int v = 0; bool found = false;
                                for(enumValueDef* ev : ed->namedValues)
                                    if(ev->name == member.value){ v = ev->value; found = true; break; }
                                if(found) expr->tokens.push_back(to_string(v));
                                else expr->tokens.push_back("_" + cur.value + "_" + member.value);
                            } else {
                                expr->tokens.push_back(member.value);
                            }
                        } else if(isAliasMember) {
                            continue;  // re-enter loop with cur set to alias type
                        } else if(!isStaticAccess && !isValueEmitterAccess) {
                            // Object property access: emit as obj.prop
                            string propType = resolvePathType(cur.value + "." + member.value, func, body);
                            if(expr->resolvedType.empty() && !propType.empty()) expr->resolvedType = propType;
                            expr->tokens.push_back(cur.value + "." + member.value);
                            // Capture host text for $self substitution in rvalue property-class
                            // operator emitters (parent($self), give $self $attr, etc.).
                            expr->emitterSelf = cur.value;
                        }
                    }
                }
            }
            // ── name?: postfix query operator ──
            // Only treat ? as postfix if the identifier's type has operator?() AND ? is not
            // a terminator for this expression. When ? is a terminator (e.g. from precedence
            // sub-parse), it's the start of a ternary, not a postfix query.
            else if(next.is("?")){
                // Check if ? is a terminator — if so, it's ternary, not postfix
                bool qIsTerminator = false;
                if(parenDepth <= startParenDepth)
                    for(const string& term : terminators)
                        if(term == "?"){ qIsTerminator = true; break; }
                string varName = cur.value;
                string varType = resolveIdentifierType(varName, func, body);
                classDef* cls = !varType.empty() ? dynamic_cast<classDef*>(&languageService.getType(varType)) : nullptr;
                functionDef* queryFn = nullptr;
                if(cls != nullptr && !qIsTerminator)
                    queryFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                    }));
                if(queryFn != nullptr){
                    // Type supports postfix ? — inline the emitter
                    string qualified = func != nullptr ? qualifyIdentifier(varName, func, body) : varName;
                    if(qualified.empty()) parsingError(format("Undeclared identifier '{0}'", varName));
                    auto* blk = dynamic_cast<i6Block*>(queryFn->body);
                    string b = processBglConditionals(blk->i6Body);
                    b = replaceWord(b, "$self", qualified);
                    b = replaceWord(b, "$val",  qualified);
                    { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                      size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1); }
                    expr->tokens.push_back(b);
                    if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
                } else {
                    // No operator?() — put ? back for ternary handling
                    prefetched = next;
                    string qualified = func != nullptr ? qualifyIdentifier(cur.value, func, body) : cur.value;
                    if(!qualified.empty()){
                        if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body);
                        expr->tokens.push_back(qualified);
                    } else if(func != nullptr){
                        parsingError(format("Undeclared identifier '{0}'", cur.value));
                    } else {
                        expr->tokens.push_back(cur.value);
                    }
                }
            }
            else {
                prefetched = next;
                // Resolve identifier: variables/params/globals take priority over verb action constants.
                // Only emit ##VerbName if the identifier doesn't resolve as a declared variable.
                // qualifyIdentifier handles: params/locals → name, object members → self.name,
                // globals → name, action constants (verbDefs) → ##VerbName.
                // It works correctly with func==nullptr (skips param/local tiers gracefully).
                // When the next token is '.', peek the member name and pass as a hint so the
                // resolver picks a candidate whose type actually exposes that member — disambiguates
                // collisions (e.g. enum value vs class instance with the same name).
                string memberHint;
                if(next.is(token::period)){
                    token after = file.peekToken(1);
                    if(after.is(eTokenType::identifier) || after.is(eTokenType::dataType))
                        memberHint = after.value;
                }
                string qualified = qualifyIdentifier(cur.value, func, body, memberHint);
                if(!qualified.empty()){
                    if(!castType.empty()){ expr->resolvedType = castType; castType = ""; }
                    else if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body, memberHint);
                    expr->tokens.push_back(qualified);
                } else if(func != nullptr){
                    parsingError(format("Undeclared identifier '{0}'", cur.value));
                } else {
                    expr->tokens.push_back(cur.value); // global-context passthrough for unknown identifiers
                }
            }
        }
        // ─── NULL COALESCING: ?? ──────────────────────────────────────────
        else if(cur.is(eTokenType::oper) && cur.value == "??" && parenDepth == 0){
            parseExprNullCoalescing(expr, terminators, func, body);
            break;
        }
        // ─── BINARY OPERATOR: emitter dispatch via applyBinaryOperator() ─
        else if(cur.is(eTokenType::oper)){
            if(!expr->resolvedType.empty()){
                classDef* cls = dynamic_cast<classDef*>(&languageService.getType(expr->resolvedType));
                // Set expected type for the RHS so name resolution can disambiguate. Applies to
                // both classDef and enumDef LHS — most binary operators take same-type RHS.
                // The parseExpression-level RAII guard restores on exit; no manual restore here.
                currentExpectedType = expr->resolvedType;
                if(cls != nullptr){
                    string opName = cur.value;
                    // Peek at RHS
                    applyBinaryOperator(expr, opName, cls, terminators, parenDepth, getNext, prefetched, func, body);
                } else if(auto* lhsEnum = dynamic_cast<enumDef*>(&languageService.getType(expr->resolvedType));
                          lhsEnum && lhsEnum->isBnum && (cur.value == "|" || cur.value == "&" || cur.value == "^")){
                    // bnum bitwise composition: RHS must be a bnum sharing a common base, the
                    // same bnum, or int. Result type is the shared base (or LHS if same/child).
                    string opName = cur.value;
                    token rhs = getNext();
                    string rhsType, rhsText;
                    if(rhs.is(eTokenType::integer)){ rhsType = "intliteral"; rhsText = rhs.value; }
                    else if(rhs.is(eTokenType::name)){
                        string nsEmission, nsEnumType;
                        if(tryConsumeNamespacedEnumValue(rhs, nsEmission, nsEnumType)){
                            rhsType = nsEnumType;
                            rhsText = nsEmission;
                        } else {
                            rhsType = resolveIdentifierType(rhs.value, func, body);
                            rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value, func, body) : rhs.value;
                            if(rhsText.empty()) rhsText = rhs.value;
                        }
                    }
                    else if(rhs.is(token::parenOpen)){
                        expression* rhsExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
                        rhsText = "(" + rhsExpr->text() + ")";
                        rhsType = rhsExpr->resolvedType;
                    }
                    else parsingError(format("Unexpected token '{0}' after bnum '{1}'", rhs.value, opName));

                    // Validate RHS and compute result type
                    enumDef* rhsEnum = dynamic_cast<enumDef*>(&languageService.getType(rhsType));
                    string resultType = expr->resolvedType;
                    auto ancestorOrSelf = [](enumDef* a, enumDef* b) -> enumDef* {
                        // Return a common ancestor (including equality) of two bnums, else nullptr.
                        for(enumDef* ai = a; ai; ai = ai->baseBnum)
                            for(enumDef* bi = b; bi; bi = bi->baseBnum)
                                if(ai == bi) return ai;
                        return nullptr;
                    };
                    if(rhsType == "int" || rhsType == "intliteral"){
                        resultType = "int";
                    } else if(rhsEnum && rhsEnum->isBnum){
                        enumDef* common = ancestorOrSelf(lhsEnum, rhsEnum);
                        if(!common)
                            parsingError(format("bnum '{0}' and '{1}' have no shared base; they cannot be combined with '{2}'",
                                                lhsEnum->dName(), rhsEnum->dName(), opName));
                        resultType = common->name;
                    } else {
                        parsingError(format("Operator '{0}' on bnum '{1}' requires a bnum or int right-hand side (got '{2}')",
                                            opName, lhsEnum->dName(), typeDisplayName(rhsType)));
                    }
                    expr->tokens.push_back(opName);
                    expr->tokens.push_back(rhsText);
                    expr->resolvedType = resultType;
                } else {
                    expr->tokens.push_back(cur.value);
                }
            } else if(cur.value == "!"){
                parseExprPrefixNot(expr, getNext(), prefetched, func, body);
            } else {
                expr->tokens.push_back(cur.value);
            }
        }
        // ─── DICTIONARY WORD LITERAL ─────────────────────────────────────
        else if(cur.is(eTokenType::dictionaryWord)){
            if(expr->resolvedType.empty()) expr->resolvedType = "dictionarywordliteral";
            // Replace apostrophes with ^ for I6 dictionary word encoding,
            // but preserve ' when it follows @ (I6 accent notation like @'e)
            string w;
            for(size_t ci = 0; ci < cur.value.size(); ci++){
                char ch = cur.value[ci];
                if(ch == '\'' && (ci == 0 || cur.value[ci-1] != '@'))
                    w += '^';
                else
                    w += ch;
            }
            string i6form = cur.isPlural ? ("'" + w + "/p'")
                                         : (w.size() == 1) ? ("'" + w + "//'") : ("'" + w + "'");
            expr->tokens.push_back(i6form);
        }
        // ─── CHAR LITERAL ─────────────────────────────────────────────────
        else if(cur.is(eTokenType::charLiteral)){
            if(expr->resolvedType.empty()) expr->resolvedType = "charliteral";
            // Numeric ZSCII (\NNN) and diacritical (@^a etc.) are standalone I6 tokens; others need '...'
            bool isBareToken = (!cur.value.empty() && all_of(cur.value.begin(), cur.value.end(), ::isdigit))
                            || (cur.value.rfind("@", 0) == 0);
            if(isBareToken) expr->tokens.push_back(cur.value);
            else expr->tokens.push_back("'" + cur.value + "'");
        }
        // ─── DOT CHAINING on resolved expression: .method() or .property ─
        else if(cur.value == "." && !expr->tokens.empty() && !expr->resolvedType.empty()){
            // Chained method call on result of prior expression: <expr>.method(args)
            token member = file.getToken(eTokenType::identifier);
            token afterMember = getNext();
            if(!afterMember.is(token::parenOpen)){
                // Not a method call (e.g. struct member) — emit '.' and re-process member
                prefetched = afterMember;
                expr->tokens.push_back(".");
                cur = member;
                continue;
            }

            string selfText;
            for(const auto& t : expr->tokens) selfText += t;

            string chainTypeName = expr->resolvedType;
            // Method receiver may be either a classDef or an objectDef (unclassed objectDefs
            // each have their own type identity). Templated names like `array<int>` aren't
            // registered as their own typeDef — use getDispatchClass to strip the <...> suffix
            // and reach the generic base when the raw lookup misses, mirroring the receiver
            // resolution used elsewhere (resolveMethod, operator dispatch).
            typeDef& chainTd = languageService.getType(chainTypeName);
            if(dynamic_cast<classDef*>(&chainTd) == nullptr && dynamic_cast<objectDef*>(&chainTd) == nullptr
               && getDispatchClass(chainTypeName) == nullptr)
                parsingError(format("Type '{0}' has no methods", chainTypeName));

            string methName = member.value;
            vector<expression*> callArgs;
            token firstArg = file.getToken();
            while(firstArg.isNot(token::parenClose)){
                expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                callArgs.push_back(arg);
                if(arg->terminator == token::parenClose) break;
                firstArg = file.getToken();
            }

            vector<string> emptyNamed;
            vector<vector<interpolatedSegment>> emptyInterp;
            functionDef* method = bindMethodCall(chainTypeName, selfText, methName,
                                                   callArgs, emptyNamed, emptyInterp);

            expr->tokens.clear();
            expr->resolvedType = method->returnType.name;

            string callText;
            if(method->isEmitter){
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    string b = processBglConditionals(blk->i6Body);
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r"); if(e != string::npos) b = b.substr(0, e+1);
                    b = replaceWord(b, "$self", selfText);
                    b = replaceWord(b, "$val",  selfText);
                    for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                        b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                    // Any leftover $prop is unresolved (no matching parameter — see array
                    // emitters that consume property names from outside the call).
                    b = replaceWord(b, "$prop", "<$prop undefined>");
                    callText = b;
                    expr->tokens.push_back(b);
                }
            } else {
                string call = selfText + "." + methName + "(";
                for(size_t i = 0; i < callArgs.size(); i++){
                    if(i > 0) call += ", ";
                    call += callArgs[i]->text();
                }
                call += ")";
                callText = call;
                expr->tokens.push_back(call);
            }

            // Chain + subscript: `arr.method(args)[i]` dispatches operator[] on the
            // method's return type, with the emitted call text as the subscript receiver.
            // Without this, the raw `[i]` would emit verbatim and I6 chokes on
            // method-call-result indexing (it only allows `name-->i` form).
            if(!callText.empty() && file.peekToken().is(token::bracketOpen)){
                string chainResultType = expr->resolvedType;
                classDef* chainCls = getDispatchClass(chainResultType);
                string chainElem;
                size_t lt = chainResultType.find('<');
                if(lt != string::npos && !chainResultType.empty() && chainResultType.back() == '>')
                    chainElem = chainResultType.substr(lt + 1, chainResultType.size() - lt - 2);
                if(chainElem.empty() && chainResultType == "bytearray") chainElem = "char";
                functionDef* getMethod = nullptr;
                if(chainCls != nullptr && !chainElem.empty())
                    getMethod = findArraySubscriptOp(chainCls, chainElem, /*isWrite=*/false);
                if(getMethod != nullptr && getMethod->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)){
                        file.getToken(token::bracketOpen);
                        expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                        string b = processBglConditionals(blk->i6Body);
                        string receiver = "(" + callText + ")";
                        b = replaceWord(b, "$self", receiver);
                        b = replaceWord(b, "$val",  receiver);
                        if(!getMethod->params.empty())
                            b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                        expr->tokens.pop_back();  // drop bare call text
                        expr->tokens.push_back(b);
                        // Substitute T → chainElem in the subscript return type, so chained
                        // expressions downstream see a concrete element type. operator[] on
                        // a generic class returns T; without this, downstream dispatch
                        // (e.g. print(arr.filter(p)[0])) would see the literal 'T'.
                        string retTypeName = getMethod->returnType.name;
                        if(retTypeName == "t" || retTypeName == "T") retTypeName = chainElem;
                        expr->resolvedType = retTypeName;
                    }
                }
            }
        }
        // ─── TERNARY OPERATOR: condition ? trueExpr : falseExpr ──────────
        // Uses continuation: true branch is sub-parsed (':' is unambiguous), then the false
        // branch is collected by continuing this loop — preserving paren tracking. Assembly
        // happens after the loop exits.
        else if(cur.value == "?") {
            PendingTernary pt;
            pt.tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
            // Strip leading structural '(' tokens from the condition. When the ternary is
            // inside a paren group like `(cond ? a : b)`, the '(' was opened before the condition
            // and its matching ')' comes after the false branch. These parens are structural —
            // they belong to the outer expression, not the ternary condition.
            vector<string> prefix;
            if(parenDepth > startParenDepth){
                // Inside a paren group: strip leading '(' up to (parenDepth - startParenDepth)
                int toStrip = parenDepth - startParenDepth;
                while(toStrip > 0 && !expr->tokens.empty() && expr->tokens.front() == "("){
                    prefix.push_back(expr->tokens.front());
                    expr->tokens.erase(expr->tokens.begin());
                    toStrip--;
                }
            } else {
                // At top level: strip balanced pairs (e.g. `(x >= 0) ? a : b`)
                while(expr->tokens.size() > 1 && expr->tokens.front() == "(" && expr->tokens.back() == ")"){
                    prefix.push_back(expr->tokens.front());
                    expr->tokens.erase(expr->tokens.begin());
                    expr->tokens.pop_back();
                }
            }
            pt.condText = expr->text();
            expression* trueExpr = parseExpression(file.getToken(), {":"}, func, body);
            pt.trueText = trueExpr->text();
            pt.trueType = trueExpr->resolvedType;
            pt.parenDepthAtQuestion = parenDepth;
            // Only preserve prefix parens if we're inside a paren group (parenDepth > startParenDepth).
            // At top level, the parens were just condition grouping (e.g. `(x >= 0) ? a : b`)
            // and shouldn't wrap the temp name.
            if(parenDepth > startParenDepth) pt.prefixParens = prefix;
            pendingTernaries.push_back(pt);
            // Clear expression and continue loop — false branch tokens collect clean (no prefix)
            expr->tokens.clear();
            expr->resolvedType = "";
            cur = getNext();
            continue;
        }
        // ─── DIRECTIVE: #beguilerSettings.property references ─────────────
        else if(cur.is(eTokenType::directive)){
            // ##VerbName is not valid in Beguile expressions; the ## prefix is emitted automatically.
            if(cur.value.rfind("##", 0) == 0){
                string verbName = cur.value.substr(2);
                parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly — the '##' prefix is emitted automatically by the verb type's operator ==.", verbName));
            }
            // #beguilerSettings.propName — resolve to a compile-time literal
            if(cur.value == "#beguilersettings"){
                file.getToken(token::period);
                token prop = file.getToken(eTokenType::identifier);
                string key = prop.value; // already lowercase
                string strVal;
                bool   isInt = false;
                int    intVal = 0;
                if     (key == "title")          strVal = beguilerSettings.title;
                else if(key == "author")         strVal = beguilerSettings.author;
                else if(key == "headline")       strVal = beguilerSettings.headline;
                else if(key == "genre")          strVal = beguilerSettings.genre;
                else if(key == "description")    strVal = beguilerSettings.description;
                else if(key == "language")       strVal = beguilerSettings.language;
                else if(key == "series")         strVal = beguilerSettings.series;
                else if(key == "seriesnumber") { isInt = true; intVal = beguilerSettings.seriesNumber; }
                else if(key == "firstpublished") strVal = beguilerSettings.firstPublished;
                else if(key == "forgiveness")    strVal = beguilerSettings.forgiveness;
                else if(key == "ifid")           strVal = beguilerSettings.ifid;
                else if(key == "target")        strVal = beguilerSettings.target;
                else if(key == "outputpath")    strVal = beguilerSettings.outputPath;
                else if(key == "blorbassetpath")strVal = beguilerSettings.blorbAssetPath;
                else if(key == "informname")    strVal = beguilerSettings.informName;
                else if(key == "release")     { isInt = true; intVal = beguilerSettings.release; }
                else if(key == "serial")       strVal = beguilerSettings.serial;
                else if(key == "framepoolsize" || key == "linqscratchsize"){
                    isInt = true;
                    int v = (key == "framepoolsize") ? beguilerSettings.framePoolSize : beguilerSettings.linqScratchSize;
                    if(v < 0){
                        // applySchemaDefaults() runs after parsing, so during source parse the
                        // runtime value may still be -1 (unset). Fall back to the schema-declared
                        // default so #beguilerSettings.X at parse time resolves consistently with
                        // what the final ICL emission will use.
                        classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilerSettingstype"));
                        if(schema){
                            for(typeMember* m : schema->members){
                                auto* vd = dynamic_cast<variableDeclaration*>(m);
                                if(vd && vd->name == key && vd->declaredExpressionValue){
                                    v = stoi(vd->declaredExpressionValue->text());
                                    break;
                                }
                            }
                        }
                        if(v < 0) v = 0;
                    }
                    intVal = v;
                }
                else parsingError(format("#beguilerSettings.{0}: unknown or unsupported property", prop.value));

                if(isInt){
                    expr->tokens.push_back(to_string(intVal));
                    if(expr->resolvedType.empty()) expr->resolvedType = "intliteral";
                } else {
                    expr->tokens.push_back("\"" + strVal + "\"");
                    if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
                }
                cur = getNext();
                continue;
            }
            parsingError(format("Directive '{0}' is not valid in an expression.", cur.value));
        }
        else {
            expr->tokens.push_back(cur.value);
        }

        cur = getNext();
    }

    // Assemble any remaining pending ternaries (e.g. top-level ternary without parens)
    while(!pendingTernaries.empty()) assembleTernary();
    return expr;
}

bool bglParser::processStatement(token tok, abstractObject& contextObj){
    sourceLocation stmtLoc = tok.src.line > 0 ? tok.src : file.currentLocation();
    currentStatementSrc = stmtLoc;
    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    string stmtCastType; // set when statement begins with (TypeName) cast prefix

    // Cast prefix: (TypeName)obj.method(args); — overrides type used for method dispatch
    if(tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
        stmtCastType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
        tok = file.getToken();  // the actual object identifier
    }

    // Static member access: ClassName.member — reclassify the class as an identifier so
    // dot-access works. Object instances are already identifiers after the type/instance split.
    if(tok.is(eTokenType::dataType)){
        if(file.peekToken(1).is(token::period) || file.peekToken(1).is("?."))
            tok.tokenType = eTokenType::identifier;
    }

    // Prefix ++ / --
    if(tok.is(eTokenType::oper) && (tok.value == "++" || tok.value == "--")){
        token varName = file.getToken(eTokenType::identifier);
        string lhs = func != nullptr ? qualifyIdentifier(varName.value, func, body) : varName.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", varName.value));
        if(isConstVariable(varName.value, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", varName.value));
        file.getToken(token::endStatement);
        // Try emitter lookup for "prefix++" / "prefix--" on the LHS type, falling back to the
        // plain "++" / "--" emitter if no prefix-specific override is defined.
        string lhsTypeName = resolveIdentifierType(varName.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
        bool emitterFound = false;
        string prefixOpName = "prefix" + tok.value;  // e.g. "prefix++"
        auto tryEmitter = [&](const string& opName) -> bool {
            if(!lhsClass) return false;
            typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                return opFunc && opFunc->name==opName && opFunc->isEmitter
                       && opFunc->params.empty() && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
            });
            if(!m) return false;
            auto* opFunc = dynamic_cast<functionDef*>(m);
            auto* blk = dynamic_cast<i6Block*>(opFunc->body);
            string b = processBglConditionals(blk->i6Body);
            b = replaceWord(b, "$self", lhs);
            b = replaceWord(b, "$val",  lhs);
            i6RawNode& node = *(new i6RawNode());
            node.text = b + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
            return true;
        };
        if(tryEmitter(prefixOpName) || tryEmitter(tok.value)) emitterFound = true;
        if(!emitterFound){
            if(!lhsTypeName.empty() && lhsTypeName != "var")
                parsingError(format("No operator '{0}' defined on type '{1}'", tok.value, typeDisplayName(lhsTypeName)));
            i6RawNode& node = *(new i6RawNode());
            node.text = tok.value + lhs + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
        }
        return false;
    }

    // Determine if this token is a literal with a registered class (e.g. intLiteral, stringLiteral).
    // If so, it may head a method call: "hello".print() or 42.someMethod().
    // literalSelfText holds the I6 text to substitute for $self in emitter bodies.
    string literalTypeName, literalSelfText;
    {
        auto resolveLiteralType = [&]() -> pair<string,string> {
            if(tok.is(eTokenType::integer))       return {"intliteral",    tok.value};
            if(tok.isString())                    return {"stringliteral", tok.value};
            if(tok.is(eTokenType::charLiteral))   { bool bare = (!tok.value.empty() && all_of(tok.value.begin(),tok.value.end(),::isdigit)) || tok.value.rfind("@",0)==0; return {"charliteral", bare ? tok.value : "'" + tok.value + "'"}; }
            return {"",""};
        };
        auto [tn, st] = resolveLiteralType();
        // Only treat as a typed literal if a classDef is actually registered for it.
        if(!tn.empty() && dynamic_cast<classDef*>(&languageService.getType(tn)) != nullptr){
            literalTypeName = tn;
            literalSelfText = st;
        }
    }
    bool tokIsLiteral = !literalTypeName.empty();
    if(!tok.is(eTokenType::identifier) && !tokIsLiteral)
        return parsingError(format("Unrecognized statement starting with token '{0}'", (string) tok));

    //make sure the identifier is complete, including any member access paths (chain all dots and ?.)
    token symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    int optionalChainDepth = 0; // number of ?. guards opened
    while(symbol.is(token::period) || symbol.is("?.")) {
        if(symbol.is("?.")){
            // Optional chaining at statement level: emit if(nullTest){ as pre-injection, } as post-injection
            string pathSoFar = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
            if(pathSoFar.empty()) pathSoFar = tok.value;
            string pathType = resolveIdentifierType(tok.value, func, body);
            if(pathType.empty()) pathType = resolvePathType(tok.value, func, body);
            classDef* cls = !pathType.empty() ? dynamic_cast<classDef*>(&languageService.getType(pathType)) : nullptr;
            functionDef* nullTestFn = nullptr;
            if(cls != nullptr)
                nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
            if(nullTestFn == nullptr)
                parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", pathType));
            auto* blk = dynamic_cast<i6Block*>(nullTestFn->body);
            string guard = processBglConditionals(blk->i6Body);
            guard = i6Emitter::replaceWord(guard, "$self", pathSoFar);
            guard = i6Emitter::replaceWord(guard, "$val",  pathSoFar);
            { size_t s=guard.find_first_not_of(" \t\n\r"); if(s!=string::npos) guard=guard.substr(s);
              size_t e=guard.find_last_not_of(" \t\n\r;"); if(e!=string::npos) guard=guard.substr(0,e+1); }
            i6RawNode* openNode = new i6RawNode();
            openNode->text = "if (" + guard + ") {";
            pendingInjections.push_back(openNode);
            optionalChainDepth++;
        }
        token nextPart = file.getToken(eTokenType::identifier);
        tok.value += "." + nextPart.value;
        // Keep originalValue in sync so loose-mode displayFunctionName preserves case
        // across the full dotted path (e.g. "RedSpell.cast", not just "RedSpell").
        if(tok.originalValue.empty()) tok.originalValue = tok.value;
        else tok.originalValue += "." + (nextPart.originalValue.empty() ? nextPart.value : nextPart.originalValue);
        symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    }
    // Generate matching close braces as post-injections
    for(int i = 0; i < optionalChainDepth; i++){
        i6RawNode* closeNode = new i6RawNode();
        closeNode->text = "}";
        postInjections.push_back(closeNode);
    }

    // A literal with no chained method call is meaningless as a statement.
    if(tokIsLiteral && tok.value.find('.') == string::npos)
        return parsingError(format("Literal value cannot appear as a statement without a method call"));



    //----------------------------------------------------------------------
    //We've encountered an identifier, which could be a variable assignment,
    //  subscript assignment, function call, or value emitter statement.

    // Value emitter as statement: identifier; or dot-path; where it resolves to a value emitter
    if(symbol.is(token::endStatement)){
        string ident = tok.value;
        // Use qualifyIdentifier to resolve dot-paths, aliases, and #using imports
        string qualified = qualifyIdentifier(ident, func, body);
        if(!qualified.empty() && qualified != ident){
            // qualifyIdentifier expanded a value emitter — emit as raw I6
            i6RawNode& node = *(new i6RawNode());
            node.text = qualified + ";";
            node.src = stmtLoc;
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
            pendingInjections.clear();
            if(body != nullptr) body->statements.push_back(&node);
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
            return false;
        }
        // Also check simple global/import value emitters (qualified == ident means no expansion)
        functionDef* veFunc = nullptr;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->name == ident && fd->isValueEmitter && fd->isEmitter){ veFunc = fd; break; }
        if(!veFunc)
            for(classDef* imp : usingImports)
                for(typeMember* m : imp->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == ident && fd->isValueEmitter && fd->isEmitter){ veFunc = fd; break; }
        if(!veFunc)
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == ident && fd->isValueEmitter && fd->isEmitter){ veFunc = fd; break; }
        if(veFunc){
            if(auto* blk = dynamic_cast<i6Block*>(veFunc->body)){
                string bodyText = processBglConditionals(blk->i6Body);
                size_t s = bodyText.find_first_not_of(" \t\n\r"); if(s != string::npos) bodyText = bodyText.substr(s);
                size_t e = bodyText.find_last_not_of(" \t\n\r;"); if(e != string::npos) bodyText = bodyText.substr(0, e+1);
                i6RawNode& node = *(new i6RawNode());
                node.text = bodyText + ";";
                node.src = stmtLoc;
                for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
                pendingInjections.clear();
                if(body != nullptr) body->statements.push_back(&node);
                for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
                postInjections.clear();
            }
            return false;
        }
    }

    // Subscript: name[i] = v  (assignment) or  name[i].member (dot-chain on result)
    if(symbol.is(token::bracketOpen)) {
        string arrPath = (string)tok;  // e.g. "scores" or "player.inventory"
        expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);

        // Peek after ']': if '.', this is a dot-chain on the subscript result (e.g. arr[0].method()).
        // Build the subscript-read text, then dispatch the continuation as a method call or property access.
        token afterBracket = file.peekToken();
        if(afterBracket.is(token::period)){
            file.getToken(); // consume '.'
            // Build subscript-read I6 text (same emitter expansion as expression-level path)
            string arrType = resolvePathType(arrPath, func, body);
            classDef* arrCls = dynamic_cast<classDef*>(&languageService.getType(arrType));
            string elemType;
            size_t dotPos = arrPath.find('.');
            if(dotPos == string::npos) elemType = resolveArrayElementType(arrPath, func, body);
            else elemType = resolveArrayElementTypeDotted(arrPath.substr(0, dotPos), arrPath.substr(dotPos + 1), func, body);
            if(elemType.empty() && arrType == "bytearray") elemType = "char";
            functionDef* getMethod = nullptr;
            if(arrCls != nullptr && !elemType.empty())
                getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
            if(getMethod == nullptr)
                parsingError(format("Subscript on '{0}': cannot read element for dot-access", arrPath));
            string subscriptText;
            if(getMethod->isEmitter)
                if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)){
                    string b = processBglConditionals(blk->i6Body);
                    string pv = (arrType == "array" || arrType == "bytearray") ? "0" : "<$prop undefined>";
                    string selfValue = arrPath;
                    size_t innerDot = arrPath.rfind('.');
                    if(innerDot != string::npos){ selfValue = arrPath.substr(0, innerDot); pv = arrPath.substr(innerDot + 1); }
                    b = replaceWord(b, "$self", selfValue);
                    b = replaceWord(b, "$val",  arrPath);
                    b = replaceWord(b, "$prop", pv);
                    if(!getMethod->params.empty())
                        b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                    subscriptText = b;
                }
            // Read member name and dispatch
            token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            string memberName = memberTok.value;
            token afterMember = file.getToken();
            if(afterMember.is(token::parenOpen)){
                // Method call: arr[i].method(args)
                classDef* elemCls = dynamic_cast<classDef*>(&languageService.getType(elemType));
                ParsedArgList pal = parseCallArgList(func, body);
                vector<string> namedArgNames = pal.namedArgNames;
                vector<vector<interpolatedSegment>> interpSegs = pal.interpSegmentsPerArg;
                functionDef* method = bindMethodCall(elemType, subscriptText, memberName,
                    pal.args, namedArgNames, interpSegs);
                functionCallStatement& callStmt = *(new functionCallStatement());
                callStmt.src = stmtLoc;
                callStmt.functionName = subscriptText + "." + memberName;
                callStmt.args = pal.args;
                callStmt.namedArgNames = namedArgNames;
                callStmt.interpSegmentsPerArg = interpSegs;
                if(method->isEmitter && !method->isPrePassStub)
                    if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                        string b = processBglConditionals(blk->i6Body);
                        for(size_t i = 0; i < method->params.size() && i < pal.args.size(); i++)
                            b = replaceWord(b, "$" + method->params[i]->name, pal.args[i]->text());
                        b = replaceWord(b, "$self", subscriptText);
                        b = replaceWord(b, "$val",  subscriptText);
                        callStmt.emitterBody = b;
                    }
                file.getToken(token::endStatement);
                if(body != nullptr) body->statements.push_back(&callStmt);
                return false;
            } else if(afterMember.is(token::assignment)){
                // Property assignment: arr[i].prop = value
                // parseExpression with endStatement terminator consumes through ';'
                expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
                assignmentStatement& assign = *(new assignmentStatement());
                assign.src = stmtLoc;
                assign.variableLeft = subscriptText + "." + memberName;
                assign.assignedExpression = valExpr;
                if(body != nullptr) body->statements.push_back(&assign);
                return false;
            } else {
                parsingError(format("Expected '(' or '=' after '{0}[...].{1}', got '{2}'",
                    arrPath, memberName, afterMember.value));
            }
        }

        file.getToken(token::assignment);
        expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);

        // Resolve array type and compute $self/$prop
        string arrType = resolvePathType(arrPath, func, body);
        // Use getDispatchClass so templated receiver types (e.g. `array<var>` on a
        // parametric param) strip down to the generic class for operator[]= lookup.
        classDef* arrCls = getDispatchClass(arrType);
        if(arrCls == nullptr) parsingError(format("Type '{0}' does not support subscript access", arrType));

        // Element-type-aware lookup: find operator[]= whose second parameter type matches the
        // array's declared element type. Handles both bare (`name[i]=v`) and dotted
        // (`obj.prop[i]=v`) paths by splitting arrPath on '.'.
        string elemType;
        size_t dotPos = arrPath.find('.');
        if(dotPos == string::npos)
            elemType = resolveArrayElementType(arrPath, func, body);
        else
            elemType = resolveArrayElementTypeDotted(arrPath.substr(0, dotPos), arrPath.substr(dotPos + 1), func, body);
        if(elemType.empty() && arrType == "bytearray") elemType = "char";

        functionDef* setMethod = nullptr;
        if(!elemType.empty())
            setMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/true);
        if(setMethod == nullptr){
            if(elemType.empty())
                parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrPath));
            parsingError(format("No operator[]= for element type '{0}' on type '{1}'. Add an overload or use a supported element type.",
                typeDisplayName(elemType), typeDisplayName(arrType)));
        }
        // Validate value type against element type
        string valType = valExpr ? valExpr->resolvedType : "";
        if(!valType.empty() && !isTypeCompatible(valType, elemType))
            parsingError(format("Cannot assign value of type '{0}' to element of array<{1}>",
                typeDisplayName(valType), typeDisplayName(elemType)));

        // Compute $self and $prop
        size_t innerDot = arrPath.rfind('.');
        string selfValue = (innerDot == string::npos) ? arrPath : arrPath.substr(0, innerDot);
        string propValue = (innerDot == string::npos)
            ? (arrType == "array" ? "0" : "<$prop undefined>")
            : arrPath.substr(innerDot + 1);

        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        callStmt.functionName = arrPath + ".set";
        callStmt.args.push_back(indexExpr);
        callStmt.args.push_back(valExpr);

        if(setMethod->isEmitter)
            if(auto* blk = dynamic_cast<i6Block*>(setMethod->body)) {
                string b = processBglConditionals(blk->i6Body);
                size_t pos = 0;
                for(size_t i = 0; i < setMethod->params.size() && i < callStmt.args.size(); i++)
                    b = replaceWord(b, "$" + setMethod->params[i]->name, callStmt.args[i]->text());
                b = replaceWord(b, "$self", selfValue);
                b = replaceWord(b, "$val",  arrPath);
                b = replaceWord(b, "$prop", propValue);
                callStmt.emitterBody = b;
            }
        if(body != nullptr) body->statements.push_back(&callStmt);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }

    if(symbol.is(token::assignment))  {
        assignmentStatement& assignExpr=*(new assignmentStatement());
        assignExpr.src = stmtLoc;
        string lhsOriginal = (string)tok;
        if(isConstVariable(lhsOriginal, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", lhsOriginal));
        if(func != nullptr){
            string qualified = qualifyIdentifier(lhsOriginal, func, body);
            if(qualified.empty())
                parsingError(format("Undeclared variable '{0}'", lhsOriginal));
            assignExpr.variableLeft = qualified;
        } else {
            assignExpr.variableLeft = lhsOriginal;
        }

        // look up the left-hand variable's type using original (unqualified) name
        typeDef* leftType = nullptr;
        string emitterSelfForLhs = lhsOriginal;  // $self value for emitter substitution

        size_t lhsDot = lhsOriginal.rfind('.');
        if(lhsDot != string::npos){
            // dot-path LHS: resolve owner type, then find property type in its class
            string ownerPath = lhsOriginal.substr(0, lhsDot);
            string propName  = lhsOriginal.substr(lhsDot + 1);
            // Check for static member assignment: ClassName.staticMember
            classDef* ownerAsCls = dynamic_cast<classDef*>(&languageService.getType(ownerPath));
            if(ownerAsCls != nullptr){
                for(typeMember* m : ownerAsCls->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->isStatic && vd->name == propName){
                            if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                            string mangledName = "_bgl_" + ownerAsCls->name + "_" + propName;
                            assignExpr.variableLeft = mangledName;
                            emitterSelfForLhs = mangledName;  // $self should be the mangled global, not the owner
                            leftType = &vd->type;
                            break;
                        }
            }
            string ownerType = leftType != nullptr ? "" : resolvePathType(ownerPath, func, body);
            if(!ownerType.empty()){
                classDef* ownerCls = dynamic_cast<classDef*>(&languageService.getType(ownerType));
                if(ownerCls != nullptr)
                    for(typeMember* m : ownerCls->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == propName){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                                leftType = &vd->type; break;
                            }
            }
            if(!emitterSelfForLhs.starts_with("_bgl_"))  // don't override if already set by static member resolution
                emitterSelfForLhs = ownerPath;  // $self = the owner object, not the full obj.prop path
        } else {
            if(func != nullptr){
                for(paramDef* p : func->params)
                    if(p->name == lhsOriginal){ leftType = &p->type; break; }
                if(leftType == nullptr && body != nullptr)
                    for(statement* s : body->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(vd->name == lhsOriginal){ leftType = &vd->type; break; }
                if(leftType == nullptr && currentObject != nullptr)
                    for(typeMember* m : currentObject->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == lhsOriginal){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", lhsOriginal));
                                leftType = &vd->type; break;
                            }
                if(leftType == nullptr && currentClass != nullptr)
                    for(typeMember* m : currentClass->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == lhsOriginal){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", lhsOriginal));
                                leftType = &vd->type; break;
                            }
            }
            if(leftType == nullptr)
                for(typeDef* g : languageService.globals)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == lhsOriginal){ leftType = &vd->type; break; }
        }

        classDef* classType = leftType != nullptr ? dynamic_cast<classDef*>(&languageService.getType(leftType->name)) : nullptr;

        // helper: apply operator= emitter lookup to an assignment node for a given rhs expression
        auto resolveEmitter = [&](assignmentStatement& a, expression* val){
            a.emitterSelf = emitterSelfForLhs;  // always record $self for this assignment
            if(classType != nullptr && val != nullptr){
                string valueTypeName = val->resolvedType;
                if(!valueTypeName.empty()){
                    // Two-pass emitter lookup first — explicit operator= emitters always beat raw type compatibility
                    bool found = false;
                    {
                        typeMember* m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1
                                   && opFunc->params[0]->type.name==valueTypeName && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                        });
                        if(!m) m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1
                                   && opFunc->params[0]->type.name=="var" && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                        });
                        if(m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                            a.emitterBody = processBglConditionals(blk->i6Body);
                            a.emitterParam = opFunc->params[0]->name;
                            // Pre-substitute $class with the LHS's declared type. $self / $param /
                            // $target are substituted later at emit time (i6Emitter), but $class
                            // resolves at parse time because it depends on the static type known here.
                            if(classType != nullptr)
                                a.emitterBody = i6Emitter::replaceWord(a.emitterBody, "$class", classType->i6Name());
                            found = true;
                        }
                    }
                    // Non-emitter operator=: dispatch via a mangled method call so the routine runs
                    // exactly once and the RHS is evaluated exactly once. We synthesize a one-line
                    // emitter body using $target (the full LHS path, e.g. retval.parentWin) so that
                    // member-access assignments dispatch on the property, not its owner.
                    if(!found){
                        typeMember* m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && !opFunc->isEmitter && !opFunc->isPrePassStub
                                   && opFunc->params.size()==1 && opFunc->params[0]->type.name==valueTypeName;
                        });
                        if(!m) m = findMemberInHierarchy(classType, [&](typeMember* m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            return opFunc && opFunc->name=="=" && !opFunc->isEmitter && !opFunc->isPrePassStub
                                   && opFunc->params.size()==1 && opFunc->params[0]->type.name=="var";
                        });
                        if(m){
                            auto* opFunc = dynamic_cast<functionDef*>(m);
                            if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
                            string paramName = opFunc->params[0]->name;
                            a.emitterBody  = format("$target.{0}(${1});", opFunc->i6name, paramName);
                            a.emitterParam = paramName;
                            found = true;
                        }
                    }
                    if(!found) found = isTypeCompatible(valueTypeName, leftType->name);
                    if(!found){
                        // Fallback: check if RHS type has emitter LhsType operator(){}
                        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(valueTypeName));
                        if(rhsCls != nullptr)
                            if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                                       && opFn->returnType.name==leftType->name && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                            })){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                auto* blk = dynamic_cast<i6Block*>(opFn->body);
                                string b = processBglConditionals(blk->i6Body);
                                string argText = val->text();
                                size_t pos = 0;
                                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
                                val->tokens.clear();
                                val->tokens.push_back(b);
                                val->resolvedType = leftType->name;
                                found = true;
                            }
                    }
                    if(!found)
                        parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName(leftType->name)));
                }
            }
        };

        // Interpolated string literal on RHS: var = $"..."
        if(file.peekToken(1).is("$") && file.peekToken(2).is(eTokenType::quote)){
            file.getToken();  // consume '$'
            assignExpr.interpSegments = parseInterpolatedSegments(func, body);
            file.getToken(token::endStatement);  // consume ';'
            // Create a dummy RHS expression typed as interpolatedstringliteral for emitter resolution
            expression* rhs = new expression();
            rhs->resolvedType = "interpolatedstringliteral";
            assignExpr.assignedExpression = rhs;
            resolveEmitter(assignExpr, rhs);
            if(body != nullptr) body->statements.push_back(&assignExpr);
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
            return false;
        }

        // Set expected type from the LHS so name resolution can disambiguate the RHS.
        string savedExpectedAssign = currentExpectedType;
        if(leftType != nullptr) currentExpectedType = leftType->name;
        expression* rhs = parseExpression(file.getToken(), {token::endStatement, "?"}, func, body);
        currentExpectedType = savedExpectedAssign;

        if(rhs->terminator == "?"){
            // conditional assignment: lhs = condition ? trueVal : falseVal
            // build as an ifStatement with two assignment branches, each with full emitter dispatch
            ifStatement& ifStmt = *(new ifStatement());
            ifStmt.src = stmtLoc;
            ifStmt.condition = rhs;

            auto makeAssign = [&](expression* val) -> assignmentStatement* {
                assignmentStatement* a = new assignmentStatement();
                a->src = stmtLoc;
                a->variableLeft = assignExpr.variableLeft;
                a->assignedExpression = val;
                resolveEmitter(*a, val);
                return a;
            };

            expression* trueVal  = parseExpression(file.getToken(), {":"}, func, body);
            expression* falseVal = parseExpression(file.getToken(), {token::endStatement}, func, body);

            ifStmt.thenBlock = new statementBlock();
            ifStmt.thenBlock->statements.push_back(makeAssign(trueVal));
            ifStmt.elseBlock = new statementBlock();
            ifStmt.elseBlock->statements.push_back(makeAssign(falseVal));

            if(body != nullptr) body->statements.push_back(&ifStmt);
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
            return false;
        }

        assignExpr.assignedExpression = rhs;
        // Skip operator= emitter if RHS contains $target — the opcode handles its own store
        if(rhs->text().find("$target") == string::npos)
            resolveEmitter(assignExpr, rhs);

        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&assignExpr);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    // Compound assignment: +=, -=, *=, /=, %=, |=, &=, ^=, <<=, >>=
    static const vector<string> compoundOps = {"+=","-=","*=","/=","%=","|=","&=","^=","<<=",">>="};
    if(symbol.is(eTokenType::oper) && find(compoundOps.begin(), compoundOps.end(), symbol.value) != compoundOps.end()){
        string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
        if(isConstVariable(tok.value, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", tok.value));
        expression* rhs = parseExpression(file.getToken(), {token::endStatement}, func, body);

        // Try emitter lookup for this compound operator on the LHS type
        string lhsTypeName = resolveIdentifierType(tok.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
        bool emitterFound = false;
        if(lhsClass != nullptr && rhs != nullptr && !rhs->resolvedType.empty()){
            string rhsType = rhs->resolvedType;
            // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all
            typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                       && opFunc->params.size()==1 && opFunc->params[0]->type.name==rhsType
                       && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
            });
            // Conversion fallback: check if RHS type converts to a type the operator accepts
            if(!m){
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhsType));
                if(rhsCls != nullptr)
                    for(typeMember* rm : rhsCls->members){
                        auto* convFn = dynamic_cast<functionDef*>(rm);
                        if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
                        string convertedType = convFn->returnType.name;
                        m = findMemberInHierarchy(lhsClass, [&](typeMember* m2){
                            auto* opFunc = dynamic_cast<functionDef*>(m2);
                            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                                   && opFunc->params.size()==1 && opFunc->params[0]->type.name==convertedType
                                   && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                        });
                        if(m) break;
                    }
            }
            // var wildcard fallback
            if(!m) m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                       && opFunc->params.size()==1 && opFunc->params[0]->type.name=="var"
                       && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
            });
            if(m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                assignmentStatement& a = *(new assignmentStatement());
                a.src = stmtLoc;
                a.variableLeft = lhs;
                a.assignedExpression = rhs;
                a.emitterBody = processBglConditionals(blk->i6Body);
                a.emitterParam = opFunc->params[0]->name;
                a.emitterSelf = lhs;
                if(body != nullptr) body->statements.push_back(&a);
                emitterFound = true;
            }
        }
        if(!emitterFound){
            if(!lhsTypeName.empty() && lhsTypeName != "var")
                parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(lhsTypeName)));
            // No emitter and untyped: expand to I6 form: x op= y  →  x = x op y;
            string op = symbol.value.substr(0, symbol.value.size() - 1); // strip trailing '='
            string rhsText = rhs != nullptr ? rhs->text() : "";
            i6RawNode& node = *(new i6RawNode());
            node.text = lhs + " = " + lhs + " " + op + " " + rhsText + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
        }
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    if(symbol.is(eTokenType::oper) && (symbol.value == "++" || symbol.value == "--")){
        file.getToken(token::endStatement);
        string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
        if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
        if(isConstVariable(tok.value, func, body))
            parsingError(format("Cannot assign to const variable '{0}'", tok.value));
        // Try emitter lookup for this operator on the LHS type
        string lhsTypeName = resolveIdentifierType(tok.value, func, body);
        classDef* lhsClass = dynamic_cast<classDef*>(&languageService.getType(lhsTypeName));
        bool emitterFound = false;
        if(lhsClass != nullptr){
            if(typeMember* m = findMemberInHierarchy(lhsClass, [&](typeMember* m){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                       && opFunc->params.empty() && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
            })){
                auto* opFunc = dynamic_cast<functionDef*>(m);
                auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                string b = processBglConditionals(blk->i6Body);
                b = replaceWord(b, "$self", lhs);
                b = replaceWord(b, "$val",  lhs);
                i6RawNode& node = *(new i6RawNode());
                node.text = b + ";";
                node.src = stmtLoc;
                if(body != nullptr) body->statements.push_back(&node);
                emitterFound = true;
            }
        }
        if(!emitterFound){
            if(!lhsTypeName.empty() && lhsTypeName != "var")
                parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(lhsTypeName)));
            i6RawNode& node = *(new i6RawNode());
            node.text = lhs + symbol.value + ";";
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
        }
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    if(symbol.is(token::parenOpen))  { //then this is a function call.

        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        // Qualify bare function name: if inside an instance and the name matches an instance
        // member method, prepend "self." so it routes to the method call path below.
        {
            string rawName = (string)tok;
            // replace chaining: replaced() resolves to the predecessor's mangled name
            if(rawName == "replaced" && currentFunc && !currentFunc->replacedTarget.empty()){
                rawName = currentFunc->replacedTarget;
                currentFunc->replacedWasCalled = true;
            }
            if(func != nullptr && rawName.find('.') == string::npos){
                string qualified = qualifyIdentifier(rawName, func, body);
                // qualifyIdentifier walks inherited VARIABLES but not functions.
                // For call-form resolution, also check the class hierarchy for inherited methods.
                // Skip if the name also exists as a global function (global arity matching wins).
                if((qualified.empty() || qualified == rawName) && currentClass != nullptr){
                    bool isGlobalFunc = false;
                    for(typeDef* g : languageService.globals)
                        if(auto* fd = dynamic_cast<functionDef*>(g))
                            if(fd->name == rawName){ isGlobalFunc = true; break; }
                    if(!isGlobalFunc){
                        function<bool(classDef*)> searchHierarchy = [&](classDef* c) -> bool {
                            for(typeMember* m : c->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == rawName) return true;
                            for(classDef* base : c->baseClasses)
                                if(searchHierarchy(base)) return true;
                            return false;
                        };
                        if(searchHierarchy(currentClass)) qualified = "self." + rawName;
                    }
                }
                callStmt.functionName = qualified.empty() ? rawName : qualified;
            } else {
                callStmt.functionName = rawName;
            }
        }

        // parse argument list
        {
            ParsedArgList pal = parseCallArgList(func, body);
            callStmt.args = pal.args;
            callStmt.namedArgNames = pal.namedArgNames;
            callStmt.interpSegmentsPerArg = pal.interpSegmentsPerArg;
        }

        string chainReturnType;
        size_t dotPos = callStmt.functionName.rfind('.');  // use LAST dot for method name
        if(dotPos != string::npos){
            // method call: validate and resolve emitter
            string objectPath = callStmt.functionName.substr(0, dotPos);  // may be "obj" or "obj.prop"
            string methodName = callStmt.functionName.substr(dotPos + 1);
            string objectName = objectPath;  // kept for backward compat in non-emitter emit path
            // Pass memberHint=methodName so the resolver disambiguates a name collision in favor
            // of whichever candidate's type actually exposes the method.
            string objectType = !stmtCastType.empty() ? stmtCastType
                              : !literalTypeName.empty() ? literalTypeName
                              : resolvePathType(objectPath, func, body, methodName);
            stmtCastType = "";  // consume the cast
            if(objectType.empty())
                parsingError(format("Unknown variable '{0}'", objectPath));
            // Compute $self and $prop for emitter substitution.
            // For literals, $self is the raw literal text (e.g. "hello", 42, 'x'), not the path.
            size_t innerDot = objectPath.rfind('.');
            string selfValue = (!literalSelfText.empty() && innerDot == string::npos)
                              ? literalSelfText
                              : (innerDot == string::npos) ? objectPath : objectPath.substr(0, innerDot);
            string propValue = (innerDot == string::npos)
                ? (objectType == "array" ? "0" : "<$prop undefined>")
                : objectPath.substr(innerDot + 1);
            // Receiver type can be a classDef OR an objectDef (each unclassed objectDef has its
            // own type identity); both have addressable methods.
            typeDef& objTd2 = languageService.getType(objectType);
            classDef* cls = dynamic_cast<classDef*>(&objTd2);
            bool opaqueReceiver = (cls == nullptr && dynamic_cast<objectDef*>(&objTd2) == nullptr);
            // Generic specialization fallback: templated receiver name (`array<int>` from
            // a parametric param) isn't a registered type, but its base ("array") is. Treat
            // as non-opaque if the base resolves to a class — bindMethodCall handles the
            // element-type binding for substitution.
            if(opaqueReceiver){
                auto lt = objectType.find('<');
                if(lt != string::npos && lt > 0){
                    typeDef& baseTd = languageService.getType(objectType.substr(0, lt));
                    if(dynamic_cast<classDef*>(&baseTd) != nullptr){
                        opaqueReceiver = false;
                        cls = dynamic_cast<classDef*>(&baseTd);
                    }
                }
            }
            if(opaqueReceiver && !looseIdentifierMode)
                parsingError(format("Type '{0}' is not a class or object", objectType));
            if(opaqueReceiver){
                // Loose mode: receiver is unknown to Beguile (typically an I6 symbol). Skip
                // method binding and emitter substitution; the call statement emits the
                // verbatim `path.method(args)`, which is valid I6. Carry original case via
                // the inherited displayName field (same convention as typeMember.dName()).
                chainReturnType = "var";
                callStmt.displayName = tok.originalValue;
            } else {
                // Element-type binding for generic receivers (array<T>, etc.).
                string recvElemType = resolveArrayElementType(selfValue, currentFunc, currentFunc ? dynamic_cast<statementBlock*>(currentFunc->body) : nullptr);
                functionDef* method = bindMethodCall(objectType, objectPath, methodName,
                                                       callStmt.args, callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                                       recvElemType);
                cls = dynamic_cast<classDef*>(&languageService.getType(objectType));
                // if emitter, pre-substitute $self, $prop, and $class
                if(method->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = replaceWord(b, "$self", selfValue);
                        b = replaceWord(b, "$val",  objectPath);
                        // $class — declared receiver type (ignores multiple inheritance).
                        // Resolves to the variable's static type, not the type that owns the
                        // inherited emitter. Powers class-message I6 emission from mixins.
                        if(cls != nullptr)
                            b = replaceWord(b, "$class", cls->i6Name());
                        // $prop fallback — done before staging callStmt.emitterBody so that
                        // resolveEmitterText's later param substitution can still substitute
                        // a `prop`-named parameter when present (e.g. `provides(property prop)`).
                        // Skip if any parameter is named `prop` so the param sub wins.
                        bool hasPropParam = false;
                        for(paramDef* p : method->params) if(p->name == "prop"){ hasPropParam = true; break; }
                        if(!hasPropParam)
                            b = replaceWord(b, "$prop", propValue);
                        callStmt.emitterBody = b;
                        for(paramDef* p : method->params)
                            callStmt.emitterParams.push_back(p->name);
                    }
                chainReturnType = method->returnType.name;
            }
        } else {
            // global function call: bind (resolve + validate + finalize) then stage emitter body
            GlobalCallBinding gcb = bindGlobalCall(callStmt.functionName, callStmt.args,
                                                    callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                                    func, body);
            if(!gcb.funcVarReturnType.empty())  chainReturnType = gcb.funcVarReturnType;
            else if(gcb.method != nullptr)      chainReturnType = gcb.method->returnType.name;
            else                                chainReturnType = "var"; // loose mode: unresolved → opaque
            if(gcb.method && gcb.method->isEmitter)
                if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
                    callStmt.emitterBody = processBglConditionals(blk->i6Body);
                    for(paramDef* p : gcb.method->params) callStmt.emitterParams.push_back(p->name);
                }
            // Loose-mode unresolved global call: carry original case via displayName so
            // the emitter can prefer it over the lowercased functionName.
            if(gcb.method == nullptr && gcb.funcVarReturnType.empty() && looseIdentifierMode)
                callStmt.displayName = tok.originalValue;
        }

        // method chaining: handle optional ".method()" suffixes before the final ";"
        auto resolveEmitterText = [&](functionCallStatement& cs) -> string {
            string b = cs.emitterBody;
            for(size_t i=0; i<cs.emitterParams.size() && i<cs.args.size(); i++)
                b = replaceWord(b, cs.emitterParams[i], cs.args[i]->text());
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1);
            return b;
        };
        token chainTok = file.getToken();
        while(chainTok.is(token::period) || chainTok.is(eTokenType::dictionaryWord)){
            // After ')' the lexer returns '.method' as a dictionaryWord; after an identifier it returns '.' + identifier separately.
            token chainMember;
            if(chainTok.is(token::period))
                chainMember = file.getToken(eTokenType::identifier);
            else
                chainMember = chainTok;  // dictionaryWord already holds the method name
            file.getToken(token::parenOpen);
            vector<expression*> chainArgs;
            token chainArgTok = file.getToken();
            while(chainArgTok.isNot(token::parenClose)){
                expression* arg = parseExpression(chainArgTok, {token::comma, token::parenClose}, func, body);
                chainArgs.push_back(arg);
                if(arg->terminator == token::parenClose) break;
                chainArgTok = file.getToken();
            }
            classDef* chainCls = dynamic_cast<classDef*>(&languageService.getType(chainReturnType));
            if(chainCls == nullptr)
                parsingError(format("Type '{0}' is not a class (cannot chain method '{1}')", chainReturnType, chainMember.value));
            string chainMethodName = chainMember.value;
            functionDef* chainMethod = nullptr;
            functionDef* chainNameMatch = nullptr;
            findMemberInHierarchy(chainCls, [&](typeMember* m) -> bool {
                auto* fd = dynamic_cast<functionDef*>(m);
                if(!fd || fd->name != chainMethodName) return false;
                if(chainNameMatch == nullptr) chainNameMatch = fd;
                size_t req=0; for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                if(chainArgs.size() >= req && chainArgs.size() <= fd->params.size()){
                    chainMethod = fd; return true;
                }
                return false;
            });
            // Conversion operator fallback for chained methods
            if(chainNameMatch == nullptr && chainCls){
                for(typeMember* m : chainCls->members){
                    auto* convOp = dynamic_cast<functionDef*>(m);
                    if(convOp && convOp->name == "operator()" && convOp->isEmitter && !convOp->isExplicit){
                        string convertedType = convOp->returnType.name;
                        classDef* convCls = dynamic_cast<classDef*>(&languageService.getType(convertedType));
                        if(convCls){
                            findMemberInHierarchy(convCls, [&](typeMember* m2) -> bool {
                                auto* fd = dynamic_cast<functionDef*>(m2);
                                if(!fd || fd->name != chainMethodName) return false;
                                if(chainNameMatch == nullptr) chainNameMatch = fd;
                                size_t req=0; for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                                if(chainArgs.size() >= req && chainArgs.size() <= fd->params.size()){
                                    chainMethod = fd; return true;
                                }
                                return false;
                            });
                            if(chainNameMatch){
                                chainReturnType = convertedType;
                                chainCls = convCls;
                                break;
                            }
                        }
                    }
                }
            }
            if(chainNameMatch == nullptr)
                parsingError(format("No method '{0}' on type '{1}'", chainMethodName, typeDisplayName(chainReturnType)));
            if(chainMethod == nullptr)
                parsingError(format("Method '{0}' on type '{1}' has wrong arity for {2} argument(s)",
                    chainMethodName, chainReturnType, chainArgs.size()));
            if(!chainMethod->isEmitter || !dynamic_cast<i6Block*>(chainMethod->body))
                parsingError(format("Chained method '{0}' on type '{1}' is not an emitter", chainMethodName, chainReturnType));
            string selfText = resolveEmitterText(callStmt);
            i6Block* chainBlk = dynamic_cast<i6Block*>(chainMethod->body);
            string b = processBglConditionals(chainBlk->i6Body);
            b = replaceWord(b, "$self", selfText);
            b = replaceWord(b, "$val",  selfText);
            for(size_t i=0; i<chainMethod->params.size() && i<chainArgs.size(); i++)
                b = replaceWord(b, "$" + chainMethod->params[i]->name, chainArgs[i]->text());
            callStmt.emitterBody = b;
            callStmt.emitterParams.clear();
            callStmt.args.clear();
            chainReturnType = chainMethod->returnType.name;
            chainTok = file.getToken();
        }
        chainTok.assert(token::endStatement);

        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&callStmt);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }
    return parsingError(format("Unhandled token '{0}'",tok.value));
}

/*bool bglParser::processDataType(token dataType, bool isExternal){
    token name;
    token symbol;

    // if(dataType.is(token::constantDeclararion)){
    //     dataType = file.getToken(eTokenType::dataType);
    //     name = file.getToken(eTokenType::identifier);
    //     symbol = file.getToken(token::assignment);
    //     processConstantDeclaration(dataType, name, symbol);        
    //     file.getToken(token::endStatement);
    //     return false;
    // }
    
    name = file.getToken(eTokenType::identifier);
    if(name.is("operator")){
        //set is operator value here.
        name = file.getToken(eTokenType::oper);    
    }
    
    symbol = file.getToken({eTokenType::symbol, eTokenType::oper}); 

    //--a variable declaration, with optional assignment:
    //      int myVar;
    //      int myVar=99; 
    if(symbol.isOneOf({token::endStatement, token::assignment})) {
        processVariableDeclaration(dataType, name, symbol);
        if(symbol.value==token::assignment) file.getToken(token::endStatement);
        return false;
    }
  
    //--a function declaration:
    if(symbol.is(token::parenOpen)) return processRoutineDeclaration(dataType, name, isExternal);

    //--an object instance declaration:
    if(symbol.is(token::braceOpen)) return processObjectDeclaration(dataType, name, isExternal);

    return parsingError("Unexpected value '"+symbol.value+"'.");
   
}*/
// bool parser::processAttribute(token tok){

// }
// Resolves the Beguile type of a potentially dotted path (e.g. "player.inventory").
// Single segment: delegates to resolveIdentifierType.
// Two segments: resolves head type, then looks up tail member on that type.
std::string bglParser::resolvePathType(std::string path, functionDef* func, statementBlock* body, const string& memberHint) {
    size_t dot = path.find('.');
    if(dot == string::npos) return resolveIdentifierType(path, func, body, memberHint);
    string head = path.substr(0, dot);
    string tail = path.substr(dot + 1);
    // Don't pass memberHint to the head — the hint is about the final tail's resolved type, not
    // about a member on the head. Head-resolution uses normal first-match priority.
    string headType = resolveIdentifierType(head, func, body);

    // Check the specific object instance's members first (for instance-level properties)
    string lowerHead = head;
    transform(lowerHead.begin(), lowerHead.end(), lowerHead.begin(), ::tolower);
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            if(od->name == lowerHead)
                for(typeMember* m : od->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == tail) return vd->type.name;

    // Then check the type's class hierarchy
    typeDef& td = languageService.getType(headType);
    if(auto* od = dynamic_cast<objectDef*>(&td))
        for(typeMember* m : od->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == tail) return vd->type.name;
    if(auto* cd = dynamic_cast<classDef*>(&td)){
        typeMember* found = findMemberInHierarchy(cd, [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd != nullptr && vd->name == tail;
        });
        if(found) return dynamic_cast<variableDeclaration*>(found)->type.name;
    }
    return "";
}

// Process an array<T> declaration: array<T> name[N]; or array<T> name = { ... };
bool bglParser::processArrayDeclaration(token dataType, token name, string elementType, token symbol, abstractObject& contextObj, bool isExternal) {
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    arrDecl.src = file.currentLocation();
    arrDecl.name = (string)name;
    arrDecl.type = languageService.getType(elementType == "char" ? "bytearray" : "array");
    arrDecl.elementType = elementType;
    arrDecl.isExternal = isExternal;
    if(elementType == "char") arrDecl.isByteArray = true;

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    if(symbol.is(token::bracketOpen)) {
        // array<T> name[N];
        token sizeTok = file.getToken(eTokenType::integer);
        arrDecl.arraySize = stoi(sizeTok.value);
        file.getToken(token::bracketClose);
        file.getToken(token::endStatement);
    } else if(symbol.is(token::assignment)) {
        // Check for string initializer: array<char> name = "text";
        token firstVal = file.peekToken(1);
        if(firstVal.is(eTokenType::quote) || firstVal.is(eTokenType::rawQuote)){
            if(elementType != "char" && elementType != "charliteral")
                parsingError("String initializer is only valid for array<char>");
            token strTok = file.getToken();
            arrDecl.stringInitializer = strTok.value;
            file.getToken(token::endStatement);
        } else if(firstVal.is(token::braceOpen)){
            // array<T> name = { v1, v2, ... };
            file.getToken(token::braceOpen);
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)) {
                expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            file.getToken(token::endStatement);
            // type-check each element against the declared element type
            if(!elementType.empty() && elementType != "var"){
                for(size_t i = 0; i < list->elements.size(); i++){
                    expression* elem = list->elements[i];
                    if(!elem->resolvedType.empty() && !isTypeCompatible(elem->resolvedType, elementType))
                        parsingError(format("Array element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, elementType));
                }
            }
            arrDecl.declaredExpressionValue = list;
        } else {
            // array<T> name = expression; — pointer-aliasing init. The RHS is an arbitrary
            // expression evaluating to an array pointer (typically a function return or another
            // array global). No storage is allocated for this local; the slot just holds the
            // pointer. arraySize stays 0 so the emitter's local-array allocation path skips it.
            token first = file.getToken();
            expression* expr = parseExpression(first, {token::endStatement}, func, body);
            arrDecl.declaredExpressionValue = expr;
        }
    }
    // else symbol is endStatement: extern/forward declaration — no size or initializer

    if(body != nullptr)
        body->statements.push_back(&arrDecl);
    else
        languageService.registerInstance(arrDecl);
    return false;
}

//--process a variable declaration statement and add it to the parse tree
//-- handles both simple declarations and declarations with assignment
//-- also handles parameter declarations within function definitions
//-- e.g.:
//--      int myVar;
//--      int myVar=99;
//--      int myParam)
//--      int myParam=5) 
//--      int myParam=5, ...
bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol, abstractObject& contextObj, bool isExternal, bool isConst, string i6alias){
    // Ban bare `array` as a type — every array variable must declare its element type.
    // `array<T>` / `array<char>` are handled earlier via processArrayDeclaration and never
    // reach here as a plain variable declaration.
    if(dataType.value == "array")
        parsingError("bare 'array' is not a valid type — declare the element type: array<T>");
    // Extern Type name ; where Type is verb-derived → route to object declaration (verb instance)
    if(isExternal && symbol.is(token::endStatement)){
        bool verbDerived = false;
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(dataType.value))){
            function<bool(classDef*)> checkVerb = [&](classDef* c) -> bool {
                if(!c) return false;
                if(c->name == "verb") return true;
                for(classDef* b : c->baseClasses) if(checkVerb(b)) return true;
                return false;
            };
            verbDerived = checkVerb(cls);
        }
        if(verbDerived)
            return processObjectDeclaration(dataType, variableName, true, "", "", false);
    }
    variableDeclaration& varDecl = *new variableDeclaration();
    varDecl.src = file.currentLocation();
    varDecl.name=(string) variableName;
    varDecl.displayName = variableName.originalValue;
    if(!dataType.docComment.empty())          varDecl.docComment = dataType.docComment;
    else if(!variableName.docComment.empty()) varDecl.docComment = variableName.docComment;
    varDecl.type=languageService.getType((string) dataType);
    if(!i6alias.empty()) varDecl.i6name = i6alias;
    varDecl.isExternal=isExternal;
    varDecl.isConst=isConst;
    // For func<...> types, getType returns the base "func" type. Set the full parameterized name.
    {
        string dtLower = (string)dataType;
        transform(dtLower.begin(), dtLower.end(), dtLower.begin(), ::tolower);
        if(dtLower.rfind("func<", 0) == 0) varDecl.type.name = dtLower;
    }

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    // Disallow local variable names that shadow a global, a class member, or an object member;
    // also disallow duplicate declarations within the same scope.
    if(func != nullptr){
        // Duplicate in same scope
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == varDecl.name)
                        parsingError(format("Variable '{0}' is already declared in this scope", varDecl.name));
        for(typeDef* g : languageService.globals)
            if(g->name == varDecl.name){
                if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                    const string& t = vd->type.name;
                    if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                }
                parsingWarning("Local variable '" + varDecl.name + "' shadows global of the same name; the global is unreachable from this scope.");
            }
        if(currentClass != nullptr){
            for(typeMember* m : currentClass->members)
                if(m->name == varDecl.name)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows a member of class '" + currentClass->name + "'.");
            // Walk base class hierarchy for inherited members (vars and functions) — warning only
            function<void(classDef*)> checkBases = [&](classDef* c){
                for(typeMember* m : c->members)
                    if(m->name == varDecl.name)
                        if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                            parsingWarning("Local variable '" + varDecl.name + "' shadows inherited member '" + varDecl.name + "' from class '" + c->dName() + "'.");
                for(classDef* base : c->baseClasses) checkBases(base);
            };
            for(classDef* base : currentClass->baseClasses) checkBases(base);
        }
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(m->name == varDecl.name)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows a member of object '" + currentObject->name + "'.");
        // Inside a lambda: warn if local shadows a capturable outer variable (param or local
        // from the enclosing function). Without this warning, the outer variable becomes
        // silently unreachable — the lambda-local wins at Tier 1, preventing Tier 7 capture.
        if(lambdaOuterFunc != nullptr){
            for(paramDef* p : lambdaOuterFunc->params)
                if(p->name == varDecl.name){
                    parsingWarning("Local variable '" + varDecl.name + "' shadows capturable parameter '" + varDecl.name + "' from enclosing function.");
                    break;
                }
            statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
            if(rootBody != nullptr){
                function<bool(statementBlock*)> findInBlock = [&](statementBlock* blk) -> bool {
                    if(!blk) return false;
                    for(statement* s : blk->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(vd->name == varDecl.name) return true;
                    return false;
                };
                // Check immediate enclosing body and function root body
                bool found = false;
                if(lambdaOuterBody != nullptr) found = findInBlock(lambdaOuterBody);
                if(!found && rootBody != lambdaOuterBody) found = findInBlock(rootBody);
                if(found)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows capturable local '" + varDecl.name + "' from enclosing function.");
            }
        }
    }

    bool isAuto = (dataType.value == "auto");
    if(isAuto && symbol.value != token::assignment)
        parsingError("'auto' requires an initializer to infer the type");

    if(symbol.value==token::assignment){
        token first = file.getToken();
        if(first.is(token::braceOpen)){
            // initializer list: { expr, expr, ... }
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            // consume the trailing semicolon
            file.getToken(token::endStatement);

            // type-check each element against the declared element type
            string dtStr = (string)dataType;
            string expectedElemType;
            // For array<T>, extract T as the expected element type
            if(dtStr.size() > 6 && dtStr.substr(0, 6) == "array<" && dtStr.back() == '>')
                expectedElemType = dtStr.substr(6, dtStr.size() - 7);
            // Fallback: infer from the class's single-param method
            if(expectedElemType.empty()){
                classDef* listClass = dynamic_cast<classDef*>(&languageService.getType(dtStr));
                if(listClass != nullptr)
                    for(typeMember* m : listClass->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
            }
            for(size_t i = 0; i < list->elements.size(); i++){
                expression* elem = list->elements[i];
                if(elem->resolvedType.empty())
                    parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                else if(!expectedElemType.empty() && !isTypeCompatible(elem->resolvedType, expectedElemType))
                    parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
            }

            varDecl.declaredExpressionValue = list;
        } else if(first.is("$") && file.peekToken(1).is(eTokenType::quote)){
            // Interpolated string literal initializer: string s = $"..."
            varDecl.interpSegments = parseInterpolatedSegments(func, body);
            file.getToken(token::endStatement);  // consume ';'
            expression* rhs = new expression();
            rhs->resolvedType = "interpolatedstringliteral";
            varDecl.declaredExpressionValue = rhs;
            // Emitter lookup for operator=(interpolatedstringliteral)
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr){
                functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="interpolatedstringliteral";
                }));
                if(assignOp && assignOp->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                        varDecl.initEmitterBody  = processBglConditionals(blk->i6Body);
                        varDecl.initEmitterParam = assignOp->params[0]->name;
                    }
                }
                if(!assignOp)
                    parsingError(format("Type '{0}' has no operator=(interpolatedStringLiteral) emitter", (string)dataType));
            }
        } else {
            // Set expected type for the RHS so name resolution can disambiguate (e.g. an
            // enum value of the declared type wins over a same-name value of an unrelated enum).
            // For `auto`, no expected type is known until we see the RHS — leave it empty.
            string savedExpected = currentExpectedType;
            if(!isAuto) currentExpectedType = (string)dataType;
            expression* rhs = parseExpression(first, {token::endStatement}, func, body);
            currentExpectedType = savedExpected;
            varDecl.declaredExpressionValue = rhs;

            // auto type inference: resolve type from RHS, checking operator auto() for type promotion
            if(isAuto && rhs != nullptr && !rhs->resolvedType.empty()){
                string inferredType = rhs->resolvedType;
                // Check if the RHS type has operator auto() — use its return type instead
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(inferredType));
                if(rhsCls){
                    for(typeMember* m : rhsCls->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->name == "auto"){ inferredType = fd->returnType.name; break; }
                }
                varDecl.type = languageService.getType(inferredType);
                if(varDecl.type.name.empty()) varDecl.type.name = inferredType;
                dataType.value = inferredType;  // update for downstream type checks
            } else if(isAuto){
                parsingError("Cannot infer type from initializer expression");
            }

            //type check: if the declared type is a class, verify the assigned value is accepted by one of its operator= signatures
            classDef* classType=dynamic_cast<classDef*>(&languageService.getType((string)dataType));
            if(classType != nullptr && rhs != nullptr){
                string valueTypeName = rhs->resolvedType;
                if(!valueTypeName.empty()){
                    // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all.
                    // Always run findMemberInHierarchy so we can capture the emitter body if found.
                    functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name==valueTypeName;
                    }));
                    if(!assignOp) assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m);
                        return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="var";
                    }));
                    bool found = assignOp != nullptr || isTypeCompatible(valueTypeName, (string)dataType);
                    // If operator= is an emitter, capture its body so the emitter uses it instead of plain assignment
                    // Skip if RHS contains $target — the opcode handles its own store
                    if(assignOp && assignOp->isEmitter && rhs->text().find("$target") == string::npos){
                        if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                            varDecl.initEmitterBody  = processBglConditionals(blk->i6Body);
                            varDecl.initEmitterParam = assignOp->params[0]->name;
                            // Pre-substitute $class — the declared LHS type. Mirrors the
                            // assignment-statement path; $self/$target stay deferred to emit time.
                            varDecl.initEmitterBody = i6Emitter::replaceWord(varDecl.initEmitterBody, "$class", classType->i6Name());
                        }
                    }
                    if(!found){
                        // Fallback: check if RHS type has emitter DeclaredType operator(){}
                        classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(valueTypeName));
                        if(rhsCls != nullptr)
                            if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                                       && opFn->returnType.name==(string)dataType && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                            })){
                                auto* opFn = dynamic_cast<functionDef*>(m);
                                auto* blk = dynamic_cast<i6Block*>(opFn->body);
                                string b = processBglConditionals(blk->i6Body);
                                string argText = rhs->text();
                                size_t pos = 0;
                                while((pos = b.find("$self", pos)) != string::npos){ b.replace(pos, 5, argText); pos += argText.size(); }
                                rhs->tokens.clear();
                                rhs->tokens.push_back(b);
                                rhs->resolvedType = (string)dataType;
                                found = true;
                            }
                    }
                    if(!found) parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName((string)dataType)));
                }
            }
        }
    }

    // Flush ternary-lowering injections before the variable declaration that uses _bgl_temp
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();

    if(body != nullptr)
        body->statements.push_back(&varDecl);
    else
        languageService.registerInstance(varDecl);

    // init / deinit: if the variable's type is a class with init/deinit emitters, inject them
    // For local variables: inject init as an i6RawNode and register deinit in func->cleanups.
    // For global variables: record init in languageService.globalInits for the synthesised bglInit routine.
    if(!isConst && body != nullptr && func != nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0) continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = processBglConditionals(blk->i6Body);
                // trim
                size_t s=bodyText.find_first_not_of(" \t\n\r"); if(s!=string::npos) bodyText=bodyText.substr(s);
                size_t e=bodyText.find_last_not_of(" \t\n\r");  if(e!=string::npos) bodyText=bodyText.substr(0,e+1);
                if(bodyText.empty()) continue;
                // substitute $self / $val with variable name
                string substituted = replaceWord(bodyText, "$self", varDecl.name);
                substituted = replaceWord(substituted, "$val",  varDecl.name);
                if(fn->name == "init"){
                    i6RawNode& initNode = *(new i6RawNode());
                    initNode.text = substituted;
                    // insert before varDecl (which is currently the last element)
                    body->statements.insert(body->statements.end()-1, &initNode);
                } else if(fn->name == "deinit"){
                    func->cleanups.push_back({varDecl.name, substituted});
                }
            }
        }
    }

    // Global scope: record init body in globalInits for bglInit
    if(!isConst && body == nullptr && func == nullptr){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(varDecl.type.name));
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0 || fn->name != "init") continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                string bodyText = processBglConditionals(blk->i6Body);
                size_t s=bodyText.find_first_not_of(" \t\n\r"); if(s!=string::npos) bodyText=bodyText.substr(s);
                size_t e=bodyText.find_last_not_of(" \t\n\r");  if(e!=string::npos) bodyText=bodyText.substr(0,e+1);
                if(bodyText.empty()) continue;
                string subbed = replaceWord(bodyText, "$self", varDecl.name);
                subbed = replaceWord(subbed, "$val", varDecl.name);
                languageService.globalInits.push_back({varDecl.name, subbed});
            }
        }
    }

    return false;
}
// Evaluate ##ifdef/##ifndef/##else/##endif directives in a raw emitter body string.
// Sections guarded by a false condition are removed from the output entirely.
// Nesting is supported. The ## directives are never included in the output.
string bglParser::processBglConditionals(const string& text){
    string result;
    size_t pos = 0;

    struct Frame { bool shouldEmit; bool ifCondTrue; bool parentEmit; };
    vector<Frame> stk = {{true, true, true}};

    auto readIdent = [&]() -> string {
        string id;
        while(pos < text.size() && (isalnum((unsigned char)text[pos]) || text[pos]=='_'))
            id += (char)tolower(text[pos++]);
        return id;
    };
    auto skipSpaces = [&](){
        while(pos < text.size() && text[pos]==' ') pos++;
    };

    while(pos < text.size()){
        if(pos+1 < text.size() && text[pos]=='#' && text[pos+1]=='#'){
            pos += 2;
            string dir = readIdent();
            skipSpaces();

            if(dir == "ifdef" || dir == "ifndef"){
                // Only `##if` is valid inside emitter bodies per spec §7.2. The single-hash
                // forms `#ifdef` / `#ifndef` exist as top-level directives, but their
                // double-hash counterparts were never part of the emitter-body grammar.
                parsingError(format("'##{0}' is not valid in an emitter body — use '##if <expr>' instead", dir));
            } else if(dir == "if"){
                string condText;
                while(pos < text.size() && text[pos] != '\n' && !(text[pos] == '#' && pos+1 < text.size() && text[pos+1] == '#'))
                    condText += text[pos++];
                while(!condText.empty() && (condText.back() == ' ' || condText.back() == ';')) condText.pop_back();
                bool condTrue = evaluateCondition(condText);
                bool parent = stk.back().shouldEmit;
                stk.push_back({parent && condTrue, condTrue, parent});
            } else if(dir == "else"){
                if(stk.size() > 1){
                    bool ifWasTrue = stk.back().ifCondTrue;
                    bool parent    = stk.back().parentEmit;
                    stk.pop_back();
                    stk.push_back({parent && !ifWasTrue, !ifWasTrue, parent});
                }
            } else if(dir == "endif"){
                if(stk.size() > 1) stk.pop_back();
            } else {
                // Not a known directive — pass ##identifier through verbatim (e.g. ##VerbName in emitter bodies)
                if(stk.back().shouldEmit) result += "##" + dir;
            }
            // known directive text is never forwarded to output
        } else {
            if(stk.back().shouldEmit) result += text[pos];
            pos++;
        }
    }
    return result;
}

// Evaluate a #if / #elif boolean expression against definedSymbols.
// Supports: symbol, integer literal, !expr, expr&&expr, expr||expr, (expr),
//           and comparison operators: == != < > <= >=
// Precedence (low→high): || → && → ! → comparison → atom
// (macro system removed — emitters with ##if cover the use case)

bool bglParser::evaluateCondition(const string& expr){
    struct Eval {
        const string& s;
        size_t pos = 0;
        map<string,string>& syms;

        void skipWs(){ while(pos<s.size() && isspace(s[pos])) pos++; }

        // Read a value atom — returns its string representation.
        // Symbols resolve to their defined value ("" → "1"), undefined → "0".
        // Numeric literals return the literal text.
        string readAtom(){
            skipWs();
            if(pos<s.size() && (isdigit(s[pos]) || (s[pos]=='-' && pos+1<s.size() && isdigit(s[pos+1])))){
                size_t start=pos; if(s[pos]=='-') pos++;
                while(pos<s.size() && isdigit(s[pos])) pos++;
                return s.substr(start,pos-start);
            }
            if(pos<s.size() && (isalpha(s[pos]) || s[pos]=='_')){
                size_t start=pos;
                while(pos<s.size() && (isalnum(s[pos])||s[pos]=='_')) pos++;
                string name=s.substr(start,pos-start);
                // normalize to lowercase to match stored keys
                transform(name.begin(),name.end(),name.begin(),::tolower);
                auto it=syms.find(name);
                if(it==syms.end()) return "0";               // undefined → 0
                return it->second.empty() ? "1" : it->second; // boolean flag → 1
            }
            return "0";
        }

        bool parseOr(){
            bool lhs=parseAnd();
            while(true){ skipWs();
                if(pos+1<s.size() && s[pos]=='|' && s[pos+1]=='|'){ pos+=2; lhs=lhs||parseAnd(); }
                else break; }
            return lhs;
        }
        bool parseAnd(){
            bool lhs=parseNot();
            while(true){ skipWs();
                if(pos+1<s.size() && s[pos]=='&' && s[pos+1]=='&'){ pos+=2; lhs=lhs&&parseNot(); }
                else break; }
            return lhs;
        }
        bool parseNot(){
            skipWs();
            if(pos<s.size() && s[pos]=='!'){ pos++; return !parseNot(); }
            return parseComparison();
        }
        bool parseComparison(){
            skipWs();
            // parenthesised sub-expression
            if(pos<s.size() && s[pos]=='('){
                pos++;
                bool v=parseOr();
                skipWs();
                if(pos<s.size() && s[pos]==')') pos++;
                return v;
            }
            string lhsVal=readAtom();
            skipWs();
            // look for a comparison operator (two-char first, then one-char)
            string op;
            if(pos+1<s.size()){
                string two=s.substr(pos,2);
                if(two=="==" || two=="!=" || two=="<=" || two==">="){ op=two; pos+=2; }
            }
            if(op.empty() && pos<s.size() && (s[pos]=='<' || s[pos]=='>')){ op=string(1,s[pos]); pos++; }
            if(op.empty()){
                // no comparison — treat atom as boolean (non-empty and non-"0")
                return !lhsVal.empty() && lhsVal!="0";
            }
            skipWs();
            string rhsVal=readAtom();
            // try numeric comparison
            try{
                int lhs=stoi(lhsVal), rhs=stoi(rhsVal);
                if(op=="==") return lhs==rhs;
                if(op=="!=") return lhs!=rhs;
                if(op=="<")  return lhs<rhs;
                if(op==">")  return lhs>rhs;
                if(op=="<=") return lhs<=rhs;
                if(op==">=") return lhs>=rhs;
            } catch(...){
                // string fallback for == and !=
                if(op=="==") return lhsVal==rhsVal;
                if(op=="!=") return lhsVal!=rhsVal;
            }
            return false;
        }
        bool parseExpr(){ return parseOr(); }
    };
    Eval e{expr, 0, definedSymbols};
    return e.parseExpr();
}

// Skip tokens until ##else / ##endif at the current nesting depth (for ## compile-time conditionals).
void bglParser::skipBglConditionalBlock(abstractObject& ctx){
    int depth = 1;
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside ##ifdef block."); return; }
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("##ifdef") || t.is("##ifndef")) { depth++; continue; }
        if(t.is("##endif")) { depth--; if(depth==0) return; continue; }
        if(depth==1 && t.is("##else")){
            processDirective(t, ctx);
            return;
        }
    }
}

// Skip tokens in a false #if/#elif branch until #elif (true) / #else / #endif at the current
// nesting depth. On #elif-true or #else, resume normal parsing. On #endif, done. Handles nested
// #if correctly via depth counting. Records the skipped source-line range via
// recordInactiveRange() for LSP inactive-region reporting.
void bglParser::skipConditionalBlock(abstractObject& ctx){
    int depth = 1;
    int startLine1 = file.currentLocation().line;
    while(true){
        token t = file.getToken();
        if(t.is(eTokenType::eof)) {
            recordInactiveRange(startLine1, file.currentLocation().line);
            parsingError("Unexpected end of file inside #if block.");
            return;
        }
        if(!t.is(eTokenType::directive)) continue;
        if(t.is("#if"))    { depth++; continue; }
        if(t.is("#endif")) {
            depth--;
            if(depth == 0) { recordInactiveRange(startLine1, t.src.line - 1); return; }
            continue;
        }
        if(depth == 1 && t.is("#elif")){
            // Evaluate this #elif's condition; on true, record and resume. On false, continue
            // skipping — extend the current dead range through the next terminator.
            int elifLine = t.src.line;
            string condText;
            token ct = file.getBasicToken(true);
            while(ct.isNot("\n") && ct.isNot(eTokenType::eof)){ condText += ct.value; ct = file.getBasicToken(true); }
            if(evaluateCondition(condText)) {
                recordInactiveRange(startLine1, elifLine - 1);
                return;
            }
            // continue skipping — the next terminator determines the range end
        }
        if(depth == 1 && t.is("#else")){
            recordInactiveRange(startLine1, t.src.line - 1);
            return;
        }
    }
}

bool bglParser::processDirective(token directive, abstractObject& contextObj){

    token tok;
    switch(directive.chk()){
        case chk("#include"):{
            token next = file.getToken();
            // Optional include: #include ?"file" or #include ?<file> — skip silently if not found
            bool isOptional = false;
            if(next.is("?")){
                isOptional = true;
                next = file.getToken();
            }
            if(next.isString()){
                // Quoted form: search source dir + bglIncludePaths for a .bgl file
                string includeName = next.value;
                if(includeName.size() >= 2 && includeName.front()=='"' && includeName.back()=='"')
                    includeName = includeName.substr(1, includeName.size()-2);
                filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
                string resolved = resolveIncludePath(includeName, ".bgl", curDir, beguilerSettings.includePaths);
                if(!resolved.empty())
                    parseFile(resolved);
                else if(!isOptional)
                    parsingError(format("#include: file '{0}' not found", includeName));
            } else if(next.is("<")){
                // Angle-bracket form: search lib path for sub-paths like <bindings/i6StandardLibrary>
                string includeName;
                token t = file.getToken();
                while(!t.is(">") && !t.is(eTokenType::eof)){
                    includeName += t.originalValue.empty() ? t.value : t.originalValue;
                    t = file.getToken();
                }
                filesystem::path libPath = findCaseInsensitive(settings.libPath, includeName + ".bgl");
                if(filesystem::exists(libPath)){
                    parseFile(libPath.string());
                    if(includeName == "array") languageService.linqInUse = true;
                }
                else if(!isOptional)
                    parsingError(format("#include: file '<{0}>' not found", includeName));
            }
            return false;
            break;
        }
        case chk("#once"):{
            // Register the current file so that any future #include of it is silently skipped.
            string curFile = filesystem::canonical(filesystem::absolute(file.currentLocation().file)).string();
            onceFiles.insert(curFile);
            return false;
            break;
        }
        case chk("#startup"):{
            // Collect the raw I6 body for emission inside bglInit().
            // Deduplicated per source file so re-including a file doesn't register its blocks twice.
            string curFile = filesystem::absolute(file.currentLocation().file).string();
            file.getToken(token::braceOpen);
            string body = file.getRawTextThroughClosingBrace();
            if(!startupFiles.count(curFile)){
                startupFiles.insert(curFile);
                languageService.startupBlocks.push_back(body);
            }
            return false;
            break;
        }
        case chk("#using"):{
            // Import a class or object's members into the current file's scope.
            //   #using Class        — classes' members become visible
            //   #using object       — object's own members become visible (may include aliases)
            //   #using a.b.c        — walk the dot-path, resolving alias members along the way,
            //                         import the final class or object's members
            token first = file.getToken();
            string displayPath = first.originalValue.empty() ? first.value : first.originalValue;
            string curName = first.value;  // lowercased
            classDef*  curCls = dynamic_cast<classDef*>(&languageService.getType(curName));
            objectDef* curObj = nullptr;
            if(!curCls) {
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == curName){ curObj = od; break; }
            }
            if(!curCls && !curObj){
                parsingWarning(format("#using '{0}': not a declared class or object; directive ignored", displayPath));
                return false;
            }
            // Walk dotted path: at each `.member`, look up member on current scope and redirect
            while(file.peekToken().is(token::period)){
                file.getToken(); // consume '.'
                token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
                string memberName = memberTok.value;
                displayPath += "." + (memberTok.originalValue.empty() ? memberTok.value : memberTok.originalValue);
                // Find the member in the current scope
                typeMember* found = nullptr;
                if(curCls) for(typeMember* m : curCls->members) if(m->name == memberName){ found = m; break; }
                if(!found && curObj) for(typeMember* m : curObj->members) if(m->name == memberName){ found = m; break; }
                if(!found){
                    parsingWarning(format("#using '{0}': member not found; directive ignored", displayPath));
                    return false;
                }
                // Only variable declarations (type references, including alias members) can be traversed
                auto* vd = dynamic_cast<variableDeclaration*>(found);
                if(!vd){
                    parsingWarning(format("#using '{0}': '{1}' is not a type reference; directive ignored", displayPath, memberName));
                    return false;
                }
                // Follow the member — could resolve to a class (alias target) or an object.
                // For auto members on non-emitter objects, the declared type may be "object"
                // (base class) rather than the specific target. Use the initializer expression
                // name to find the actual target first, then fall back to the type name.
                string nextType = vd->type.name;
                string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                classDef*  nextCls = nullptr;
                objectDef* nextObj = nullptr;
                // Try initializer name first (most specific)
                if(!initName.empty()){
                    nextCls = dynamic_cast<classDef*>(&languageService.getType(initName));
                    if(!nextCls)
                        for(typeDef* g : languageService.globals)
                            if(auto* od = dynamic_cast<objectDef*>(g))
                                if(od->name == initName){ nextObj = od; break; }
                }
                // Fall back to declared type name
                if(!nextCls && !nextObj){
                    nextCls = dynamic_cast<classDef*>(&languageService.getType(nextType));
                    if(!nextCls)
                        for(typeDef* g : languageService.globals)
                            if(auto* od = dynamic_cast<objectDef*>(g))
                                if(od->name == nextType){ nextObj = od; break; }
                }
                if(!nextCls && !nextObj){
                    parsingWarning(format("#using '{0}': '{1}' has type '{2}' which is not importable; directive ignored",
                        displayPath, memberName, nextType));
                    return false;
                }
                curCls = nextCls;
                curObj = nextObj;
            }
            // Import the final scope
            if(curCls) usingImports.push_back(curCls);
            else if(curObj) usingObjectImports.push_back(curObj);
            return false;
        }
        case chk("#emitfirst"):{
            // Additive: every #emitfirst block contributes. Re-include cycles are protected
            // by `#once` (each BLR file that uses #emitfirst declares #once at the top), so
            // we don't dedup at this level. Multiple #emitfirst blocks in the same file —
            // common for .inf-mode with many #bgl islands — all register.
            file.getToken(token::braceOpen);
            languageService.emitFirstBlocks.push_back(file.getRawTextThroughClosingBrace());
            return false;
            break;
        }
        case chk("#emitlast"):{
            // Same additive policy as #emitfirst.
            file.getToken(token::braceOpen);
            languageService.emitLastBlocks.push_back(file.getRawTextThroughClosingBrace());
            return false;
            break;
        }
        case chk("#includei6"):{
            // Optional: #includeI6 ?"file" — skip silently if not found
            bool i6Optional = false;
            if(file.peekToken().is("?")){
                file.getToken(); // consume '?'
                i6Optional = true;
            }
            token filename = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string innerPath = filename.value;
            if(innerPath.size() >= 2 && innerPath.front()=='"' && innerPath.back()=='"')
                innerPath = innerPath.substr(1, innerPath.size()-2);
            // Resolve the file: search source dir + bglIncludePaths + i6IncludePaths (fallback)
            filesystem::path curDir = filesystem::path(file.currentLocation().file).parent_path();
            string resolved = resolveIncludePath(innerPath, "", curDir, beguilerSettings.includePaths);
            if(resolved.empty()){
                // Also try with .h extension
                resolved = resolveIncludePath(innerPath, ".h", curDir, beguilerSettings.includePaths);
            }
            if(resolved.empty()){
                if(!i6Optional)
                    parsingError(format("#includeI6: file '{0}' not found", innerPath));
                return false;
            }
            // Emit with the resolved absolute path
            string emitPath = "\"" + rewritePathSeps(resolved) + "\"";
            string nodeText = format("#include {0};", emitPath);
            // Claim the pre-scan stub if present (preserves source ordering in output)
            bool claimed = false;
            for(typeDef* g : languageService.globals){
                if(auto* raw = dynamic_cast<i6RawNode*>(g)){
                    if(raw->isPrePassStub && raw->text.find("#include") == 0){
                        // Match by the original filename since the stub was registered with the unresolved path
                        string stubPath = raw->text;
                        size_t q1 = stubPath.find('"'), q2 = stubPath.rfind('"');
                        if(q1 != string::npos && q2 > q1){
                            string stubFile = stubPath.substr(q1+1, q2-q1-1);
                            // Match if the stub filename ends with the original include name
                            if(stubFile.find(rewritePathSeps(innerPath)) != string::npos){
                                raw->text = nodeText;
                                raw->isPrePassStub = false;
                                claimed = true;
                                break;
                            }
                        }
                    }
                }
            }
            if(!claimed){
                i6RawNode& node = *(new i6RawNode());
                node.text = nodeText;
                languageService.globals.push_back(&node);
            }
            return false;
        }
        case chk("#i6"):{
            functionDef* func = dynamic_cast<functionDef*>(&contextObj);
            statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;
            // Helper: install an i6RawNode at the current context — into the function body
            // when one exists, otherwise into globals (claiming the pre-scan placeholder so
            // source order is preserved).
            auto installI6Node = [&](i6RawNode* node){
                if(body != nullptr){
                    body->statements.push_back(node);
                } else {
                    // Replace the pre-scan placeholder slot with the new node so all fields
                    // (including composite `parts`) transfer naturally.
                    bool claimed = false;
                    for(size_t i = 0; i < languageService.globals.size(); i++){
                        if(auto* raw = dynamic_cast<i6RawNode*>(languageService.globals[i])){
                            if(raw->isPrePassStub && raw->text == "#i6_placeholder"){
                                languageService.globals[i] = node;
                                claimed = true;
                                break;
                            }
                        }
                    }
                    if(!claimed) languageService.globals.push_back(node);
                }
            };

            token t = file.getToken();
            if(!t.is(token::braceOpen)){
                // Single-line variant: read raw chars to end of line. No #bgl support here —
                // a single-line I6 statement is too small to need it.
                i6RawNode* node = new i6RawNode();
                node->src = file.currentLocation();
                node->text = t.value;
                char c = file.readChar();
                while(c != '\n' && c != EOF){ node->text += c; c = file.readChar(); }
                installI6Node(node);
                return false;
            }

            // Multi-line block: alternate between raw-I6 chunks and embedded #bgl{} regions.
            // Each #bgl{} is parsed as Beguile statements (code-block mode — declarations
            // disallowed) and appended in source order, so the emission preserves the natural
            // interleaving between raw I6 and Beguile.
            //
            // Inside a function body, raw chunks become i6RawNodes and bgl statements push
            // directly into body->statements. At global scope, both are accumulated into a
            // single composite i6RawNode whose `parts` vector preserves the interleaving for
            // emit-time rendering — Beguile statements assume a code-block context, so we
            // synthesize one (lambdaOuterFunc / activeBlockStack) for the duration of the parse.
            i6RawNode* compositeNode = nullptr;
            functionDef* synthFunc = nullptr;
            statementBlock* synthBody = nullptr;
            if(body == nullptr){
                compositeNode = new i6RawNode();
                synthFunc = new functionDef();
                synthFunc->name = "__bgl_inline_block";
                synthBody = new statementBlock();
                synthFunc->body = synthBody;
                openCompileContext(eCompileContext::codeBlock, synthBody);
            }
            string accumulatedRaw;  // accumulating raw text between/around #bgl statements (global scope)
            sourceLocation accumulatedRawSrc;  // src of the FIRST char of accumulatedRaw (preserved across appends)
            int depth = 1;
            while(depth > 0){
                eBglDirective directive = eBglDirective::NotFound;
                // Capture source position before reading the raw segment so the resulting
                // i6RawNode's `src` reflects where this chunk begins in the .bgl file. Used
                // by the emitter to anchor per-source-line entries in the source map, so I6
                // diagnostics inside the raw block remap accurately to the .bgl line.
                sourceLocation segStart = file.currentLocation();
                string segment = file.getRawTextUntilCloseOrBgl(directive, depth, depth);
                if(body != nullptr){
                    if(!segment.empty()){
                        i6RawNode* node = new i6RawNode();
                        node->src = segStart;
                        node->text = segment;
                        installI6Node(node);
                    }
                } else {
                    if(accumulatedRaw.empty()) accumulatedRawSrc = segStart;
                    accumulatedRaw += segment;
                }
                if(directive != eBglDirective::NotFound){
                    // Slice 1: all three directives (#bgl / #bglDecl / #bglStmt) route to the
                    // existing loose-statement parser inside `#i6{}` regions of `.bgl` files.
                    // TODO (later slice): decide whether `#bglDecl` is even valid here, since
                    // file-scope declarations belong outside the surrounding `#i6{}` block.
                    (void)directive;
                    statementBlock* targetBody = body != nullptr ? body : synthBody;
                    // Two forms (matching #i6):
                    //   #bgl{ stmts… }  — multi-line, terminated by matching `}`
                    //   #bgl stmt;…     — single-line, terminated by newline
                    // The single-line form must have its `{` (if any) on the same source line
                    // as `#bgl`; a newline before any non-whitespace puts us into single-line.
                    string bglContent;
                    sourceLocation hereLoc = file.currentLocation();
                    bool isMultiLine = false;
                    {
                        // Skip space/tab on the same line, looking for `{`. Don't cross newlines.
                        while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
                        if(file.peekChar() == '{'){
                            file.readChar();
                            isMultiLine = true;
                        }
                    }
                    if(isMultiLine){
                        // We consumed the `{` ourselves, so balance braceDepth for the
                        // decrement that getRawTextThroughClosingBrace will perform.
                        file.braceDepth++;
                        bglContent = file.getRawTextThroughClosingBrace();
                    } else {
                        // Single-line: read raw chars to end of line. The newline itself is
                        // part of the surrounding I6 stream, so don't consume it here.
                        char c = file.peekChar();
                        while(c != '\n' && c != (char)EOF){
                            file.readChar();
                            bglContent += c;
                            c = file.peekChar();
                        }
                    }
                    // Sub-parse via an in-memory stream so getToken() drives Beguile parsing
                    // over the bgl content with normal line tracking. Skip whitespace and
                    // check for stream EOF before calling into getToken — the lexer treats a
                    // raw EOF inside a non-global compile context as an error, but in our
                    // case end-of-bgl-content is a normal terminator.
                    abstractObject& subContext = (compositeNode != nullptr) ? *(abstractObject*)synthFunc : contextObj;
                    size_t stmtCountBefore = targetBody->statements.size();
                    // Use the original file path verbatim so editor click-to-navigate works
                    // on errors fired during the sub-parse. The virtual stream's startLine
                    // is the source line of `#bgl`, so reported line numbers map back correctly.
                    file.openText(bglContent, hereLoc.file, hereLoc.line);
                    bool savedLoose = looseIdentifierMode;
                    looseIdentifierMode = true;
                    try {
                        while(true){
                            file.bleedSpaces();
                            if(file.peekChar() == (char)EOF) break;
                            token nt = file.getToken();
                            if(nt.is(eTokenType::eof)) break;
                            // processStatementDispatch returns true when it consumes a stray
                            // `}` — shouldn't happen in well-formed bgl content, but guard anyway.
                            if(processStatementDispatch(nt, subContext)) break;
                        }
                    } catch(...) { looseIdentifierMode = savedLoose; file.close(); throw; }
                    looseIdentifierMode = savedLoose;
                    file.close();
                    // Global-scope: move newly-parsed statements into the composite node's
                    // parts list, attaching the accumulated raw text in front of the first.
                    if(compositeNode != nullptr){
                        bool firstStatement = true;
                        for(size_t i = stmtCountBefore; i < targetBody->statements.size(); i++){
                            statement* s = targetBody->statements[i];
                            string lead = firstStatement ? accumulatedRaw : "";
                            sourceLocation leadSrc = firstStatement ? accumulatedRawSrc : sourceLocation{};
                            compositeNode->parts.push_back({lead, s, leadSrc});
                            firstStatement = false;
                        }
                        if(firstStatement){
                            // No statements were parsed — nothing to anchor accumulatedRaw to;
                            // it will be picked up by the next iteration's segment handling.
                        } else {
                            accumulatedRaw.clear();
                            accumulatedRawSrc = {};
                        }
                        // Drop them from synthBody so the next sub-parse starts fresh.
                        targetBody->statements.resize(stmtCountBefore);
                    }
                }
            }
            // Global-scope: install the composite node, with any trailing raw text appended.
            if(compositeNode != nullptr){
                closeCompileContext(eCompileContext::codeBlock);
                if(!accumulatedRaw.empty()){
                    if(compositeNode->parts.empty()){
                        compositeNode->text = accumulatedRaw;
                        compositeNode->src = accumulatedRawSrc;
                    } else {
                        compositeNode->parts.push_back({accumulatedRaw, nullptr, accumulatedRawSrc});
                    }
                }
                installI6Node(compositeNode);
            }
            return false;
            break;
        }
        case chk("#define"):{
            token sym = file.getToken(eTokenType::identifier);
            // optional value on the same line — skip horizontal whitespace only (not newlines)
            while(file.peekChar() == ' ' || file.peekChar() == '\t') file.readChar();
            token val = file.getBasicToken(true);
            string valStr;
            if(val.isNot("\n") && val.isNot(eTokenType::eof)){
                valStr = val.value;
                // consume rest of line
                token rest = file.getBasicToken(true);
                while(rest.isNot("\n") && rest.isNot(eTokenType::eof)) rest = file.getBasicToken(true);
            }
            definedSymbols[sym.value] = valStr;
            return false;
        }
        case chk("#undef"):{
            token sym = file.getToken(eTokenType::identifier);
            definedSymbols.erase(sym.value);
            return false;
        }
        case chk("#if"):{
            // Collect condition text up to end of line, optional `;` no-op, EOF, or the
            // start of the next #-directive. Single-line forms like `#if cond; …; #endif`
            // and `#if cond #includeI6 … #endif` both parse correctly. When stopping on
            // a #-directive, hand it off to processDirective recursively.
            string condText;
            token t = file.getBasicToken(true);
            bool stoppedOnDirective = false;
            while(t.isNot("\n") && t.isNot(";") && t.isNot(eTokenType::eof)){
                if(!t.value.empty() && t.value[0] == '#'){ stoppedOnDirective = true; break; }
                condText += t.value;
                t = file.getBasicToken(true);
            }
            if(!evaluateCondition(condText))
                skipConditionalBlock(contextObj);
            else if(stoppedOnDirective)
                processDirective(t, contextObj);
            return false;
        }
        case chk("#elif"):{
            // reached here only when a prior #if branch was TRUE — skip to #endif
            int startLine1 = directive.src.line + 1;  // first line after the #elif directive
            int depth = 1;
            int endLine1 = startLine1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #elif block."); return false; }
                if(t.is("#if"))    depth++;
                if(t.is("#endif")) { depth--; if(depth == 0) endLine1 = t.src.line - 1; }
            }
            recordInactiveRange(startLine1, endLine1);
            return false;
        }
        case chk("#else"):{
            // reached here only when a prior #if/#elif branch was TRUE — skip to #endif
            int startLine1 = directive.src.line + 1;  // first line after the #else directive
            int depth = 1;
            int endLine1 = startLine1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside #else block."); return false; }
                if(t.is("#if"))    depth++;
                if(t.is("#endif")) { depth--; if(depth == 0) endLine1 = t.src.line - 1; }
            }
            recordInactiveRange(startLine1, endLine1);
            return false;
        }
        case chk("#endif"):{
            // consumed as a no-op (skipConditionalBlock handles it, but a stray #endif is harmless)
            return false;
        }

        // ## compile-time conditionals — evaluated by the Beguile transpiler; emit nothing when false
        case chk("##ifdef"):{
            token sym = file.getToken(eTokenType::identifier);
            if(definedSymbols.find(sym.value) == definedSymbols.end())
                skipBglConditionalBlock(contextObj);
            return false;
        }
        case chk("##ifndef"):{
            token sym = file.getToken(eTokenType::identifier);
            if(definedSymbols.find(sym.value) != definedSymbols.end())
                skipBglConditionalBlock(contextObj);
            return false;
        }
        case chk("##else"):{
            // reached only when the preceding ##ifdef/##ifndef branch was TRUE — skip to ##endif
            int depth = 1;
            while(depth > 0){
                token t = file.getToken();
                if(t.is(eTokenType::eof)) { parsingError("Unexpected end of file inside ##else block."); return false; }
                if(t.is("##ifdef") || t.is("##ifndef")) depth++;
                if(t.is("##endif")) depth--;
            }
            return false;
        }
        case chk("##endif"):{
            // consumed as a no-op (skipBglConditionalBlock handles it)
            return false;
        }

        case chk("#beguilersettings"):{
            return processBeguilerSettings();
        }
        case chk("#message"):{
            token msg = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string text = msg.value;
            if(text.size()>=2 && text.front()=='"' && text.back()=='"') text = text.substr(1,text.size()-2);
            cout << text << endl;
            return false;
        }
        case chk("#error"):{
            token msg = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string text = msg.value;
            if(text.size()>=2 && text.front()=='"' && text.back()=='"') text = text.substr(1,text.size()-2);
            return parsingError(text);
        }
        case chk("#warning"):{
            token msg = file.getToken({eTokenType::quote, eTokenType::rawQuote});
            string text = msg.value;
            if(text.size()>=2 && text.front()=='"' && text.back()=='"') text = text.substr(1,text.size()-2);
            parsingWarning(text);
            return false;
        }
        case chk("#exit"):{
            throw exitFileSignal{};
        }
    }
    // ##VerbName is an I6 action-constant syntax that Beguile handles automatically.
    if(directive.value.rfind("##", 0) == 0){
        string verbName = directive.value.substr(2);
        return parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly — the '##' prefix is emitted automatically by the verb type's operator ==.", verbName));
    }
    return parsingError("Unrecognized directive '" + directive.value + "'.");
}
bool bglParser::processRoutineDeclaration(token returnType, token name, abstractObject& contextObject, bool isExternal, bool isEmitter, bool isReplace){
    functionDef& funcDef=*(new functionDef());
    funcDef.name=(string) name; funcDef.displayName=name.originalValue;
    // Use the name token's source location (preserved from when it was read) — currentLocation()
    // would return where we are NOW (after consuming the name), which may be on a later line.
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType=languageService.getType((string) returnType);
    funcDef.isExternal=isExternal;
    funcDef.isEmitter=isEmitter;
    if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
    else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
    // Non-emitter operator routines (name starts with non-identifier char) need a mangled
    // i6name so the emitted I6 routine has a valid property identifier. Call sites use the
    // same mangler to look it up. Emitters are inlined and don't need the mangled name.
    if(!isEmitter && !funcDef.name.empty() && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_')
        funcDef.i6name = mangleOperatorName(funcDef.name);

    // Register into globals EARLY (replacing any pre-scan stub) so that LSP error recovery
    // preserves partial parse state even if the body parse throws. The full body and params
    // are populated via references into this same object below.
    //
    // In .inf-mode, Pass 1 walks every #bgl island before Pass 2 starts, so all function
    // stubs cluster at the front of `globals` regardless of where their islands sit in the
    // .inf body. Replacing the stub in place would leave the funcDef there too — emitting
    // it ahead of compositeNodes for raw I6 (and `#includeI6` directives) that Pass 2
    // push_backs later. WadeWar3 hit this: `[initialise; ... location = ...]` was emitted
    // before `#include "...#parser.h"` (which declares `location`), and I6 rejected the
    // assignment. So in .inf-mode we erase the stub and push_back the real funcDef,
    // landing it at the current Pass-2 position. Other stubs remain visible for
    // forward-reference resolution during this function's body parse.
    //
    // In .bgl-mode, Pass 1 walks the source in order, so prescan stubs are already at
    // their source-order positions — claim in place preserves source order naturally.
    bool registeredEarly = false;
    if(languageService.isInfMode){
        for(auto it = languageService.globals.begin(); it != languageService.globals.end(); ++it){
            if(auto* stub = dynamic_cast<functionDef*>(*it)){
                if(stub->name == funcDef.name && stub->isPrePassStub){
                    languageService.globals.erase(it);
                    break;
                }
            }
        }
        languageService.globals.push_back(&funcDef);
        registeredEarly = true;
    } else {
        for(typeDef*& g : languageService.globals){
            if(auto* stub = dynamic_cast<functionDef*>(g)){
                if(stub->name == funcDef.name && stub->isPrePassStub){
                    g = &funcDef;
                    registeredEarly = true;
                    break;
                }
            }
        }
        if(!registeredEarly){
            languageService.globals.push_back(&funcDef);
            registeredEarly = true;
        }
    }

    processParameterList(funcDef);

    // ── replace chaining (non-emitter): rename existing before body parse ──
    // For non-emitter replace, we rename the existing function to a mangled name
    // BEFORE parsing the body so that replaced() calls can resolve during parsing.
    // Emitter replace keeps the simpler body-swap path (no chaining).
    if(isReplace && !isEmitter){
        functionDef* existing = nullptr;
        for(typeDef* g : languageService.globals){
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                if(fd->name != funcDef.name) continue;
                if(fd->isPrePassStub) continue; // pre-scan stub doesn't count as a real definition
                existing = fd;
                break;
            }
        }
        if(existing){
            // Determine N for mangled name: count existing _bgl_replaced_NAME_* in globals
            int n = 0;
            string prefix = "_bgl_replaced_" + funcDef.name + "_";
            for(typeDef* g : languageService.globals)
                if(auto* fd = dynamic_cast<functionDef*>(g))
                    if(fd->name.rfind(prefix, 0) == 0) n++;
            string mangledName = prefix + to_string(n);
            existing->name = mangledName;
            funcDef.replacedTarget = mangledName;
            funcDef.replacedFunc = existing;
        } else {
            parsingWarning(format("replace: no existing global function '{0}' found; treating as new definition", funcDef.name));
        }
    }

    if(isEmitter && file.peekToken().is(token::endStatement)){
        // Semicolon-terminated emitter: pass-through (value unchanged)
        file.getToken(); // consume ';'
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=" $self";
        funcDef.body=&rawblock;
    } else if(isEmitter){
        file.getToken(token::braceOpen);
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=file.getRawTextThroughClosingBrace();
        funcDef.body=&rawblock;
    } else {
        file.getToken(token::braceOpen);
        funcDef.body=new statementBlock();
        functionDef* savedFunc = currentFunc;
        currentFunc = &funcDef;
        openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
        while(processNextStatement(funcDef)==false){
        }
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
            parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    }

    // ── replace: emitter body-swap (existing behavior) ──
    if(isReplace && isEmitter){
        for(typeDef* g : languageService.globals){
            if(auto* existing = dynamic_cast<functionDef*>(g)){
                if(existing->name != funcDef.name) continue;
                if(existing->params.size() != funcDef.params.size()) continue;
                bool paramsMatch = true;
                for(size_t i = 0; i < funcDef.params.size() && paramsMatch; i++)
                    if(funcDef.params[i]->type.name != existing->params[i]->type.name)
                        paramsMatch = false;
                if(!paramsMatch) continue;
                existing->body = funcDef.body;
                existing->isEmitter = funcDef.isEmitter;
                existing->returnType = funcDef.returnType;
                return false;
            }
        }
        parsingWarning(format("replace: no existing emitter '{0}' found with matching signature; treating as new definition", funcDef.name));
    }

    // ── replace chaining: dead code elimination ──
    // If replaced() was never called in the body, the predecessor (and its entire
    // backward chain) will never execute — mark them dead so the emitter skips them.
    if(isReplace && !isEmitter && funcDef.replacedFunc && !funcDef.replacedWasCalled){
        functionDef* f = funcDef.replacedFunc;
        while(f){
            f->isReplacedDead = true;
            f = f->replacedFunc;
        }
    }

    // funcDef was already registered in globals above (before body parse) so LSP error
    // recovery preserves the partial definition. Nothing more to do here.
    return false;
}

void bglParser::parsePropertyValue(variableDeclaration& prop, string typeName){
    token first = file.getToken();
    if(first.is(token::braceOpen)){
        initializerList* list = new initializerList();
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
            if(t.is(token::braceOpen)){
                // nested initializer list: { {a, b}, {c, d} }
                initializerList* inner = new initializerList();
                token t2 = file.getToken();
                while(!t2.is(token::braceClose) && !t2.is(eTokenType::eof)){
                    expression* elem = parseExpression(t2, {",", token::braceClose}, nullptr, nullptr);
                    inner->elements.push_back(elem);
                    if(elem->terminator == token::braceClose) break;
                    t2 = file.getToken();
                }
                list->elements.push_back(inner);
                // after inner }, expect , or outer }
                t = file.getToken({token::comma, token::braceClose});
                if(t.is(token::braceClose)) break;
                t = file.getToken();
                continue;
            }
            expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
            list->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        string expectedElemType;
        // For array<T>, extract T as the expected element type
        if(typeName.size() > 6 && typeName.substr(0, 6) == "array<" && typeName.back() == '>')
            expectedElemType = typeName.substr(6, typeName.size() - 7);
        // Fallback: infer from the class's single-param method
        if(expectedElemType.empty()){
            classDef* listClass = dynamic_cast<classDef*>(&languageService.getType(typeName));
            if(listClass != nullptr)
                for(typeMember* m : listClass->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
        }
        for(size_t i = 0; i < list->elements.size(); i++){
            expression* elem = list->elements[i];
            if(elem->resolvedType.empty())
                parsingError(format("Undeclared identifier in initializer list (element {0})", i));
            else if(!expectedElemType.empty() && !isTypeCompatible(elem->resolvedType, expectedElemType))
                parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
        }
        prop.declaredExpressionValue = list;
    } else {
        expression* expr = parseExpression(first, {token::endStatement}, nullptr, nullptr);
        // auto inference: if the property type is 'auto', adopt the expression's resolved type
        if(typeName == "auto" && !expr->resolvedType.empty()){
            typeName = expr->resolvedType;
            prop.type = languageService.getType(typeName);
        } else if(!typeName.empty() && typeName != "var" && !expr->resolvedType.empty()
           && !isTypeCompatible(expr->resolvedType, typeName))
            parsingError(format("Cannot assign value of type '{0}' to property '{1}' of type '{2}'",
                typeDisplayName(expr->resolvedType), prop.dName(), typeDisplayName(typeName)));
        prop.declaredExpressionValue = expr;
    }
}

void bglParser::processI6InlineMember(objectDef& obj){
    token t = file.getToken();
    i6RawNode& node = *(new i6RawNode());
    if(t.is(token::braceOpen)){
        deque<char> closeChar;
        closeChar.push_back('}');
        t = file.getBasicToken(true);
        while(closeChar.size() > 0){
            if(t.value[0] == closeChar.back()){
                closeChar.pop_back();
                if(closeChar.size() == 0) break;
            }
            node.text = node.text + t.value;
            if(closeChar.back() != '\n'){
                if(t.value[0] == '{') closeChar.push_back('}');
                if(t.value[0] == '!') closeChar.push_back('\n');
            }
            t = file.getBasicToken(true);
        }
    } else {
        while(t.isNot("\n")){
            node.text = node.text + t.value;
            t = file.getBasicToken(true);
        }
    }
    obj.members.push_back((typeMember*)&node);
}

bool bglParser::processArrayMember(vector<typeMember*>& members, const string& ownerDName, verbObjectDef* vodForGrammarRules,
                                   abstractObject* ctx, Qualifiers* q){
    file.getToken("<");
    string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
    file.getToken(">");
    token propName = file.getToken(eTokenType::identifier);

    // Routine form: `array<T> name(...)` — class/object member function returning a
    // generic array type (e.g. LINQ chain primitives like
    //   `array<var> filter(array<var> src, func<bool, var> pred) { ... }`).
    // We inline the routine-creation flow here instead of calling processRoutineDeclaration,
    // because that path registers into languageService.globals and we need member-vector
    // registration. This mirrors the relevant slice of processMemberMethod / class-body
    // routine creation, scoped to the array<T> return-type case.
    if(ctx && q && file.peekToken().is(token::parenOpen)){
        file.getToken(token::parenOpen);
        functionDef& funcDef = *(new functionDef());
        funcDef.name = (string)propName;
        funcDef.displayName = propName.originalValue;
        funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
        funcDef.returnType.name = "array<" + elemType + ">";
        funcDef.isEmitter  = q->isEmitter;
        funcDef.isExternal = q->isExtern;

        processParameterList(funcDef);

        if(funcDef.isEmitter){
            file.getToken(token::braceOpen);
            i6Block& rawblock = *(new i6Block());
            rawblock.i6Body = file.getRawTextThroughClosingBrace();
            funcDef.body = &rawblock;
        } else {
            file.getToken(token::braceOpen);
            funcDef.body = new statementBlock();
            functionDef* savedFunc = currentFunc;
            currentFunc = &funcDef;
            openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
            while(processNextStatement(funcDef) == false){}
            closeCompileContext(eCompileContext::codeBlock);
            currentFunc = savedFunc;
            if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
                parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
        }

        if(!replaceStubMember(members, funcDef))
            members.push_back(&funcDef);
        return true;
    }

    // array<grammarRule>: parse {{Verb, {pattern}}, ...} initializer
    if(elemType == "grammarrule"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        token inner = file.getToken();
        while(inner.is(token::braceOpen)){
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = propName.value;
            rd.type = languageService.getType("grammarrule");
            // Parse verb reference
            token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            string lower = verbTok.value;
            transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            bool verbFound = false;
            for(verbObjectDef* v : languageService.verbs)
                if(v->name == lower){ verbFound = true; break; }
            if(!verbFound){
                string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
                parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
            }
            rd.targetVerb = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
            file.getToken(token::comma);
            file.getToken(token::braceOpen);
            rd.line = parseGrammarLineContent();
            // Optional `, priority` third element (default 10 from BLR `class verb`).
            rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
            rd.line.isOwnLine  = false;
            file.getToken(token::braceClose);  // closing } of this rule
            // Add to verb's grammarLines if applicable
            if(vodForGrammarRules){
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                vodForGrammarRules->grammarLines.push_back(gl);
            }
            members.push_back(&rd);
            token sep = file.getToken({token::comma, token::braceClose});
            if(sep.is(token::braceClose)) break;
            inner = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        return false;
    }

    token sym = file.getToken({token::bracketOpen, token::assignment, token::endStatement});
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    arrDecl.name = (string)propName;
    arrDecl.type = languageService.getType("array");
    arrDecl.elementType = elemType;
    if(sym.is(token::bracketOpen)){
        token sizeTok = file.getToken(eTokenType::integer);
        arrDecl.arraySize = stoi(sizeTok.value);
        file.getToken(token::bracketClose);
        file.getToken(token::endStatement);
    } else if(sym.is(token::assignment)){
        // Check for string initializer: array<char> name = "text";
        token peek = file.peekToken(1);
        if(peek.is(eTokenType::quote) || peek.is(eTokenType::rawQuote)){
            if(elemType != "char" && elemType != "charliteral")
                parsingError("String initializer is only valid for array<char>");
            token strTok = file.getToken();
            arrDecl.stringInitializer = strTok.value;
            arrDecl.isByteArray = true;
            arrDecl.type = languageService.getType("bytearray");
            if(file.peekToken().is(token::endStatement)) file.getToken();
        } else {
            file.getToken(token::braceOpen);
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            if(file.peekToken().is(token::endStatement)) file.getToken();
            arrDecl.declaredExpressionValue = list;
        }
    }
    // Set byte array type for char arrays
    if(elemType == "char"){
        arrDecl.isByteArray = true;
        arrDecl.type = languageService.getType("bytearray");
    }
    for(typeMember* m : members)
        if(m->name == arrDecl.name)
            parsingError(format("'{0}': member '{1}' is already defined", ownerDName, arrDecl.dName()));
    members.push_back((typeMember*)&arrDecl);
    return false;
}

void bglParser::processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace){
    functionDef& funcDef = *(new functionDef());
    funcDef.name = (string)name;
    funcDef.displayName = name.originalValue;
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType = languageService.getType((string)returnType);
    processParameterList(funcDef);
    file.getToken(token::braceOpen);
    funcDef.body = new statementBlock();
    functionDef* savedFunc = currentFunc;
    currentFunc = &funcDef;
    openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
    while(processNextStatement(funcDef) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    currentFunc = savedFunc;
    if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    // Check class hierarchy for shadowed methods — warn if 'replace' not specified
    if(!isReplace && !funcDef.name.empty()){
        string shadowedFrom;
        bool shadowedIsDefault = false;
        function<void(classDef*)> searchClass = [&](classDef* c){
            if(!shadowedFrom.empty() || !c) return;
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == funcDef.name){ shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault; return; }
            for(classDef* base : c->baseClasses) searchClass(base);
        };
        // Search objectClass hierarchy
        if(obj.objectClass) searchClass(obj.objectClass);
        // Search objectDef base classes (from ': Parent' clause)
        for(typeDef* base : obj.baseClasses)
            if(auto* cd = dynamic_cast<classDef*>(base)) searchClass(cd);
        if(!shadowedFrom.empty() && !shadowedIsDefault)
            parsingWarning(format("object '{0}': method '{1}' shadows definition in class '{2}'; use 'replace' to suppress this warning",
                obj.dName(), funcDef.dName(), shadowedFrom));
    }
    if(!replaceStubMember(obj.members, funcDef)){
        // No stub — check for duplicates
        bool replaced = false;
        if(isReplace){
            for(size_t i = 0; i < obj.members.size(); i++)
                if(auto* fd = dynamic_cast<functionDef*>(obj.members[i]))
                    if(fd->name == funcDef.name && fd->params.size() == funcDef.params.size()){
                        obj.members[i] = &funcDef;
                        replaced = true;
                        break;
                    }
            if(!replaced)
                parsingWarning(format("object '{0}': 'replace' specified but no existing method '{1}' to replace", obj.dName(), funcDef.dName()));
        }
        if(!replaced){
            for(typeMember* m : obj.members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == funcDef.name && fd->params.size() == funcDef.params.size())
                        parsingError(format("object '{0}': method '{1}' with the same signature is already defined (originally at {2}:{3}); use 'replace' to override",
                            obj.dName(), funcDef.dName(), fd->src.file, fd->src.line));
        }
        if(!replaced)
            obj.members.push_back((typeMember*)&funcDef);
    }
}

void bglParser::processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue, bool isReplace){
    variableDeclaration& prop = *(new variableDeclaration());
    prop.name = name;
    prop.src = file.currentLocation();
    prop.type = languageService.getType(typeName);
    if(hasValue) parsePropertyValue(prop, typeName);
    bool replaced = false;
    if(isReplace){
        for(size_t i = 0; i < obj.members.size(); i++)
            if(auto* vd = dynamic_cast<variableDeclaration*>(obj.members[i]))
                if(vd->name == prop.name){ obj.members[i] = &prop; replaced = true; break; }
        if(!replaced)
            parsingWarning(format("object '{0}': 'replace' specified but no existing member '{1}' to replace", obj.dName(), prop.dName()));
    }
    if(!replaced){
        for(typeMember* m : obj.members)
            if(m->name == prop.name)
                parsingError(format("object '{0}': member '{1}' is already defined", obj.dName(), prop.dName()));
        obj.members.push_back((typeMember*)&prop);
    }
}

void bglParser::processTypedMember(objectDef& obj, token typeTok, bool isReplace){
    token propName = file.getToken(eTokenType::identifier);
    if(propName.is("operator")){
        token opTok = file.getToken();
        if(opTok.is(token::parenOpen))
            propName.value = "operator()";
        else {
            opTok.assert(eTokenType::oper);
            propName.value = "operator " + opTok.value;
        }
    }
    // grammarRule member: parse {Verb, {pattern}} or array<grammarRule> {{Verb, {pattern}}, ...}
    if(typeTok.value == "grammarrule"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        token peek = file.peekToken();
        if(peek.is(token::braceOpen)){
            // array form: single grammarRule can't have nested braces
            parsingError(format("grammarRule '{0}' takes a single rule initializer; use array<grammarRule> for multiple rules", propName.value));
        }
        // Single rule: {VerbRef, {.word, TOKEN, ...}}
        grammarRuleDecl& rd = *(new grammarRuleDecl());
        rd.name = propName.value;
        rd.type = languageService.getType("grammarrule");
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        // Resolve verb reference
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool verbFound = false;
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower){ verbFound = true; break; }
        if(!verbFound){
            string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
            parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        }
        rd.targetVerb = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        file.getToken(token::comma);
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional `, priority` third element (default 10 from BLR `class verb`).
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        file.getToken(token::braceClose);  // closing } of outer initializer
        if(file.peekToken().is(token::endStatement)) file.getToken();
        // If on a grammarRuleList-bearing object, also add to its grammarLines
        if(auto* vod = dynamic_cast<verbObjectDef*>(&obj)){
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            vod->grammarLines.push_back(gl);
        }
        obj.members.push_back(&rd);
        return;
    }
    // grammarRuleList member: parse grammar lines instead of standard property value
    if(typeTok.value == "grammarrulelist"){
        // Resolve verb context for inferred-verb grammarRule entries
        auto* vod = dynamic_cast<verbObjectDef*>(&obj);
        bool isVerbContext = (vod != nullptr);
        if(!isVerbContext){
            bool isVerbDerived = false;
            function<void(classDef*)> searchBases = [&](classDef* cls){
                if(!cls || isVerbDerived) return;
                if(cls->name == "verb"){ isVerbDerived = true; return; }
                for(classDef* base : cls->baseClasses) searchBases(base);
            };
            if(obj.objectClass) searchBases(obj.objectClass);
            isVerbContext = isVerbDerived;
        }
        string inferredVerb;
        if(vod) inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

        file.getToken(token::assignment);
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = propName.value;
        gtd.type = languageService.getType("grammarrulelist");
        gtd.verbName = inferredVerb;

        // Parse grammar lines and create a grammarRuleDecl for each
        vector<grammarLine> lines = parseGrammarLines();
        for(grammarLine& gl : lines){
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = propName.value;
            rd.type = languageService.getType("grammarrule");
            rd.line = gl;
            rd.targetVerb = inferredVerb;
            if(!isVerbContext)
                parsingWarning(format("grammarRule in '{0}' uses inferred verb form but the owning object is not a verb; use explicit form: grammarRule r = {{Verb, {{pattern}}}}", propName.value));
            gtd.rules.push_back(&rd);
            gl.targetVerb = inferredVerb;
            gtd.grammarLines.push_back(gl);
        }

        if(file.peekToken().is(token::endStatement)) file.getToken();
        if(vod){
            vod->grammarLines.insert(vod->grammarLines.end(), gtd.grammarLines.begin(), gtd.grammarLines.end());
            if(vod->isExternal) languageService.globals.push_back(&gtd);
        }
        obj.members.push_back(&gtd);
        return;
    }
    token sym = file.getToken({token::assignment, token::endStatement, token::parenOpen});
    if(sym.is(token::parenOpen))
        processMemberMethod(obj, typeTok, propName, isReplace);
    else
        processMemberVariable(obj, typeTok.value, propName.value, sym.is(token::assignment), isReplace);
}

void bglParser::processInheritedMember(objectDef& obj, token nameTok){
    string propTypeName;
    // Search the object's declared class first (if any), then fall back to the base 'object' class.
    // For arrayDeclaration members, reconstruct the full templated typeName (e.g. `array<dictionaryWord>`)
    // so parsePropertyValue can extract the element type — otherwise `vd->type.name` returns just `array`
    // and parsePropertyValue's fallback misidentifies the element type as int (from operator[]'s index param).
    std::function<void(classDef*)> searchClass = [&](classDef* cls){
        if(!cls || !propTypeName.empty()) return;
        for(typeMember* m : cls->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == nameTok.value){
                    if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
                        propTypeName = "array<" + arr->elementType + ">";
                    else
                        propTypeName = vd->type.name;
                    return;
                }
        for(classDef* base : cls->baseClasses)
            searchClass(base);
    };
    searchClass(obj.objectClass);
    if(propTypeName.empty()){
        classDef* baseObj = dynamic_cast<classDef*>(&languageService.getType("object"));
        searchClass(baseObj);
    }
    if(propTypeName.empty())
        parsingError(format("'{0}' is not a property defined on the object class or its bases; add a type specifier (e.g. 'object {0} = ...')", nameTok.value));
    // grammarRule/grammarRuleList with inferred type: route to grammar-specific parsing
    if(propTypeName == "grammarrule"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        grammarRuleDecl& rd = *(new grammarRuleDecl());
        rd.name = nameTok.value;
        rd.type = languageService.getType("grammarrule");
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool verbFound = false;
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower){ verbFound = true; break; }
        if(!verbFound){
            string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
            parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        }
        rd.targetVerb = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        file.getToken(token::comma);
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional `, priority` third element (default 10 from BLR `class verb`).
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        file.getToken(token::braceClose);
        if(file.peekToken().is(token::endStatement)) file.getToken();
        if(auto* vod = dynamic_cast<verbObjectDef*>(&obj)){
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            vod->grammarLines.push_back(gl);
        }
        obj.members.push_back(&rd);
        return;
    }
    if(propTypeName == "grammarrulelist"){
        auto* vod = dynamic_cast<verbObjectDef*>(&obj);
        bool isVerbContext = (vod != nullptr);
        if(!isVerbContext){
            bool isVerbDerived = false;
            function<void(classDef*)> searchBases = [&](classDef* cls){
                if(!cls || isVerbDerived) return;
                if(cls->name == "verb"){ isVerbDerived = true; return; }
                for(classDef* base : cls->baseClasses) searchBases(base);
            };
            if(obj.objectClass) searchBases(obj.objectClass);
            isVerbContext = isVerbDerived;
        }
        string inferredVerb;
        if(vod) inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

        file.getToken(token::assignment);
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = nameTok.value;
        gtd.type = languageService.getType("grammarrulelist");
        gtd.verbName = inferredVerb;

        vector<grammarLine> lines = parseGrammarLines();
        for(grammarLine& gl : lines){
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = nameTok.value;
            rd.type = languageService.getType("grammarrule");
            rd.line = gl;
            rd.targetVerb = inferredVerb;
            if(!isVerbContext)
                parsingWarning(format("grammarRule in '{0}' uses inferred verb form but the owning object is not a verb; use explicit form: grammarRule r = {{Verb, {{pattern}}}}", nameTok.value));
            gtd.rules.push_back(&rd);
            gl.targetVerb = inferredVerb;
            gtd.grammarLines.push_back(gl);
        }

        if(file.peekToken().is(token::endStatement)) file.getToken();
        if(vod){
            vod->grammarLines.insert(vod->grammarLines.end(), gtd.grammarLines.begin(), gtd.grammarLines.end());
            if(vod->isExternal) languageService.globals.push_back(&gtd);
        }
        obj.members.push_back(&gtd);
        return;
    }
    bool hasValue = file.getToken({token::assignment, token::endStatement}).is(token::assignment);
    processMemberVariable(obj, propTypeName, nameTok.value, hasValue);
}

bool bglParser::processEmitterValueDeclaration(token typeTok, token nameTok){
    // emitter Type name { body } — no parens, expands inline as a value emitter
    functionDef& funcDef = *(new functionDef());
    funcDef.name = nameTok.value;
    funcDef.displayName = nameTok.originalValue;
    funcDef.returnType = languageService.getType(typeTok.value);
    funcDef.isEmitter = true;
    funcDef.isValueEmitter = true;
    funcDef.src = file.currentLocation();
    i6Block& rawblock = *(new i6Block());
    rawblock.i6Body = file.getRawTextThroughClosingBrace();
    funcDef.body = &rawblock;
    for(typeDef*& g : languageService.globals)
        if(auto* stub = dynamic_cast<functionDef*>(g))
            if(stub->name == funcDef.name && stub->isPrePassStub){ g = &funcDef; return false; }
    languageService.globals.push_back(&funcDef);
    return false;
}

bool bglParser::processArrayDeclarationFromGeneric(token arrayTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "array" "<" have been consumed. Reads: elementType > name symbol
    string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
    file.getToken(">");
    // Build the full generic type token (e.g. "array<int>") so processRoutineDeclaration sees it
    token typeTok = arrayTok;
    typeTok.value = "array<" + elemType + ">";
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    token symbol = file.getToken({token::bracketOpen, token::assignment, token::endStatement, token::parenOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, name, ctx, q.isExtern, q.isEmitter, q.isReplace);
    processArrayDeclaration(arrayTok, name, elemType, symbol, ctx, q.isExtern);
    return false;
}

bool bglParser::processTypedObjectDeclaration(token typeTok, token nameTok, token classNameTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "Type name : ClassName" have been consumed. Reads optional "as alias" then symbol.
    string objectClassName = classNameTok.value;
    string i6alias;
    if(file.peekToken().is("as")){
        file.getToken();
        token aliasTok = file.getToken(eTokenType::identifier);
        i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    }
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, nameTok, ctx, q.isExtern, q.isEmitter, q.isReplace);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, nameTok, q.isExtern, objectClassName, i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias);
}

bool bglParser::processAliasedDeclaration(token typeTok, token nameTok, token aliasTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "Type name as alias" have been consumed. Reads symbol.
    string i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, nameTok, ctx, q.isExtern, q.isEmitter, q.isReplace);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, nameTok, q.isExtern, "", i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias);
}

bool bglParser::processObjectDeclaration(token objectType, token name, bool isExternal, string className, string i6alias, bool hasBody, bool isEmitter){
    // If emitter qualifier is set and body is present, this is an emitter value declaration
    if(isEmitter && hasBody) return processEmitterValueDeclaration(objectType, name);


    // Resolve the class to determine if this is a verb-derived object
    string resolvedClassName = !className.empty() ? className : objectType.value;
    bool isVerbDerived = false;
    if(!resolvedClassName.empty()){
        function<bool(classDef*)> checkVerbBase = [&](classDef* cls) -> bool {
            if(!cls) return false;
            if(cls->name == "verb") return true;
            for(classDef* base : cls->baseClasses)
                if(checkVerbBase(base)) return true;
            return false;
        };
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(resolvedClassName)))
            isVerbDerived = checkVerbBase(cls);
    }

    // Create verbObjectDef for verb-derived objects, objectDef otherwise
    objectDef* objPtr;
    verbObjectDef* vod = nullptr;
    string origName = name.originalValue.empty() ? (string)name : name.originalValue;
    if(isVerbDerived){
        verbObjectDef& vd = languageService.registerVerbObject(origName, isExternal);
        objPtr = &vd;
        vod = &vd;
    } else {
        objectDef& od = languageService.registerObject((string)name, isExternal, name.originalValue);
        objPtr = &od;
    }
    objectDef& newObj = *objPtr;
    if(!i6alias.empty()) newObj.i6name = i6alias;
    // Doc-comment attached to the object's declaration (leading type or name token).
    if(!objectType.docComment.empty())   newObj.docComment = objectType.docComment;
    else if(!name.docComment.empty())    newObj.docComment = name.docComment;
    if(!resolvedClassName.empty()){
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(resolvedClassName)))
            newObj.objectClass = cls;
        else if(!className.empty())
            parsingError(format("Unknown class '{0}' in object declaration", className));
    }

    // Extern objects: parse body for type registration but validate members.
    // Only type declarations (no initializer) and emitters are allowed.
    if(isExternal){
        if(hasBody){
            objectDef* savedObject = currentObject;
            currentObject = &newObj;
            openCompileContext(eCompileContext::objectDef);

            // Syntax sweetener ("pretty lie") for extern verb claimed-words: when the body
            // begins with a dictionary-word literal, the entire body is interpreted as the
            // trigger-words section of a single grammar line. Desugars to the canonical form
            // `extern verb V { grammar = { {.w1|.w2|.w3} }; }`. Triggers may be combined with
            // `|`-alternation; no pattern tokens are allowed in this form (extern verbs don't
            // emit grammar). The canonical body parsing path below handles all other cases.
            if(file.peekToken().is(eTokenType::dictionaryWord)){
                auto* vod = dynamic_cast<verbObjectDef*>(&newObj);
                if(!vod)
                    parsingError(format("extern object '{0}': dictionary-word body shorthand is only valid on extern verbs", newObj.dName()));
                grammarLine line;
                token trigger = file.getToken(eTokenType::dictionaryWord);
                line.verbWord = trigger.value;
                while(file.peekToken().is("|")){
                    file.getToken();  // consume |
                    token alt = file.getToken(eTokenType::dictionaryWord);
                    line.additionalVerbWords.push_back(alt.value);
                }
                file.getToken(token::braceClose);
                line.targetVerb = newObj.displayName.empty() ? newObj.name : newObj.displayName;
                line.isOwnLine = true;
                vod->grammarLines.push_back(line);
                closeCompileContext(eCompileContext::objectDef);
                currentObject = savedObject;
                return false;
            }

            token tok = file.getToken();
            while(tok.isNot(token::braceClose) && tok.isNot(eTokenType::eof)){
                Qualifiers q = parseQualifiers(tok);
                if(q.isEmitter){
                    // Emitter methods and emitter values are allowed on extern objects
                    token retType = tok;
                    token propName = file.getToken(eTokenType::identifier);
                    if(propName.is("operator")){
                        token opTok = file.getToken();
                        if(opTok.is(token::parenOpen)) propName.value = "operator()";
                        else if(opTok.is("?")) propName.value = "?";
                        else if(opTok.is("switch")) propName.value = "switch";
                        else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
                    }
                    functionDef& funcDef = *(new functionDef());
                    funcDef.name = (string)propName;
                    funcDef.returnType = languageService.getType((string)retType);
                    funcDef.isEmitter = true;
                    funcDef.isExplicit = q.isExplicit;
                    funcDef.isDefault = q.isDefault;
                    token sym = file.getToken();
                    bool funcHasParens = false;
                    if(sym.is(token::parenOpen)){ funcHasParens = true; processParameterList(funcDef); sym = file.getToken(); }
                    i6Block& rawblock = *(new i6Block());
                    if(sym.is(token::endStatement)) rawblock.i6Body = " $self";
                    else { rawblock.i6Body = file.getRawTextThroughClosingBrace(); if(!funcHasParens) funcDef.isValueEmitter = true; }
                    funcDef.body = &rawblock;
                    newObj.members.push_back((typeMember*)&funcDef);
                } else if(tok.isDataType()){
                    // Type declaration or method stub on extern object
                    token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
                    token sym = file.getToken({token::endStatement, token::assignment, token::parenOpen});
                    if(sym.is(token::assignment)){
                        // Property with initializer — skip the value, issue error
                        token v = file.getToken();
                        while(!v.is(token::endStatement) && !v.is(token::braceClose) && !v.is(eTokenType::eof)) v = file.getToken();
                        parsingError(format("extern object '{0}': property '{1}' cannot have an initializer; extern objects are defined in I6",
                            newObj.dName(), propName.value));
                    }
                    if(sym.is(token::parenOpen)){
                        // Method declaration — register as functionDef for type checking
                        functionDef& funcDef = *(new functionDef());
                        funcDef.name = propName.value;
                        funcDef.returnType = languageService.getType(tok.value);
                        funcDef.isExternal = true;
                        processParameterList(funcDef);
                        // Body is not allowed on non-emitter methods in extern objects
                        token after = file.getToken();
                        if(after.is(token::braceOpen)){
                            file.getRawTextThroughClosingBrace();
                            parsingError(format("extern object '{0}': non-emitter method '{1}' cannot have a body; use 'emitter' or declare without a body",
                                newObj.dName(), propName.value));
                        }
                        // else: after is ; — declaration stub, valid
                        newObj.members.push_back((typeMember*)&funcDef);
                        tok = file.getToken();
                        continue;
                    }
                    // Property without initializer — register for type checking
                    variableDeclaration& prop = *(new variableDeclaration());
                    prop.name = propName.value;
                    prop.type = languageService.getType(tok.value);
                    prop.isExternal = true;
                    newObj.members.push_back((typeMember*)&prop);
                } else if(tok.is(eTokenType::identifier) && tok.value == "grammar"){
                    // Extern verb body: `grammar = { {.w1|.w2|.w3, ...}, ... };` declares the dict
                    // words this verb claims at the I6 level. Identical syntax to non-extern verbs;
                    // the difference is purely emission: extern verbs never emit grammar (their I6
                    // grammar is defined externally), so only the trigger words (first dict word
                    // of each line plus |-alternation extras) are meaningful here. Pattern tokens
                    // after the trigger position are parsed (for syntax validation) but ignored;
                    // a warning is issued so the author knows they have no effect.
                    auto* vod = dynamic_cast<verbObjectDef*>(&newObj);
                    if(!vod)
                        parsingError(format("'grammar' is only valid inside a verb body (on '{0}')", newObj.dName()));
                    file.getToken(token::assignment);
                    vector<grammarLine> lines = parseGrammarLines();
                    string verbDisplayName = newObj.displayName.empty() ? newObj.name : newObj.displayName;
                    for(grammarLine& gl : lines){
                        if(!gl.patternTokens.empty())
                            parsingWarning(format("extern verb '{0}': pattern tokens after the trigger word(s) in `grammar = {{...}}` are ignored — extern verbs don't emit I6 grammar; only the dict words in the first position contribute to the verb's claimed-words list",
                                verbDisplayName));
                        gl.targetVerb = verbDisplayName;
                        gl.isOwnLine = true;
                        vod->grammarLines.push_back(gl);
                    }
                    if(file.peekToken().is(token::endStatement)) file.getToken();
                } else {
                    parsingError(format("Unexpected '{0}' in extern object body", tok.value));
                }
                tok = file.getToken();
            }
            closeCompileContext(eCompileContext::objectDef);
            currentObject = savedObject;
        }
        return false;
    }

    objectDef* savedObject = currentObject;
    currentObject = &newObj;
    openCompileContext(eCompileContext::objectDef);

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError(format("Unexpected end of file inside object '{0}' — missing closing '}}'", newObj.dName()));
        if(tok.is(eTokenType::directive)){
            if(tok.is("#i6")) processI6InlineMember(newObj);
            else parsingError(format("Unsupported directive in object body: '{0}'", tok.value));
            tok = file.getToken();
            continue;
        }
        Qualifiers q = parseQualifiers(tok);
        bool memberIsReplace = q.isReplace;
        // Context-specific validation
        if(q.isExtern)  parsingError("'extern' is not valid inside an object body");
        if(q.isExtend)  parsingError("'extend' is not valid inside an object body");
        if(q.isAlias){
            // Type alias member: `alias memberName for TypeName;`
            // Creates a compile-time type reference — used by namespace-scoped type resolution.
            token aliasName = tok;  // tok is already the first token after qualifiers
            token forKw = file.getToken();
            if(!forKw.is("for"))
                parsingError(format("'alias {0}' in object body requires 'for': alias {0} for TypeName;", aliasName.value));
            token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            if(!languageService.isClassType(typeTok.value))
                parsingError(format("alias member '{0}': '{1}' is not a declared type (class or enum)", aliasName.value,
                    typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
            file.getToken(token::endStatement);
            variableDeclaration& aliasDef = *(new variableDeclaration());
            aliasDef.name = (string)aliasName;
            aliasDef.displayName = aliasName.originalValue;
            aliasDef.type = languageService.getType(typeTok.value);
            aliasDef.isExternal = true;  // no I6 emission
            aliasDef.isAlias = true;     // marks as type alias, not instance
            newObj.members.push_back(&aliasDef);
            tok = file.getToken();
            continue;
        }
        if(q.isConst)   parsingError("'const' is not valid inside an object body (use on the property type)");
        if(q.isStatic)  parsingError("'static' is not valid inside an object body");
        if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
        if(q.isEmitter){
            // Alias member: `emitter auto name = ClassRef;` — compile-time indirection to another class.
            // Supported on objects so namespace-like objects can expose emitter classes alongside real state.
            if(tok.value == "auto"){
                token aliasName = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(token::assignment);
                token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhs.value));
                if(!rhsCls)
                    parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                        (string)aliasName, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
                file.getToken(token::endStatement);
                variableDeclaration& aliasDef = *(new variableDeclaration());
                aliasDef.name = (string)aliasName;
                aliasDef.type = languageService.getType(rhs.value);
                aliasDef.isExternal = true;  // no I6 emission
                newObj.members.push_back(&aliasDef);
                tok = file.getToken();
                continue;
            }
            // emitter method inside object body — parse as raw I6 body
            token retType = tok;
            token propName = file.getToken(eTokenType::identifier);
            if(propName.is("operator")){
                token opTok = file.getToken();
                if(opTok.is(token::parenOpen)) propName.value = "operator()";
                else if(opTok.is("?")) propName.value = "?";
                else if(opTok.is("switch")) propName.value = "switch";
                else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
            }
            functionDef& funcDef = *(new functionDef());
            funcDef.name = (string)propName;
            funcDef.returnType = languageService.getType((string)retType);
            funcDef.isEmitter = true;
            funcDef.isExplicit = q.isExplicit;
            funcDef.isDefault = q.isDefault;
            if(q.isExplicit && funcDef.name != "operator()")
                parsingError("'explicit' is only valid on conversion operators (operator())");
            token sym = file.getToken();
            bool hasParens = false;
            if(sym.is(token::parenOpen)){
                hasParens = true;
                processParameterList(funcDef);
                sym = file.getToken();
            }
            i6Block& rawblock = *(new i6Block());
            if(sym.is(token::endStatement)){
                // Semicolon-terminated emitter: pass-through
                rawblock.i6Body = " $self";
            } else {
                // sym should be braceOpen
                rawblock.i6Body = file.getRawTextThroughClosingBrace();
                if(!hasParens) funcDef.isValueEmitter = true;
            }
            funcDef.body = &rawblock;
            if(!replaceStubMember(newObj.members, funcDef))
                newObj.members.push_back((typeMember*)&funcDef);
        } else if(tok.value == "array")
            processArrayMember(newObj.members, newObj.dName(), dynamic_cast<verbObjectDef*>(&newObj), &newObj, &q);
        else if(tok.isDataType())
            processTypedMember(newObj, tok, memberIsReplace);
        else if(tok.is(eTokenType::identifier))
            processInheritedMember(newObj, tok);
        else
            parsingError(format("Unexpected token '{0}' in object body", tok.value));
        tok = file.getToken();
    }

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;

    // Verb-specific post-processing: link perform() and warn if missing
    if(vod && !isExternal){
        for(typeMember* m : vod->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "perform"){ vod->doFunc = fd; break; }
        if(!vod->doFunc)
            parsingWarning(format("verb '{0}' does not define perform()", origName));
    }

    return false;
}

bool bglParser::processBeguilerSettings(){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("beguilerSettings is only allowed in global context");

    // Use the global singleton — CLI-set fields take precedence; source block fills gaps.
    beguilerSettingsDef& cfg = beguilerSettings;

    // Look up the schema class for property name/type validation
    classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilerSettingstype"));

    file.getToken(token::braceOpen);
    token tok = file.getToken();
    while(!tok.is(token::braceClose) && !tok.is(eTokenType::eof)){
        // each entry: [<type>] <name> = <value> ;
        // Type is optional: if the second token is '=', the first is the property name.
        // Otherwise the first is the type and the second is the property name.
        token propName;
        token second = file.getToken();
        if(second.is(token::assignment)){
            propName = tok;
        } else {
            if(!tok.is(eTokenType::identifier) && !tok.isDataType())
                parsingError(format("Expected type or property name in #beguilerSettings, got '{0}'", tok.value));
            propName = second;
            file.getToken(token::assignment);
        }

        string key = propName.value; // already lowercase (lexer normalises identifiers)

        // Validate property name and look up its declared type from the schema class
        string memberType;
        if(schema){
            typeMember* m = findMemberInHierarchy(schema, [&](typeMember* tm){ return tm->name == key; });
            if(!m)
                parsingError(format("Unknown beguilerSettings property '{0}'", key));
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                memberType = vd->type.name;
        }

        token val = file.getToken({eTokenType::identifier, eTokenType::quote, eTokenType::rawQuote, eTokenType::integer});
        // Allow optional qualified enum form: EnumType.Member — consume the dot and member, use member as value
        if(val.is(eTokenType::identifier) && file.peekToken().is(".")){
            file.getToken(".");
            token member = file.getToken(eTokenType::identifier);
            // Validate that qualifier matches the declared member type
            if(!memberType.empty() && val.value != memberType)
                parsingError(format("beguilerSettings property '{0}' expects a {1} value, got '{2}'", key, memberType, val.value));
            val = member;
        }
        file.getToken(token::endStatement);

        // Type-check the value against the schema-declared member type
        if(!memberType.empty()){
            bool expectInt = (memberType == "int");
            bool expectStr = (memberType == "string");
            if(expectInt && !val.is(eTokenType::integer))
                parsingError(format("beguilerSettings property '{0}' expects an int, got '{1}'", key, val.value));
            if(expectStr && !val.isString())
                parsingError(format("beguilerSettings property '{0}' expects a string, got '{1}'", key, val.value));
            if(!expectInt && !expectStr && !val.is(eTokenType::identifier))
                parsingError(format("beguilerSettings property '{0}' expects a {1} value, got '{2}'", key, memberType, val.value));
        }

        string strVal = val.value;
        if(val.isString()){
            strVal = val.unescape(val.value);
            if(strVal.size() >= 2 && strVal.front() == '"' && strVal.back() == '"')
                strVal = strVal.substr(1, strVal.size() - 2);
        }

        // Per-property storage and semantic validation
        if(key == "errorformat"){
            string upper = strVal;
            transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if(upper != "E1" && upper != "E2")
                parsingError(format("beguilerSettings property 'errorformat' must be E1 or E2, got '{0}'", strVal));
            if(cfg.errorFormat.empty()) cfg.errorFormat = upper.substr(1); // store just the digit
        }
        else if(key == "beguilibpath"){ if(cfg.beguiLibPath.empty())    cfg.beguiLibPath = rewritePathSeps(strVal); }
        else if(key == "informpath"){   if(cfg.informBinaryPath.empty()) cfg.informBinaryPath = rewritePathSeps(strVal); }
        else if(key == "informname"){   if(cfg.informName.empty())       cfg.informName = rewritePathSeps(strVal); }
        else if(key == "outputpath"){   if(cfg.outputPath.empty())       cfg.outputPath = rewritePathSeps(strVal); }
        else if(key == "includepaths"){
            // Comma-separated list, matching I6 ICL `+include_path=a,b,c` convention.
            // Whitespace around each entry is trimmed; empty entries are skipped; existing
            // entries are not duplicated.
            size_t i = 0;
            while(i < strVal.size()){
                size_t comma = strVal.find(',', i);
                string entry = strVal.substr(i, comma == string::npos ? string::npos : comma - i);
                size_t a = entry.find_first_not_of(" \t");
                size_t b = entry.find_last_not_of(" \t");
                if(a != string::npos){
                    string path = rewritePathSeps(entry.substr(a, b - a + 1));
                    if(find(cfg.includePaths.begin(), cfg.includePaths.end(), path) == cfg.includePaths.end())
                        cfg.includePaths.push_back(path);
                }
                if(comma == string::npos) break;
                i = comma + 1;
            }
        }
        else if(key == "release"){ if(cfg.release == 0) cfg.release = stoi(strVal); }
        else if(key == "serial"){
            if(cfg.serial.empty()){
                if(strVal.size() != 6 || !all_of(strVal.begin(), strVal.end(), ::isdigit))
                    parsingError("beguilerSettings property 'serial' must be exactly 6 digits (e.g. \"250328\")");
                cfg.serial = strVal;
            }
        }
        else if(key == "framepoolsize"){
            int sz = stoi(strVal);
            if(sz < 1) parsingError("beguilerSettings property 'framePoolSize' must be at least 1");
            if(cfg.framePoolSize == -1) cfg.framePoolSize = sz;  // -1 = unset sentinel
        }
        else if(key == "linqscratchsize"){
            int sz = stoi(strVal);
            if(sz < 1) parsingError("beguilerSettings property 'linqScratchSize' must be at least 1");
            if(cfg.linqScratchSize == -1) cfg.linqScratchSize = sz;  // -1 = unset sentinel
        }
        else if(key == "target"){
            if(languageService.getEnumType(strVal) != "etarget")
                parsingError(format("Invalid target '{0}'. Must be a value of eTarget (Glulx, Z3, Z5, or Z8).", strVal));
            if(cfg.target.empty()) cfg.target = strVal;
        }
        else if(key == "rewritepaths"){
            if(!cfg.rewritePaths.has_value()){
                if(strVal == "true")       cfg.rewritePaths = true;
                else if(strVal == "false") cfg.rewritePaths = false;
                else parsingError(format("beguilerSettings property 'rewritePaths' expects true or false, got '{0}'", strVal));
            }
        }
        else if(key == "generateblorb"){
            if(strVal == "true")       cfg.blorbEnabled = true;
            else if(strVal == "false") cfg.blorbEnabled = false;
            else parsingError(format("beguilerSettings property 'generateBlorb' expects true or false, got '{0}'", strVal));
        }
        else if(key == "blorbassetpath"){ if(cfg.blorbAssetPath.empty()) cfg.blorbAssetPath = rewritePathSeps(strVal); }
        else if(key == "author"){         if(cfg.author.empty())         cfg.author         = strVal; }
        else if(key == "title"){          if(cfg.title.empty())          cfg.title          = strVal; }
        else if(key == "headline"){       if(cfg.headline.empty())       cfg.headline       = strVal; }
        else if(key == "genre"){          if(cfg.genre.empty())          cfg.genre          = strVal; }
        else if(key == "description"){    if(cfg.description.empty())    cfg.description    = strVal; }
        else if(key == "language"){       if(cfg.language.empty())       cfg.language       = strVal; }
        else if(key == "series"){         if(cfg.series.empty())         cfg.series         = strVal; }
        else if(key == "seriesnumber"){   if(cfg.seriesNumber == 0)      cfg.seriesNumber   = stoi(strVal); }
        else if(key == "firstpublished"){ if(cfg.firstPublished.empty()) cfg.firstPublished = strVal; }
        else if(key == "forgiveness"){    if(cfg.forgiveness.empty())    cfg.forgiveness    = strVal; }
        else if(key == "ifid"){           if(cfg.ifid.empty())           cfg.ifid           = strVal; }

        tok = file.getToken();
    }

    // Apply path overrides immediately so subsequent includes use them
    // (informBinaryPath is intentionally excluded here — resolved after all parsing in beguiler.cpp)
    if(!cfg.beguiLibPath.empty()) settings.libPath = cfg.beguiLibPath;
    if(!cfg.outputPath.empty() && settings.outputPath.empty()) settings.outputPath = cfg.outputPath;

    return false;
}

// Apply default values declared on beguilerSettingsType members to any settings fields
// that were never set by a #beguilerSettings block.  Called once after all parsing is done.
void bglParser::applySchemaDefaults(){
    beguilerSettingsDef& cfg = beguilerSettings;
    classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilerSettingstype"));
    if(!schema) return;

    for(typeMember* m : schema->members){
        variableDeclaration* vd = dynamic_cast<variableDeclaration*>(m);
        if(!vd || !vd->declaredExpressionValue) continue;

        string key = vd->name; // already lowercase
        string defVal = vd->declaredExpressionValue->text();
        // Strip surrounding quotes if the default is a string literal
        if(defVal.size() >= 2 && defVal.front()=='"' && defVal.back()=='"')
            defVal = defVal.substr(1, defVal.size()-2);

        if(key == "target"        && cfg.target.empty())       cfg.target = defVal;
        else if(key == "framepoolsize" && cfg.framePoolSize == -1) cfg.framePoolSize = stoi(defVal);
        else if(key == "linqscratchsize" && cfg.linqScratchSize == -1) cfg.linqScratchSize = stoi(defVal);
        else if(key == "errorformat"  && cfg.errorFormat.empty()){
            string upper = defVal;
            transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if(upper == "E1" || upper == "E2") cfg.errorFormat = upper.substr(1);
        }
        else if(key == "release"      && cfg.release == 0)         cfg.release = stoi(defVal);
        else if(key == "rewritepaths" && !cfg.rewritePaths.has_value()) cfg.rewritePaths = (defVal == "true");
        else if(key == "blorbassetpath" && cfg.blorbAssetPath.empty()) cfg.blorbAssetPath = defVal;
    }

    // Inject compile-time target symbols if not already set by extractBlorbSettings
    if(definedSymbols.find("target_glulx") == definedSymbols.end()
       && definedSymbols.find("target_zcode") == definedSymbols.end()){
        string t = cfg.target;
        transform(t.begin(), t.end(), t.begin(), ::tolower);
        if(t == "glulx")
            definedSymbols["target_glulx"] = "";
        else if(t.size() == 2 && t[0] == 'z' && isdigit(t[1]))
            definedSymbols["target_zcode"] = string(1, t[1]);
    }
}
#pragma endregion

//-------------------------------------------------------------------------------------------------------------------------------
// Throw an error, formatting the output to point to the current line
bool bglParser::parsingError(string msg){
    string errorMessage;
    if(file.getNumberOfOpenFiles()>0) {
        // Prefer the statement-start location if set; fall back to current stream position.
        string fileName;
        int curLine, curCol;
        if(currentStatementSrc.line > 0){
            fileName = currentStatementSrc.file;
            curLine  = currentStatementSrc.line;
            curCol   = 1;
        } else {
            auto detail = file.getCurrentFileDetail();
            fileName = get<1>(detail);
            curLine  = get<2>(detail);
            curCol   = get<3>(detail);
        }
        errorMessage=format("{0}:{1}:{2}: ERROR: {3}",fileName,curLine,curCol,msg);
    }
    else{
        errorMessage=msg;
    }
    
    if(lspMode) {
        lspErrors.push_back(errorMessage);
        throw lspRecoverySignal(errorMessage);
    }
    throw runtime_error(errorMessage);
    return true; //won't ever actually run
}
void bglParser::parsingWarning(string msg){
    string warningMessage;
    if(file.getNumberOfOpenFiles()>0) {
        string fileName;
        int curLine, curCol;
        if(currentStatementSrc.line > 0){
            fileName = currentStatementSrc.file;
            curLine  = currentStatementSrc.line;
            curCol   = 1;
        } else {
            auto detail = file.getCurrentFileDetail();
            fileName = get<1>(detail);
            curLine  = get<2>(detail);
            curCol   = get<3>(detail);
        }
        warningMessage=format("{0}:{1}:{2}: warning: {3}",fileName,curLine,curCol,msg);
    }
    else{
        warningMessage=msg;
    }
    cerr << warningMessage << endl;
}

#pragma region Compile Context management
//===============================================================================================================================
// Routines to manage the compile context 
//-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
void bglParser::openCompileContext(eCompileContext newScope, statementBlock* body){
    compileContextStack.push_front(newScope);
    // For codeBlock contexts, always push to the active block stack (nullptr if no body provided).
    // closeCompileContext always pops for codeBlock, so push/pop counts must match.
    if(newScope == eCompileContext::codeBlock) activeBlockStack.push_back(body);
}
string bglParser::contextToString(eCompileContext context){
    switch(context){
        case eCompileContext::global:
            return "global";
            break;
        case eCompileContext::objectDef:
            return "object definition";
            break;
        case eCompileContext::codeBlock:
            return "code block";
            break;
        default:
            return "unknown compile context";
    }  
}
void bglParser::closeCompileContext(eCompileContext expectedScope){
    eCompileContext oldScope=compileContextStack.front();
    if(oldScope!=expectedScope) parsingError(format("Internal Error: Attempting to close compile context '{0}' but current context is '{1}'.", contextToString(expectedScope), contextToString(oldScope)));
    compileContextStack.pop_front();
    // Pop the active block stack if we pushed a body on openCompileContext
    if(!activeBlockStack.empty() && expectedScope == eCompileContext::codeBlock)
        activeBlockStack.pop_back();
    // Invalidate cached statement location on leaving a code block — without this,
    // a later global-scope error reuses the last in-body statement's file:line.
    if(expectedScope == eCompileContext::codeBlock) currentStatementSrc.line = 0;
}
eCompileContext bglParser::getCurrentCompileContext(){ 
    if(compileContextStack.size()==0) return eCompileContext::noContext; //we register base data types before we even begin parsing.  So lets assume global context even before any context is officially opened.
    return compileContextStack.front();
}
#pragma endregion Compile Context management
// classDef& bglParser::registerDefinition(classDef newType){
    
//     // classDef& el =languageService.registerType(newType);
    
//     // if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(&el);

//     // return el; 
// }
// typeDef& bglParser::registerDefinition(objectDef newType){
//     //typeDef el;
//     //typeDef& el =languageService.registerType(newType);
//     //if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(std::make_unique<classDef>(newClass));

//     return emptyTDef;
// }
// typeDef& bglParser::registerDefinition(enumDef newType){
//     typeDef el;
//     //typeDef& el =languageService.registerType(newType);
//     //if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(std::make_unique<classDef>(newClass));

//     return emptyTDef;
// }
// typeDef& bglParser::registerDefinition(string newTypeName){
//     //typeDef el;
//     //typeDef& el =languageService.registerType(newTypeName);
//     //if(getCurrentCompileContext()==eCompileContext::global) globals.push_back(el);
//     return emptyTDef;
// }

//===============================================================================================================================
// Object extension: extend <objectName> { }
//===============================================================================================================================

bool bglParser::processObjectExtension(token nameTok){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'extend' declarations are only allowed in global context");

    string lower = nameTok.value;
    // Look up as verb first, then as regular object
    objectDef* obj = nullptr;
    verbObjectDef* vod = nullptr;
    for(verbObjectDef* v : languageService.verbs)
        if(v->name == lower){ obj = v; vod = v; break; }
    if(!obj){
        for(typeDef* g : languageService.globals)
            if(auto* od = dynamic_cast<objectDef*>(g))
                if(od->name == lower){ obj = od; break; }
    }
    if(!obj)
        parsingError(format("extend '{0}': no previously declared object with that name",
            nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));

    bool isExternalObj = obj->isExternal || (vod && vod->isExternal);

    // Parse body using standard object body loop
    file.getToken(token::braceOpen);
    objectDef* savedObject = currentObject;
    currentObject = obj;
    openCompileContext(eCompileContext::objectDef);

    // For verb extend blocks, track the block's grammar priority. `priority = N;` inside this
    // extend body acts as a block-local directive (it stamps the priority onto grammar lines
    // added by `grammar +=` here) rather than mutating a persistent member on the verb. The
    // default comes from `class verb`'s BLR-declared `int priority = N;` default so the value
    // lives in Beguile, not buried in C++.
    int verbPriorityDefault = (vod != nullptr) ? languageService.getClassFieldIntDefault("verb", "priority", 10) : 0;
    int extendBlockPriority = verbPriorityDefault;
    bool extendHadReplaceGrammar = false;  // tracks `grammar = { ... }` (replace) — used to error
                                           // out a later `priority = N;` set in the same block

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside object — missing closing '}'");
        if(tok.is(eTokenType::directive)){
            if(tok.is("#i6")) processI6InlineMember(*obj);
            else parsingError(format("Unsupported directive in extend body: '{0}'", tok.value));
            tok = file.getToken();
            continue;
        }
        Qualifiers q = parseQualifiers(tok);
        bool memberIsReplace = q.isReplace;
        if(q.isExtern)  parsingError("'extern' is not valid inside an extend body");
        if(q.isExtend)  parsingError("'extend' is not valid inside an extend body");
        if(q.isAlias){
            // Type alias member: `alias memberName for TypeName;`
            token aliasName = tok;
            token forKw = file.getToken();
            if(!forKw.is("for"))
                parsingError(format("'alias {0}' in extend body requires 'for': alias {0} for TypeName;", aliasName.value));
            token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            if(!languageService.isClassType(typeTok.value))
                parsingError(format("alias member '{0}': '{1}' is not a declared type (class or enum)", aliasName.value,
                    typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
            file.getToken(token::endStatement);
            variableDeclaration& aliasDef = *(new variableDeclaration());
            aliasDef.name = (string)aliasName;
            aliasDef.displayName = aliasName.originalValue;
            aliasDef.type = languageService.getType(typeTok.value);
            aliasDef.isExternal = true;
            aliasDef.isAlias = true;
            obj->members.push_back(&aliasDef);
            tok = file.getToken();
            continue;
        }
        if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
        if(isExternalObj && !q.isEmitter){
            // For extern objects, only compound assignment on collection members is allowed,
            // plus a few block-local directives that don't actually mutate the extern object
            // (e.g. verb extend's `priority = N;` is captured into extendBlockPriority and
            // stamped onto added grammar lines — it never becomes a member on the extern verb).
            bool isAllowed = false;
            if(tok.isDataType()){
                token peekName = file.peekToken();
                token peekOp = file.peekToken(2);
                if(peekOp.is("+=") || peekOp.is("-=")) isAllowed = true;
            } else if(tok.is(eTokenType::identifier)){
                token peekOp = file.peekToken();
                if(peekOp.is("+=") || peekOp.is("-=")) isAllowed = true;
                else if(vod != nullptr && tok.value == "priority" && peekOp.is(token::assignment))
                    isAllowed = true;  // block-local priority directive (see identifier branch below)
                else if(vod != nullptr && tok.value == "grammar" && peekOp.is(token::assignment))
                    isAllowed = true;  // replace semantics: `grammar = { ... }` (see identifier branch below)
            }
            if(!isAllowed)
                parsingError(format("Cannot add members to extern object '{0}'; only compound assignment (+=) on collection members is allowed",
                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
        }
        if(q.isEmitter){
            if(isExternalObj)
                parsingError(format("Cannot add emitter methods to extern object '{0}'",
                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
            token retType = tok;
            token propName = file.getToken(eTokenType::identifier);
            if(propName.is("operator")){
                token opTok = file.getToken();
                if(opTok.is(token::parenOpen)) propName.value = "operator()";
                else if(opTok.is("?")) propName.value = "?";
                else if(opTok.is("switch")) propName.value = "switch";
                else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
            }
            functionDef& funcDef = *(new functionDef());
            funcDef.name = (string)propName;
            funcDef.returnType = languageService.getType((string)retType);
            funcDef.isEmitter = true;
            funcDef.isExplicit = q.isExplicit;
            funcDef.isDefault = q.isDefault;
            token sym = file.getToken();
            bool hasParens = false;
            if(sym.is(token::parenOpen)){ hasParens = true; processParameterList(funcDef); sym = file.getToken(); }
            i6Block& rawblock = *(new i6Block());
            if(sym.is(token::endStatement)) rawblock.i6Body = " $self";
            else { rawblock.i6Body = file.getRawTextThroughClosingBrace(); if(!hasParens) funcDef.isValueEmitter = true; }
            funcDef.body = &rawblock;
            if(!replaceStubMember(obj->members, funcDef)){
                bool replaced = false;
                if(memberIsReplace){
                    for(size_t i = 0; i < obj->members.size(); i++)
                        if(auto* fd = dynamic_cast<functionDef*>(obj->members[i]))
                            if(fd->name == funcDef.name){ obj->members[i] = &funcDef; replaced = true; break; }
                }
                if(!replaced) obj->members.push_back((typeMember*)&funcDef);
            }
        } else if(tok.value == "array")
            processArrayMember(obj->members, obj->dName(), dynamic_cast<verbObjectDef*>(obj), obj, &q);
        else if(tok.isDataType()){
            // Check for += / -= compound assignment on a typed member
            token peekName = file.peekToken();
            token peekOp = file.peekToken(2);
            if((peekOp.is("+=") || peekOp.is("-=")) && peekName.is(eTokenType::identifier)){
                token memberName = file.getToken();
                token op = file.getToken();
                processExtendCompoundAssignment(*obj, memberName, op.value, vod, extendBlockPriority);
            } else {
                processTypedMember(*obj, tok, memberIsReplace);
            }
        }
        else if(tok.is(eTokenType::identifier)){
            // Check for += / -= compound assignment (inferred type)
            token peekOp = file.peekToken();
            if(peekOp.is("+=") || peekOp.is("-=")){
                token op = file.getToken();
                processExtendCompoundAssignment(*obj, tok, op.value, vod, extendBlockPriority);
            } else if(vod != nullptr && tok.value == "priority" && peekOp.is(token::assignment)){
                // Verb extend block: `priority = N;` is a block-local directive that sets the
                // priority stamped onto subsequent `grammar +=` contributions. It is NOT a
                // persistent member on the verb (which would conflict with multiple extend
                // blocks at different priorities, all merging into the same verbObjectDef).
                file.getToken(token::assignment);
                token valTok = file.getToken();
                int newPriority = extendBlockPriority;
                try { newPriority = stoi(valTok.value); }
                catch(...) { parsingError(format("priority: expected integer literal, got '{0}'", valTok.value)); }
                if(extendHadReplaceGrammar && newPriority != verbPriorityDefault)
                    parsingError("priority is meaningless with `grammar = { ... }` (replace); remove one or set priority to the default");
                extendBlockPriority = newPriority;
                file.getToken(token::endStatement);
            } else if(vod != nullptr && tok.value == "grammar" && peekOp.is(token::assignment)){
                // Verb extend block: `grammar = { ... }` is REPLACE semantics — emits I6
                // `Extend 'v' replace …` directives, wiping the verb's previous rules for
                // each trigger word. Priority is meaningless under replace; if the block also
                // set a non-default `priority = N;`, that's a contradiction.
                if(extendBlockPriority != verbPriorityDefault)
                    parsingError("`grammar = { ... }` (replace) cannot be combined with a non-default `priority = N;` in the same extend block — priority is meaningless under replace");
                file.getToken(token::assignment);
                vector<grammarLine> lines = parseGrammarLines();
                if(file.peekToken().is(token::endStatement)) file.getToken();
                string inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

                // Find or create the verb's "grammar" rule-list and route lines through it.
                // This mirrors the `+=` path: extern verbs are emitted via the globals-level
                // grammarRuleListDecl (since emitVerbObject early-returns for extern), and
                // non-extern verbs pick the same lines up via vod->grammarLines.
                grammarRuleListDecl* gtd = nullptr;
                for(typeMember* m : obj->members)
                    if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                        if(g->name == "grammar"){ gtd = g; break; }
                if(!gtd){
                    gtd = new grammarRuleListDecl();
                    gtd->name = "grammar";
                    gtd->type = languageService.getType("grammarrulelist");
                    gtd->verbName = inferredVerb;
                    obj->members.push_back(gtd);
                    if(vod->isExternal) languageService.globals.push_back(gtd);
                }
                for(grammarLine& gl : lines){
                    gl.targetVerb     = inferredVerb;
                    gl.isOwnLine      = false;
                    gl.isReplaceMode  = true;
                    gtd->grammarLines.push_back(gl);
                    vod->grammarLines.push_back(gl);
                }
                extendHadReplaceGrammar = true;
            } else {
                processInheritedMember(*obj, tok);
            }
        }
        else
            parsingError(format("Unexpected token '{0}' in extend body", tok.value));
        tok = file.getToken();
    }

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;

    // For verb objects: link perform() and add grammar to globals if external
    if(vod){
        for(typeMember* m : vod->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "perform"){ vod->doFunc = fd; break; }
    }

    return false;
}

// Compound assignment (+= / -=) on an existing collection member inside an extend body.
// Supports: grammarRuleList, attributeList, array<T>.
void bglParser::processExtendCompoundAssignment(objectDef& obj, token memberName, const string& op, verbObjectDef* vod, int blockPriority){
    string memberNameStr = memberName.value;
    string display = memberName.originalValue.empty() ? memberName.value : memberName.originalValue;

    // Find the existing member on the object
    string memberType;
    typeMember* existingMember = nullptr;
    // Search object's own members
    for(typeMember* m : obj.members){
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->name == memberNameStr){ memberType = vd->type.name; existingMember = m; break; }
    }
    // Search class hierarchy if not found on object
    if(memberType.empty()){
        function<void(classDef*)> searchClass = [&](classDef* cls){
            if(!cls || !memberType.empty()) return;
            for(typeMember* m : cls->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == memberNameStr){ memberType = vd->type.name; break; }
            for(classDef* base : cls->baseClasses) searchClass(base);
        };
        if(obj.objectClass) searchClass(obj.objectClass);
    }
    if(memberType.empty())
        parsingError(format("'{0}' is not a member of this object; cannot use {1}", display, op));

    // -= on extern objects is not allowed — we don't control their original data
    bool isExternalObj = obj.isExternal || (vod && vod->isExternal);
    if(isExternalObj && op == "-=")
        parsingError(format("Cannot use -= on extern object '{0}'; the original data is defined externally",
            obj.displayName.empty() ? obj.name : obj.displayName));

    // grammarRuleList += / -=
    if(memberType == "grammarrulelist"){
        if(op == "+="){
            // Parse grammar lines and append
            vector<grammarLine> lines = parseGrammarLines();
            string inferredVerb;
            if(vod) inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

            // Find or create the grammarRuleListDecl on the object
            grammarRuleListDecl* gtd = nullptr;
            for(typeMember* m : obj.members)
                if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                    if(g->name == memberNameStr){ gtd = g; break; }
            if(!gtd){
                gtd = new grammarRuleListDecl();
                gtd->name = memberNameStr;
                gtd->type = languageService.getType("grammarrulelist");
                gtd->verbName = inferredVerb;
                obj.members.push_back(gtd);
                if(vod && vod->isExternal) languageService.globals.push_back(gtd);
            }
            for(grammarLine& gl : lines){
                gl.priority = blockPriority;
                gl.isOwnLine = false;        // extend-block contribution, sorts against verb's anchor
                grammarRuleDecl& rd = *(new grammarRuleDecl());
                rd.name = memberNameStr;
                rd.type = languageService.getType("grammarrule");
                rd.line = gl;
                rd.targetVerb = inferredVerb;
                gtd->rules.push_back(&rd);
                gl.targetVerb = inferredVerb;
                gtd->grammarLines.push_back(gl);
            }
            if(vod)
                vod->grammarLines.insert(vod->grammarLines.end(), gtd->grammarLines.end() - lines.size(), gtd->grammarLines.end());
        } else {
            // -= : remove matching grammar lines
            vector<grammarLine> toRemove = parseGrammarLines();
            for(typeMember* m : obj.members){
                if(auto* g = dynamic_cast<grammarRuleListDecl*>(m)){
                    if(g->name != memberNameStr) continue;
                    for(const grammarLine& rem : toRemove){
                        g->grammarLines.erase(
                            remove_if(g->grammarLines.begin(), g->grammarLines.end(),
                                [&](const grammarLine& gl){ return gl.verbWord == rem.verbWord && gl.additionalVerbWords == rem.additionalVerbWords && gl.patternTokens == rem.patternTokens; }),
                            g->grammarLines.end());
                    }
                }
            }
            if(vod){
                for(const grammarLine& rem : toRemove){
                    vod->grammarLines.erase(
                        remove_if(vod->grammarLines.begin(), vod->grammarLines.end(),
                            [&](const grammarLine& gl){ return gl.verbWord == rem.verbWord && gl.patternTokens == rem.patternTokens; }),
                        vod->grammarLines.end());
                }
            }
        }
    }
    // attributeList += / -=
    else if(memberType == "attributelist"){
        file.getToken(token::braceOpen);
        vector<string> attrs;
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
            attrs.push_back(t.value);
            t = file.getToken({token::comma, token::braceClose});
            if(t.is(token::comma)) t = file.getToken();
        }
        // Find existing attributeList member
        variableDeclaration* attrMember = nullptr;
        for(typeMember* m : obj.members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == memberNameStr){ attrMember = vd; break; }
        if(op == "+="){
            if(!attrMember){
                attrMember = new variableDeclaration();
                attrMember->name = memberNameStr;
                attrMember->type = languageService.getType("attributelist");
                attrMember->declaredExpressionValue = new initializerList();
                obj.members.push_back(attrMember);
            }
            auto* list = dynamic_cast<initializerList*>(attrMember->declaredExpressionValue);
            if(!list){ list = new initializerList(); attrMember->declaredExpressionValue = list; }
            for(const string& a : attrs){
                expression* elem = new expression();
                elem->tokens.push_back(a);
                elem->resolvedType = "attribute";
                list->elements.push_back(elem);
            }
        } else {
            // -= : remove attributes
            if(attrMember){
                if(auto* list = dynamic_cast<initializerList*>(attrMember->declaredExpressionValue)){
                    for(const string& a : attrs){
                        list->elements.erase(
                            remove_if(list->elements.begin(), list->elements.end(),
                                [&](expression* e){ return !e->tokens.empty() && e->tokens[0] == a; }),
                            list->elements.end());
                    }
                }
            }
        }
    }
    // array<T> += / -=
    else if(memberType == "array"){
        file.getToken(token::braceOpen);
        initializerList* newElements = new initializerList();
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
            expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
            newElements->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        // Find existing array member
        arrayDeclaration* arrMember = nullptr;
        for(typeMember* m : obj.members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == memberNameStr){ arrMember = ad; break; }
        if(op == "+="){
            if(arrMember){
                auto* list = dynamic_cast<initializerList*>(arrMember->declaredExpressionValue);
                if(!list){ list = new initializerList(); arrMember->declaredExpressionValue = list; }
                for(expression* e : newElements->elements)
                    list->elements.push_back(e);
            }
        } else {
            // -= : remove matching elements by text value
            if(arrMember){
                if(auto* list = dynamic_cast<initializerList*>(arrMember->declaredExpressionValue)){
                    for(expression* rem : newElements->elements){
                        string remText = rem->text();
                        list->elements.erase(
                            remove_if(list->elements.begin(), list->elements.end(),
                                [&](expression* e){ return e->text() == remText; }),
                            list->elements.end());
                    }
                }
            }
        }
    }
    else {
        parsingError(format("Type '{0}' does not support {1} in extend body", memberType, op));
    }

    if(file.peekToken().is(token::endStatement)) file.getToken();
}

//===============================================================================================================================
// Grammar declarations
//===============================================================================================================================

bool bglParser::processGrammarDeclaration(token nameOverride){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'grammar' declarations are only allowed in global context");

    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken(eTokenType::identifier);
    string grammarName = name.originalValue.empty() ? name.value : name.originalValue;

    file.getToken(token::braceOpen);
    if(file.peekToken().is(token::braceOpen))
        parsingError(format("Bare grammar lines in 'grammar {0} {{ ... }}' are no longer supported. To add lines to an existing verb use 'extend {0} {{ grammar += {{ ... }}; }}'; to author a cross-cutting grammar object use 'grammar {0} {{ grammarRule r = {{Verb, {{ ... }}}}; }}'.", grammarName));
    return processGrammarObjectDeclaration(grammarName);
}

// Parse a grammar object body with grammarRule members.
// Assumes outer '{' already consumed. grammarName is the object name.
// Members are: grammarRule name = {VerbRef, {.word, TOKEN, ...}};
// or array<grammarRule> name = {{VerbRef, {.word, ...}}, ...};
// Type can be inferred from the grammarTable class members.
bool bglParser::processGrammarObjectDeclaration(const string& grammarName){
    // Resolve grammarTable class for type inference
    classDef* gtClass = dynamic_cast<classDef*>(&languageService.getType("grammarrulelist"));

    // Create a grammarRuleListDecl to hold all the rules
    grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
    gtd.name = grammarName;
    gtd.type = languageService.getType("grammarrulelist");

    // Helper: resolve a verb reference token, return original-case verb name or error
    auto resolveVerbRef = [this](const token& verbTok) -> string {
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower)
                return verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        return "";
    };

    // Helper: parse a single grammarRule initializer {VerbRef, {.word, TOKEN, ...}, priority?}
    // Assumes outer '{' already consumed. Optional 3rd positional element is the line's sort
    // priority (default 10 from BLR `class verb`).
    auto parseGrammarRuleInit = [&](grammarRuleDecl& rd){
        // First element: verb reference
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        rd.targetVerb = resolveVerbRef(verbTok);
        file.getToken(token::comma);
        // Second element: grammar line pattern {.word, TOKEN, ...}
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional 3rd element: priority
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        // Closing } of the outer initializer
        file.getToken(token::braceClose);
    };

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside settings/object body — missing closing '}'");
        // Determine type — explicit or inferred
        string memberType;
        bool isArray = false;
        if(tok.is("array")){
            // explicit array<grammarRule>
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            memberType = "grammarrule";
            isArray = true;
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.isDataType() && tok.value == "grammarrule"){
            // explicit grammarRule
            memberType = "grammarrule";
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.is(eTokenType::identifier)){
            // inferred type from grammarTable class
            // tok is the member name; infer type from class
            if(gtClass){
                // Check if assignment follows — peek for = or {
                token afterName = file.peekToken();
                if(afterName.is(token::assignment)){
                    // Look at initializer shape to determine type
                    // For now, we'll determine after reading the initializer
                    memberType = "grammarrule"; // default inference
                }
            }
            if(memberType.empty())
                parsingError(format("Cannot infer type for '{0}' in grammar object body", tok.value));
        } else {
            parsingError(format("Unexpected '{0}' in grammar object body", tok.value));
        }

        string memberName = tok.value;
        file.getToken(token::assignment);

        token first = file.getToken(token::braceOpen);

        // Validate shape matches declared type
        token peek2 = file.peekToken();
        if(isArray){
            // array<grammarRule>: must be {{VerbRef, {pattern}}, ...}
            if(!peek2.is(token::braceOpen))
                parsingError(format("array<grammarRule> '{0}' requires nested initializer list of rule pairs", memberName));
            token inner = file.getToken();
            while(inner.is(token::braceOpen)){
                grammarRuleDecl& rd = *(new grammarRuleDecl());
                rd.name = memberName;
                rd.type = languageService.getType("grammarrule");
                parseGrammarRuleInit(rd);
                gtd.rules.push_back(&rd);
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                gtd.grammarLines.push_back(gl);
                token sep = file.getToken({token::comma, token::braceClose});
                if(sep.is(token::braceClose)) break;
                inner = file.getToken();
            }
        } else {
            // grammarRule: must be {VerbRef, {pattern}} — not nested
            if(peek2.is(token::braceOpen))
                parsingError(format("grammarRule '{0}' takes a single rule initializer; use array<grammarRule> for multiple rules", memberName));
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = memberName;
            rd.type = languageService.getType("grammarrule");
            parseGrammarRuleInit(rd);
            gtd.rules.push_back(&rd);
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            gtd.grammarLines.push_back(gl);
        }

        // Consume optional semicolon after member declaration
        if(file.peekToken().is(token::endStatement)) file.getToken();

        tok = file.getToken();
    }

    languageService.globals.push_back(&gtd);
    return false;
}

// After parseGrammarLineContent has consumed the inner `}` of a `{Verb, {pattern}}` literal,
// peek for an optional third positional element — `, priority` — and return it. Returns
// `defaultPriority` (caller-supplied; typically BLR's `class verb` default of 10) when no
// third element is present.
int bglParser::parseOptionalGrammarRulePriority(int defaultPriority){
    if(!file.peekToken().is(token::comma)) return defaultPriority;
    file.getToken();  // consume the comma
    token prioTok = file.getToken();
    try { return stoi(prioTok.value); }
    catch(...) {
        parsingError(format("grammarRule priority: expected integer literal, got '{0}'",
            prioTok.originalValue.empty() ? prioTok.value : prioTok.originalValue));
    }
    return defaultPriority;
}

// Parse a single grammar line's content: trigger word + pattern tokens.
// Assumes the opening '{' has already been consumed.
// Returns a grammarLine with verbWord and I6-ready patternTokens.
grammarLine bglParser::parseGrammarLineContent(){
    auto escDictWord = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch; return e;
    };
    auto makeI6Word = [&escDictWord](const token& t) -> string {
        string e = escDictWord(t.value);
        if(t.isPlural) return "'" + e + "/p'";
        if(e.size() == 1) return "'" + e + "//'";
        return "'" + e + "'";
    };

    grammarLine line;

    // First token: the verb trigger word — a dict word, with optional |-alternation for
    // multi-trigger lines (e.g. `.type|.enter|.put`). Extras land in additionalVerbWords;
    // they emit as one combined `verb 'w1' 'w2' 'w3' ...` directive when all are first-
    // occurrence, or fan out to per-trigger directives otherwise (see emitGrammarLines).
    token trigger = file.getToken(eTokenType::dictionaryWord);
    line.verbWord = trigger.value;   // raw word, e.g. "put"
    while(file.peekToken().is("|")) {
        file.getToken();  // consume '|'
        token alt = file.getToken(eTokenType::dictionaryWord);
        line.additionalVerbWords.push_back(alt.value);
    }

    // Remaining pattern tokens, comma-separated until }
    token tok = file.getToken({token::comma, token::braceClose});
    while(tok.is(token::comma)){
        // Optional opening paren — syntactic sugar for a dict-word alternatives group
        bool hasParens = file.peekToken().is("(");
        if(hasParens) file.getToken();  // consume '('

        token pt = file.getToken({eTokenType::dictionaryWord, eTokenType::identifier, eTokenType::dataType});

        if(pt.is(eTokenType::dictionaryWord)){
            // Dict word — may be chained with | alternatives: .word1 | .word2 | .word3
            string i6tok = makeI6Word(pt);
            while(file.peekToken().is("|")){
                file.getToken();  // consume '|'
                token alt = file.getToken(eTokenType::dictionaryWord);
                i6tok += "/" + makeI6Word(alt);
            }
            if(hasParens) file.getToken(")");  // consume ')'
            line.patternTokens.push_back(i6tok);
        } else {
            if(hasParens)
                parsingError("Parenthesized grammar group must contain dictionary word alternatives (.word | .word2 | ...)");

            // identifier or dataType in grammar pattern
            string tokenStr = pt.value;
            string display = pt.originalValue.empty() ? pt.value : pt.originalValue;

            // Resolve declared type. Grammar-line context: prefer enum-value matches
            // (e.g. `extern enum grammarToken { noun, held, ... }`) over global decls,
            // because some grammar-token names (`noun`) shadow extern globals of the same
            // name (the I6 runtime `noun` object). In a grammar line position the token
            // meaning always wins; outside grammar lines the global wins via normal
            // ambiguity resolution.
            string resolvedType;
            for(typeDef* t : languageService.objectTypes){
                auto* ed = dynamic_cast<enumDef*>(t);
                if(!ed) continue;
                bool found = false;
                for(enumValueDef* v : ed->namedValues)
                    if(v->name == tokenStr){
                        resolvedType = ed->name;
                        tokenStr = ed->isExternal ? v->name : to_string(v->value);
                        found = true; break;
                    }
                if(found) break;
            }
            if(resolvedType.empty()){
                for(typeDef* g : languageService.globals){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == tokenStr){
                            resolvedType = vd->type.name;
                            if(!vd->i6name.empty()) tokenStr = vd->i6name;
                            break;
                        }
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == tokenStr){ resolvedType = "function"; break; }
                }
            }

            // `reverse` pseudo-token — action-target modifier, not a pattern token.
            // Must be the LAST element in the line literal (next token must be `}`).
            // Sets line.isReverse and skips push_back so it doesn't end up in the I6 pattern.
            if(resolvedType == "grammartoken" && tokenStr == "reverse"){
                token next = file.peekToken();
                if(next.isNot(token::braceClose))
                    parsingError("'reverse' must be the last element in a grammar line literal — no tokens may follow it");
                line.isReverse = true;
                tok = file.getToken({token::comma, token::braceClose});  // consume `}`
                break;
            }

            if(file.peekToken().is("(")){
                // NOUN(Routine) / SCOPE(Routine) — parameterized grammar token
                if(resolvedType != "grammartoken")
                    parsingError(format("'{0}' in grammar pattern: only grammarToken types can be parameterized with (Routine)", display));
                file.getToken();  // consume '('
                token routine = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(")");  // consume ')'
                functionDef* rfd = nullptr;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == routine.value){ rfd = fd; break; }
                if(!rfd)
                    parsingError(format("'{0}' in '{1}({0})' does not name a declared global function", routine.value, display));
                if(rfd->returnType.name != "bool" && rfd->returnType.name != "ebool")
                    parsingError(format("'{0}' in '{1}({0})' must return bool (returns '{2}')", routine.value, display, rfd->returnType.name));
                tokenStr = pt.value + "=" + routine.value;  // I6 form: noun=Routine
            } else {
                // bare token: must be a declared grammarToken, attribute, or global function
                if(resolvedType != "grammartoken" && resolvedType != "attribute" && resolvedType != "function")
                    parsingError(format("'{0}' in grammar pattern is not a declared grammarToken, attribute, or global function", display));
            }
            line.patternTokens.push_back(tokenStr);
        }
        tok = file.getToken({token::comma, token::braceClose});
    }
    // tok is now } (closing brace of this line)
    return line;
}

// Parse a grammar line list: { {.put, held, .on, noun}, {.hang, held, .on, noun} }
// First token of each line is the verb trigger word (must be a dictionaryWord).
// Remaining tokens are pattern tokens: dict words become I6 'word', identifiers stay as-is.
//
// Single-line shorthand ("pretty lie"): when the first content token after the outer `{`
// is a dictionaryWord, the entire outer braces are treated as the one and only grammar
// line — `grammar = {.give, held, .to, creature}` desugars to
// `grammar = { {.give, held, .to, creature} }`. Mirrors the existing extern-verb body
// shorthand (parseObjectBody) so the same shape works at any grammar-list site.
vector<grammarLine> bglParser::parseGrammarLines(){
    vector<grammarLine> result;

    file.getToken(token::braceOpen);   // outer {

    // Single-line shorthand: outer `{` directly followed by a dictionaryWord means the
    // outer braces ARE the line literal. parseGrammarLineContent reads from the current
    // position (just past `{`) and consumes through the matching `}` — which is the
    // outer close here.
    if(file.peekToken().is(eTokenType::dictionaryWord)){
        result.push_back(parseGrammarLineContent());
        return result;
    }

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside grammar — missing closing '}'");
        tok.assert(token::braceOpen, "Expected '{' to start a grammar line");
        result.push_back(parseGrammarLineContent());

        // After }, expect , (more lines) or } (end of list)
        tok = file.getToken({token::comma, token::braceClose});
        if(tok.is(token::comma))
            tok = file.getToken();   // either { for next line, or } for end
    }
    return result;
}

bglParser parser;
