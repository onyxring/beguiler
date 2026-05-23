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
#include "bglParserHelpers.h"

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

// Build a mangled I6 property name for an overloaded object/class method, e.g.
// `getAll(func<bool,object>)` → `getAll_1_func`. Format: <name>_<arity> followed by
// `_<paramTypeName>` for each parameter. Generic suffixes are stripped so `func<...>`
// and `array<T>` collapse to `func` / `array` — overload disambiguation runs on the
// outer type, not template args. Used by mangleOverloadSet below and (via the same
// helper) by call-site emission paths.
static string mangleObjectMethodName(functionDef* fd){
    string safe = fd->name + "_" + to_string(fd->params.size());
    for(paramDef* p : fd->params){
        string t = p->type.name;
        size_t lt = t.find('<');
        if(lt != string::npos) t = t.substr(0, lt);
        safe += "_";
        safe += t.empty() ? "var" : t;
    }
    return safe;
}

// Helper: mangle the i6name of every non-emitter member of an overload set on `members`.
// Members already carrying an i6name (e.g. operator overloads, which were mangled at parse
// time) are preserved. Returns whether any member was newly mangled.
static void mangleOverloadSet(vector<typeMember*>& members, const string& methodName){
    vector<functionDef*> group;
    for(typeMember* m : members)
        if(auto* fd = dynamic_cast<functionDef*>(m))
            if(fd->name == methodName && !fd->isEmitter && !fd->isPrePassStub)
                group.push_back(fd);
    if(group.size() < 2) return;
    for(functionDef* fd : group)
        if(fd->i6name.empty())
            fd->i6name = mangleObjectMethodName(fd);
}

void bglParser::assignObjectMethodOverloadMangling(){
    // Walk every object and class; mangle every same-name non-emitter method group.
    // Emitters are inlined at call sites — they don't emit as I6 properties, so they can't
    // collide. This pass is the safety net for overload sets that no call site references
    // (the call-site path mangles eagerly via mangleOverloadSetForReceiver).
    auto manglePerType = [](vector<typeMember*>& members){
        set<string> seen;
        for(typeMember* m : members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(!fd->isEmitter && !fd->isPrePassStub && seen.insert(fd->name).second)
                    mangleOverloadSet(members, fd->name);
    };
    for(typeDef* g : languageService.globals){
        if(auto* od = dynamic_cast<objectDef*>(g))       manglePerType(od->members);
        else if(auto* cd = dynamic_cast<classDef*>(g))   manglePerType(cd->members);
    }
}


void bglParser::synthesizeParamBackings(functionDef& funcDef, const string& classContext){
    // operator= IS the field-copy machinery — its body is what the value-semantic
    // copy-in dispatches to. If we synthesized param backing for operator=, the
    // copy-in would dispatch operator= on its own param, recursing infinitely.
    // Skip operator= methods entirely.
    if(funcDef.name == "=") return;
    // Emitter routines are inlined at every call site — they have no I6 routine
    // and no param-binding step. Param backing only makes sense for real routines.
    if(funcDef.isEmitter) return;
    // Extern declarations are bound to existing I6 routines — Beguile doesn't get
    // to insert copy-in code at their entry.
    if(funcDef.isExternal) return;

    for(paramDef* p : funcDef.params){
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(p->type.name));
        if(!cls) continue;
        if(!cls->isByVal) continue;  // class-level opt-in is the gate

        // Look up operator=(SameType) for the entry copy-in, then var-wildcard fallback.
        // Required for byVal classes when used as a param — error if missing.
        functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name==p->type.name;
        }));
        if(!assignOp) assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="var";
        }));
        if(!assignOp){
            parsingError(format("byVal class '{0}' used as parameter '{1}' has no operator=. Declare 'operator =' on the class to define copy-in semantics for value-typed parameters.",
                typeDisplayName(p->type.name), p->name));
            continue;
        }
        if(!assignOp->isEmitter && assignOp->i6name.empty())
            assignOp->i6name = mangleOperatorName(assignOp->name);
        // Synthesize a global I6 object backing. Name includes the class context (if any)
        // so same-method-name across different classes doesn't collide:
        //   top-level fn `f` with param `p` → `_bglParam_f_p`
        //   class `Foo` method `f` with param `p` → `_bglParam_Foo_f_p`
        string backingName = classContext.empty()
            ? "_bglParam_" + funcDef.name + "_" + p->name
            : "_bglParam_" + classContext + "_" + funcDef.name + "_" + p->name;
        variableDeclaration* backing = new variableDeclaration();
        backing->name = backingName;
        backing->displayName = backingName;
        backing->type = p->type;
        backing->src = funcDef.src;
        languageService.registerInstance(*backing);
        p->i6name = backingName;
        p->isClassParamWithBacking = true;
    }
}

void bglParser::mangleOverloadSetForReceiver(const string& receiverTypeName, const string& methodName){
    if(receiverTypeName.empty() || methodName.empty()) return;
    // Strip a generic suffix on the receiver type name (`array<int>` → `array`) since
    // overload disambiguation runs on the outer type. Mirrors what bindMethodCall does
    // for templated receivers.
    string base = receiverTypeName;
    auto lt = base.find('<');
    if(lt != string::npos) base = base.substr(0, lt);
    typeDef& td = languageService.getType(base);
    if(auto* od = dynamic_cast<objectDef*>(&td))      mangleOverloadSet(od->members, methodName);
    else if(auto* cd = dynamic_cast<classDef*>(&td))  mangleOverloadSet(cd->members, methodName);
}

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
    { return processClassDeclaration(t[0], q.isExtern, q.isExtend, q.isEmitter, q.isAlias, t[1], q.isByVal); }
bool bglParser::processGrammar(vector<token>& t, Qualifiers&, abstractObject&)
    { return processGrammarDeclaration(t[1]); }
bool bglParser::processArray(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processArrayDeclarationFromGeneric(t[0], q, c); }
bool bglParser::processRoutine(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processRoutineDeclaration(t[0], t[1], c, q.isExtern, q.isEmitter, q.isReplace); }
bool bglParser::processObject(vector<token>& t, Qualifiers& q, abstractObject&)
    { t[0] = consumeTypeToken(t[0]); return processObjectDeclaration(t[0], t[1], q.isExtern, "", "", true, q.isEmitter); }
bool bglParser::processVariable(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processVariableDeclaration(t[0], t[1], t[2], c, q.isExtern, q.isConst, "", q.isRef); }
bool bglParser::processTypedObject(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processTypedObjectDeclaration(t[0], t[1], t[3], q, c); }
bool bglParser::processAliased(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processAliasedDeclaration(t[0], t[1], t[3], q, c); }

// ── Statement handlers (code-block scope) ───────────────────────────────────
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
        return processVariableDeclaration(typeTok, name, symbol, ctx, q.isExtern, q.isConst, i6alias, q.isRef);
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

// Returns true if ALL execution paths through the block are guaranteed to return.
// Criteria:
//   - An unconditional returnStatement at the top level of the block
//   - An if-else where both the then-block and else-block all-paths-return
//   - A switch that has a default case and every case body all-paths-returns
// Loops (for/while/do) are NOT treated as guaranteed — the body may not execute.

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


// Format a function signature for error messages: "name(type1 p1, type2 p2) → returnType"
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


// True if a class has at least one stored (non-emitter, non-static, non-attribute, non-grammar)
// data field declared directly on it — i.e. a field whose value would need to be copied for
// a value-semantics assignment to be meaningful. Used by the operator= dispatch paths to
// distinguish "this class carries state worth copying" from "this class is interface-like
// (only emitter methods)". Own-members-only by design.

// True if the class participates in the I6 world tree — direct or transitive inheritance
// from `object`. Tree-citizen classes use reference semantics by convention (`Room a = b;`
// means "a now refers to the same room as b," not "copy b's fields into a"); their stored
// fields live as I6 properties on the underlying object and are never field-copied. This
// gates the value-semantics-operator= error so it fires only for plain (non-tree) classes.
// The hardcoded "object" name is the same coupling flagged in
// [[compiler-blr-coupling-audit-2026-05-12]] item #1; revisit when the world-tree/utility
// split lands.



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
        else if(tok.is("ref"))                     { q.isRef      = true; advance(); }
        else if(tok.is("byval"))                   { q.isByVal    = true; advance(); }
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

// whose type is a registered class. Returns the flat type name or "".
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

// Supports: grammarRuleList, attributeList, array<T>.


bglParser parser;
