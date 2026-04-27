// lspServer.cpp — Language Server Protocol implementation for Beguile
// Standalone module. To remove: delete this file, lspServer.h, json.hpp,
// and the --lsp check in beguiler.cpp.

// Include json.hpp first, then undef assert macro before beguile headers
// (json.hpp pulls in <cassert> which defines assert() as a macro,
// conflicting with token::assert() method name)
#include "json.hpp"
#undef assert

#include "lspServer.h"
#include "bglParser.h"
#include "bglLanguageService.h"
#include "settings.h"
#include "typeDef.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <filesystem>
#include <algorithm>
#include <climits>

using namespace std;

// External references to the compiler's global state
extern bglParser parser;
extern bglLanguageService languageService;
extern beguilerSettingsDef beguilerSettings;

LspServer::LspServer() {}

//=============================================================================
// JSON-RPC Message I/O
//=============================================================================

json LspServer::readMessage() {
    // LSP uses Content-Length header followed by \r\n\r\n then JSON body
    string line;
    int contentLength = -1;
    while(getline(cin, line)) {
        if(cin.eof()) return nullptr;
        // Strip trailing \r
        if(!line.empty() && line.back() == '\r') line.pop_back();
        if(line.empty()) break;  // empty line = end of headers
        if(line.rfind("Content-Length:", 0) == 0) {
            contentLength = stoi(line.substr(15));
        }
    }
    if(contentLength < 0 || cin.eof()) return nullptr;

    string body(contentLength, '\0');
    cin.read(&body[0], contentLength);
    if(cin.eof() || cin.fail()) return nullptr;
    return json::parse(body, nullptr, false);
}

void LspServer::sendMessage(const json& msg) {
    string body = msg.dump();
    cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    cout.flush();
}

void LspServer::sendResponse(const json& id, const json& result) {
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    sendMessage(msg);
}

void LspServer::sendError(const json& id, int code, const string& message) {
    json msg = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
    sendMessage(msg);
}

void LspServer::sendNotification(const string& method, const json& params) {
    json msg = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    };
    sendMessage(msg);
}

//=============================================================================
// Main Loop
//=============================================================================

void LspServer::run() {
    // Redirect stderr for logging (stdout is the LSP channel)
    cerr << "Beguile LSP server starting..." << endl;

    while(true) {
        json msg = readMessage();
        if(msg.is_null()) break;  // EOF
        handleMessage(msg);
        if(shutdownRequested) break;
    }

    cerr << "Beguile LSP server exiting." << endl;
}

void LspServer::handleMessage(const json& msg) {
    string method = msg.value("method", "");
    bool hasId = msg.contains("id");

    if(hasId && !method.empty()) {
        // Request (has id + method)
        json id = msg["id"];
        json params = msg.value("params", json::object());

        if(method == "initialize")
            sendResponse(id, handleInitialize(params));
        else if(method == "shutdown")
            handleShutdown(id);
        else if(method == "textDocument/hover")
            sendResponse(id, handleHover(params));
        else if(method == "textDocument/completion")
            sendResponse(id, handleCompletion(params));
        else if(method == "textDocument/definition")
            sendResponse(id, handleDefinition(params));
        else if(method == "textDocument/documentSymbol")
            sendResponse(id, handleDocumentSymbol(params));
        else if(method == "textDocument/signatureHelp")
            sendResponse(id, handleSignatureHelp(params));
        else if(method == "textDocument/references")
            sendResponse(id, handleReferences(params));
        else if(method == "textDocument/rename")
            sendResponse(id, handleRename(params));
        else if(method == "textDocument/semanticTokens/full")
            sendResponse(id, handleSemanticTokensFull(params));
        else if(method == "workspace/symbol")
            sendResponse(id, handleWorkspaceSymbol(params));
        else
            sendError(id, -32601, "Method not found: " + method);

    } else if(!method.empty()) {
        // Notification (no id)
        json params = msg.value("params", json::object());

        if(method == "initialized") {
            // Client acknowledged initialization — nothing to do
        } else if(method == "textDocument/didOpen")
            handleDidOpen(params);
        else if(method == "textDocument/didChange")
            handleDidChange(params);
        else if(method == "textDocument/didClose")
            handleDidClose(params);
        else if(method == "exit") {
            shutdownRequested = true;
        }
        // Unknown notifications are silently ignored per spec
    }
}

//=============================================================================
// LSP Lifecycle
//=============================================================================

json LspServer::handleInitialize(const json& params) {
    initialized = true;
    cerr << "LSP initialized." << endl;

    json capabilities = {
        {"textDocumentSync", {
            {"openClose", true},
            {"change", 1}  // 1 = Full content on change
        }},
        {"hoverProvider", true},
        {"completionProvider", {
            {"triggerCharacters", {".", "="}}
        }},
        {"definitionProvider", true},
        {"documentSymbolProvider", true},
        {"signatureHelpProvider", {
            {"triggerCharacters", {"(", ","}}
        }},
        {"referencesProvider", true},
        {"renameProvider", true},
        {"semanticTokensProvider", {
            {"legend", {
                {"tokenTypes", {
                    "namespace",    // 0
                    "type",         // 1
                    "class",        // 2
                    "enum",         // 3
                    "interface",    // 4 — used for emitter classes
                    "struct",       // 5
                    "typeParameter",// 6
                    "parameter",    // 7
                    "variable",     // 8
                    "property",     // 9
                    "enumMember",   // 10
                    "event",        // 11
                    "function",     // 12
                    "method",       // 13
                    "macro",        // 14 — directives
                    "keyword",      // 15
                    "modifier",     // 16
                    "comment",      // 17
                    "string",       // 18
                    "number",       // 19
                    "regexp",       // 20
                    "operator"      // 21
                }},
                {"tokenModifiers", {
                    "declaration",  // 0
                    "definition",   // 1
                    "readonly",     // 2
                    "static",       // 3
                    "deprecated",   // 4
                    "abstract",     // 5
                    "defaultLibrary"// 6
                }}
            }},
            {"full", true}
        }},
        {"workspaceSymbolProvider", true}
    };

    return {
        {"capabilities", capabilities},
        {"serverInfo", {
            {"name", "beguile-lsp"},
            {"version", "0.1"}
        }}
    };
}

void LspServer::handleShutdown(const json& id) {
    sendResponse(id, nullptr);
    shutdownRequested = true;
}

//=============================================================================
// Document Sync
//=============================================================================

void LspServer::handleDidOpen(const json& params) {
    string uri = params["textDocument"]["uri"];
    string text = params["textDocument"]["text"];
    openDocuments[uri] = text;
    // Detect mode by URI extension. .inf-mode documents are I6-primary with #bgl{} islands.
    DocMode mode = (uri.size() >= 4 && uri.compare(uri.size() - 4, 4, ".inf") == 0)
                   ? DocMode::Inf : DocMode::Bgl;
    documentModes[uri] = mode;
    if(mode == DocMode::Inf)
        documentBglRegions[uri] = findBglRegions(text);
    parseDocument(uri);
    publishDiagnostics(uri);
    publishInactiveRegions(uri);
}

void LspServer::handleDidChange(const json& params) {
    string uri = params["textDocument"]["uri"];
    // Full sync mode: contentChanges[0].text is the full document
    if(params.contains("contentChanges") && !params["contentChanges"].empty()) {
        openDocuments[uri] = params["contentChanges"][0]["text"];
    }
    // Re-scan #bgl regions on every change (cheap; document is in memory).
    auto it = documentModes.find(uri);
    if(it != documentModes.end() && it->second == DocMode::Inf)
        documentBglRegions[uri] = findBglRegions(openDocuments[uri]);
    resetAndReparse(uri);
}

void LspServer::handleDidClose(const json& params) {
    string uri = params["textDocument"]["uri"];
    openDocuments.erase(uri);
    documentDiagnostics.erase(uri);
    documentParsePaths.erase(uri);
    documentModes.erase(uri);
    documentBglRegions.erase(uri);
    // Clear diagnostics on close
    sendNotification("textDocument/publishDiagnostics", {
        {"uri", uri},
        {"diagnostics", json::array()}
    });
    // Also clear inactive-region overlay
    sendNotification("beguile/inactiveRegions", {
        {"uri", uri},
        {"ranges", json::array()}
    });
    // Reset parser/language service state so closed-document symbols don't leak into later
    // parses (e.g. reopening a file that declares the same class, or switching between probe
    // files in a smoke test). Re-parse any remaining open documents to rebuild state.
    parser.reset();
    languageService.reset();
    for(auto& [openUri, _] : openDocuments) parseDocument(openUri);
}

//=============================================================================
// .inf-mode polyglot support
//=============================================================================

// Scan the document text for #bgl{...} (multi-line) and #bgl ...; (single-line, terminated
// by newline) regions. Mirrors the lexer's getRawTextUntilCloseOrBgl logic but operates on
// a string in memory. Skips //, "..." string, and '...' char/dict literals so a '#bgl' inside
// a comment or string isn't a false positive. Brace-balanced for the multi-line form.
std::vector<LspServer::BglRegion> LspServer::findBglRegions(const std::string& docText) {
    std::vector<BglRegion> regions;
    int n = (int)docText.size();
    int i = 0;
    while(i < n){
        char c = docText[i];
        // Skip // line comments
        if(c == '/' && i+1 < n && docText[i+1] == '/'){
            while(i < n && docText[i] != '\n') i++;
            continue;
        }
        // Skip I6 ! line comments
        if(c == '!'){
            while(i < n && docText[i] != '\n') i++;
            continue;
        }
        // Skip strings — terminate at next "
        if(c == '"'){
            i++;
            while(i < n && docText[i] != '"') i++;
            if(i < n) i++;
            continue;
        }
        // Skip char/dict literals — terminate at next '
        if(c == '\''){
            i++;
            while(i < n && docText[i] != '\'') i++;
            if(i < n) i++;
            continue;
        }
        // Detect #bgl
        if(c == '#' && i+3 < n && docText[i+1] == 'b' && docText[i+2] == 'g' && docText[i+3] == 'l'){
            // Word-boundary check: char after 'l' must not extend the identifier
            int afterTag = i + 4;
            char b = (afterTag < n) ? docText[afterTag] : '\0';
            if(isalnum((unsigned char)b) || b == '_'){ i++; continue; }
            // Skip horizontal whitespace looking for '{'
            int p = afterTag;
            while(p < n && (docText[p] == ' ' || docText[p] == '\t')) p++;
            if(p < n && docText[p] == '{'){
                // Multi-line: brace-balanced scan from after '{' to matching '}'
                int regionStart = p + 1;
                int depth = 1;
                int q = p + 1;
                while(q < n && depth > 0){
                    char x = docText[q];
                    if(x == '/' && q+1 < n && docText[q+1] == '/'){
                        while(q < n && docText[q] != '\n') q++;
                        continue;
                    }
                    if(x == '"'){ q++; while(q < n && docText[q] != '"') q++; if(q < n) q++; continue; }
                    if(x == '\''){ q++; while(q < n && docText[q] != '\'') q++; if(q < n) q++; continue; }
                    if(x == '{') depth++;
                    else if(x == '}'){ depth--; if(depth == 0){ regions.push_back({regionStart, q}); q++; break; } }
                    q++;
                }
                i = q;
            } else {
                // Single-line: from after #bgl up to end of line
                int regionStart = afterTag;
                int q = afterTag;
                while(q < n && docText[q] != '\n') q++;
                regions.push_back({regionStart, q});
                i = q;
            }
            continue;
        }
        i++;
    }
    return regions;
}

// Convert (line, col) — both 0-based per LSP spec — to an absolute character offset.
int LspServer::positionToOffset(const std::string& docText, int line, int col) {
    int curLine = 0, i = 0, n = (int)docText.size();
    while(i < n && curLine < line){
        if(docText[i] == '\n') curLine++;
        i++;
    }
    return i + col;
}

// True when the request at (line, col) should be served. For .bgl-mode docs: always true.
// For .inf-mode docs: only true when the position is inside a #bgl region.
bool LspServer::requestAllowedAt(const std::string& uri, int line, int col) {
    auto modeIt = documentModes.find(uri);
    if(modeIt == documentModes.end() || modeIt->second != DocMode::Inf) return true;
    auto regionsIt = documentBglRegions.find(uri);
    if(regionsIt == documentBglRegions.end()) return false;
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return false;
    int offset = positionToOffset(docIt->second, line, col);
    for(const BglRegion& r : regionsIt->second)
        if(offset >= r.startOffset && offset < r.endOffset) return true;
    return false;
}

//=============================================================================
// Compilation & Diagnostics
//=============================================================================

void LspServer::parseDocument(const string& uri) {
    string path = uriToPath(uri);
    documentDiagnostics.clear();

    // Write the in-memory content to a temp file for the parser
    // (the parser reads from the filesystem — LSP sends content via didOpen/didChange)
    auto docIt = openDocuments.find(uri);
    string tempPath = path;
    bool useTempFile = false;
    if(docIt != openDocuments.end()) {
        // If the file doesn't exist on disk or we have newer content, write a temp file
        tempPath = path + ".lsp_tmp";
        ofstream tmp(tempPath);
        if(tmp.is_open()) {
            tmp << docIt->second;
            tmp.close();
            useTempFile = true;
        } else {
            tempPath = path;  // fallback to original path
        }
    }

    // Suppress compiler stdout output
    streambuf* oldCout = cout.rdbuf();
    ostringstream nullStream;
    cout.rdbuf(nullStream.rdbuf());

    // Store the canonical path so documentSymbol can filter to this file
    try {
        documentParsePaths[uri] = filesystem::canonical(filesystem::absolute(tempPath)).string();
    } catch(...) {
        documentParsePaths[uri] = tempPath;
    }

    // Enable LSP error recovery mode — parser collects errors instead of halting
    parser.lspMode = true;
    parser.lspErrors.clear();
    parser.inactiveRegions.clear();

    // Pre-scan the document text for #beguilerSettings target so TARGET_ZCODE/TARGET_GLULX are
    // defined BEFORE preScanFile walks the core library (which uses them to pick a backend).
    // This mirrors beguiler::extractBlorbSettings used in normal compile mode.
    if(docIt != openDocuments.end()) {
        const string& text = docIt->second;
        size_t tagPos = 0;
        while((tagPos = text.find("#beguilerSettings", tagPos)) != string::npos) {
            size_t open = text.find('{', tagPos);
            if(open == string::npos) break;
            int depth = 1; size_t cur = open + 1;
            while(cur < text.size() && depth > 0) {
                if(text[cur] == '{') depth++;
                else if(text[cur] == '}') depth--;
                cur++;
            }
            string block = text.substr(open + 1, cur - open - 2);
            string lower = block;
            transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            size_t k = lower.find("target");
            if(k != string::npos) {
                size_t eq = lower.find('=', k + 6);
                if(eq != string::npos) {
                    size_t vs = lower.find_first_not_of(" \t\r\n", eq + 1);
                    if(vs != string::npos) {
                        size_t ve = lower.find_first_of(" \t\r\n;}", vs);
                        string val = lower.substr(vs, ve - vs);
                        if(val == "glulx")                              parser.defineSymbol("target_glulx");
                        else if(val.size() == 2 && val[0] == 'z' && isdigit(val[1]))
                            parser.defineSymbol("target_zcode", string(1, val[1]));
                    }
                }
            }
            tagPos = cur;
        }
    }

    vector<string> errors;
    try {
        parser.preScanFile(tempPath);
        parser.parseFile(tempPath);
    } catch(exception& e) {
        errors.push_back(e.what());
    } catch(...) {
        errors.push_back("Unknown compilation error");
    }
    // Collect any errors gathered during recovery
    for(const string& e : parser.lspErrors) errors.push_back(e);
    parser.lspMode = false;

    cout.rdbuf(oldCout);  // restore cout

    // Clean up temp file
    if(useTempFile) filesystem::remove(tempPath);

    documentDiagnostics[uri] = errors;
}

void LspServer::publishDiagnostics(const string& uri) {
    json diagnostics = json::array();

    auto it = documentDiagnostics.find(uri);
    if(it != documentDiagnostics.end()) {
        for(const string& err : it->second) {
            // Parse error message format: "file:line:col: error: message"
            // or "file:line:col: warning: message"
            int severity = 1;  // 1=Error, 2=Warning
            string message = err;
            int line = 0, col = 0;

            // Try to extract line/col from the error string
            size_t firstColon = err.find(':');
            if(firstColon != string::npos) {
                size_t secondColon = err.find(':', firstColon + 1);
                if(secondColon != string::npos) {
                    size_t thirdColon = err.find(':', secondColon + 1);
                    if(thirdColon != string::npos) {
                        try {
                            line = stoi(err.substr(firstColon + 1, secondColon - firstColon - 1)) - 1;
                            col = stoi(err.substr(secondColon + 1, thirdColon - secondColon - 1)) - 1;
                        } catch(...) {}
                        // Extract severity and message
                        string rest = err.substr(thirdColon + 1);
                        size_t msgStart = rest.find(':');
                        if(msgStart != string::npos) {
                            string sevStr = rest.substr(0, msgStart);
                            // trim
                            size_t s = sevStr.find_first_not_of(" ");
                            if(s != string::npos) sevStr = sevStr.substr(s);
                            if(sevStr == "warning") severity = 2;
                            message = rest.substr(msgStart + 1);
                            size_t ms = message.find_first_not_of(" ");
                            if(ms != string::npos) message = message.substr(ms);
                        }
                    }
                }
            }
            if(line < 0) line = 0;
            if(col < 0) col = 0;

            diagnostics.push_back({
                {"range", {
                    {"start", {{"line", line}, {"character", col}}},
                    {"end", {{"line", line}, {"character", col + 1}}}
                }},
                {"severity", severity},
                {"source", "beguile"},
                {"message", message}
            });
        }
    }

    sendNotification("textDocument/publishDiagnostics", {
        {"uri", uri},
        {"diagnostics", diagnostics}
    });
}

void LspServer::publishInactiveRegions(const string& uri) {
    // Look up the canonical parse path for this URI, then collect any ranges the parser
    // recorded for that file. Always send — an empty-ranges notification clears the client's
    // overlay when a previously-inactive branch becomes live.
    string path = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);

    // Deduplicate ranges: pre-scan and main parse each visit the file and record the same
    // branches. A simple set-of-pairs dedupe keeps the transmitted list minimal.
    std::set<std::pair<int,int>> uniq;
    auto collect = [&](const std::vector<bglParser::InactiveRegion>& v){
        for(const auto& r : v) uniq.insert({r.startLine0, r.endLine0Exclusive});
    };
    auto it = parser.inactiveRegions.find(path);
    if(it != parser.inactiveRegions.end()) collect(it->second);
    json ranges = json::array();
    for(auto& [s, e] : uniq) {
        ranges.push_back({
            {"start", {{"line", s}, {"character", 0}}},
            {"end",   {{"line", e}, {"character", 0}}}
        });
    }
    sendNotification("beguile/inactiveRegions", {
        {"uri", uri},
        {"ranges", ranges}
    });
}

//=============================================================================
// Shared symbol resolution — position-to-symbol lookup used by multiple handlers
//=============================================================================

// Look up the original-case display name for a type. Beguile lowercases names at registration,
// so for user-defined types we can recover the original via displayName on the typeDef.
// Handles classes, enums, and any other registered type that sets displayName.
static string typeDisplay(const string& typeName) {
    if(typeName.empty()) return typeName;
    typeDef& td = languageService.getType(typeName);
    if(!td.displayName.empty()) return td.displayName;
    return typeName;
}

// Find the function declared in the current file whose src.line is the largest value
// <= cursorLine + 1. Returns nullptr if no function contains the cursor.
// Walks global functions AND class/object member methods.
functionDef* LspServer::findEnclosingFunction(const string& uri, int cursorLine) {
    int cursorLine1Based = cursorLine + 1;
    string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
    functionDef* best = nullptr;
    int bestLine = 0;

    auto tryFn = [&](functionDef* fd) {
        if(!fd || fd->src.line == 0) return;
        if(fd->src.file != curFile) return;
        if(fd->src.line <= cursorLine1Based && fd->src.line > bestLine) {
            bestLine = fd->src.line;
            best = fd;
        }
    };

    // Global functions
    for(typeDef* g : languageService.globals) {
        if(auto* fd = dynamic_cast<functionDef*>(g)) tryFn(fd);
    }
    // Class member methods
    for(typeDef* t : languageService.objectTypes) {
        if(auto* cd = dynamic_cast<classDef*>(t))
            for(typeMember* m : cd->members)
                if(auto* fd = dynamic_cast<functionDef*>(m)) tryFn(fd);
    }
    // Object instance member methods
    for(typeDef* t : languageService.objectInstances) {
        if(auto* od = dynamic_cast<objectDef*>(t))
            for(typeMember* m : od->members)
                if(auto* fd = dynamic_cast<functionDef*>(m)) tryFn(fd);
    }
    return best;
}

// Returns true if (line, col) is inside an open '#beguilerSettings { ... }' block.
// The block isn't a real AST node (directive bodies are consumed as raw text by the pre-scanner),
// so symbol resolution for this context has to scan the document text directly. The scan is
// tolerant: case-insensitive tag match, handles cursors past end-of-line, supports multiple
// disjoint blocks by advancing past closed ones.
bool LspServer::isInBeguilerSettingsBlock(const string& uri, int line, int col) {
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return false;
    const string& docText = docIt->second;
    // Convert (line, col) to an absolute offset. Clamp a cursor past end-of-line to that line's '\n'.
    size_t cursorOffset = docText.size();
    int curLine = 0, curCol = 0;
    for(size_t i = 0; i < docText.size(); i++) {
        if(curLine == line && curCol == col) { cursorOffset = i; break; }
        if(docText[i] == '\n') {
            if(curLine == line) { cursorOffset = i; break; }
            curLine++; curCol = 0;
        } else curCol++;
    }
    string lowerText; lowerText.reserve(cursorOffset);
    for(size_t i = 0; i < cursorOffset; i++) lowerText += (char)tolower((unsigned char)docText[i]);
    const string tag = "#beguilersettings";
    size_t searchFrom = 0;
    while(true) {
        size_t tagPos = lowerText.find(tag, searchFrom);
        if(tagPos == string::npos) return false;
        size_t bracePos = lowerText.find('{', tagPos + tag.size());
        if(bracePos == string::npos) return false;
        int depth = 1;
        for(size_t i = bracePos + 1; i < lowerText.size(); i++) {
            if(lowerText[i] == '{') depth++;
            else if(lowerText[i] == '}') { depth--; if(depth == 0) break; }
        }
        if(depth > 0) return true;
        searchFrom = bracePos + 1;
    }
}

// Resolve a name at a cursor position to its declaration, walking enclosing scopes:
// 1. local variables in the enclosing function's body
// 2. parameters of the enclosing function
// 3. members of the enclosing class (if the function is inside one)
// 4. globals, classes, enum values, verbs
LspSymbolRef LspServer::resolveSymbol(const string& uri, int cursorLine, const string& loweredName) {
    LspSymbolRef out;
    int cursorLine1Based = cursorLine + 1;
    string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);

    // Tier 0: direct source-line match on class/object members in the current file.
    // Handles hover at a member declaration line (inside class body, not in a method).
    for(typeDef* t : languageService.objectTypes) {
        auto* cd = dynamic_cast<classDef*>(t);
        if(!cd) continue;
        for(typeMember* m : cd->members) {
            if(m->name != loweredName) continue;
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                if(vd->src.file == curFile && vd->src.line == cursorLine1Based) {
                    out.kind = LspSymbolRef::ClassMember;
                    out.enclosingClass = cd;
                    out.typeName = vd->type.name;
                    out.displayName = vd->displayName.empty() ? vd->name : vd->displayName;
                    out.declSrc = vd->src;
                    return out;
                }
            } else if(auto* mfd = dynamic_cast<functionDef*>(m)) {
                if(mfd->src.file == curFile && mfd->src.line == cursorLine1Based) {
                    out.kind = LspSymbolRef::ClassMember;
                    out.enclosingClass = cd;
                    out.typeName = mfd->returnType.name;
                    out.displayName = mfd->displayName.empty() ? mfd->name : mfd->displayName;
                    out.declSrc = mfd->src;
                    return out;
                }
            }
        }
    }
    // Tier 0b: same match against object instance bodies. Handles hover inside a `room foyer {
    // short_name = ...; }` initializer where the property assignment is stored as a member
    // variableDeclaration on the object with its own src.line. For definition, prefer jumping
    // to the owning class's member declaration rather than the instance override.
    auto findClassMemberSrc = [&](classDef* startCls, const string& memberName) -> sourceLocation {
        sourceLocation result;
        function<bool(classDef*)> walk = [&](classDef* c) -> bool {
            if(!c) return false;
            for(typeMember* m : c->members) {
                if(m->name != memberName) continue;
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)) { result = vd->src; return true; }
                if(auto* fd = dynamic_cast<functionDef*>(m)) { result = fd->src; return true; }
            }
            for(classDef* base : c->baseClasses) if(walk(base)) return true;
            return false;
        };
        walk(startCls);
        return result;
    };
    for(typeDef* t : languageService.objectInstances) {
        auto* od = dynamic_cast<objectDef*>(t);
        if(!od) continue;
        for(typeMember* m : od->members) {
            if(m->name != loweredName) continue;
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                if(vd->src.file == curFile && vd->src.line == cursorLine1Based) {
                    out.kind = LspSymbolRef::ClassMember;
                    out.enclosingClass = od->objectClass;
                    out.typeName = vd->type.name;
                    out.displayName = vd->displayName.empty() ? vd->name : vd->displayName;
                    sourceLocation clsSrc = findClassMemberSrc(od->objectClass, loweredName);
                    out.declSrc = clsSrc.line > 0 ? clsSrc : vd->src;
                    return out;
                }
            } else if(auto* mfd = dynamic_cast<functionDef*>(m)) {
                if(mfd->src.file == curFile && mfd->src.line == cursorLine1Based) {
                    out.kind = LspSymbolRef::ClassMember;
                    out.enclosingClass = od->objectClass;
                    out.typeName = mfd->returnType.name;
                    out.displayName = mfd->displayName.empty() ? mfd->name : mfd->displayName;
                    sourceLocation clsSrc = findClassMemberSrc(od->objectClass, loweredName);
                    out.declSrc = clsSrc.line > 0 ? clsSrc : mfd->src;
                    return out;
                }
            }
        }
    }

    // Tier 1: enclosing function locals and parameters
    functionDef* fn = findEnclosingFunction(uri, cursorLine);
    if(fn) {
        out.enclosingFunc = fn;
        // Locals first (they shadow parameters with the same name)
        if(auto* blk = dynamic_cast<statementBlock*>(fn->body)) {
            for(statement* s : blk->statements) {
                if(auto* vd = dynamic_cast<variableDeclaration*>(s)) {
                    if(vd->name == loweredName) {
                        out.kind = LspSymbolRef::Local;
                        out.typeName = vd->type.name;
                        out.displayName = vd->displayName.empty() ? vd->name : vd->displayName;
                        out.declSrc = vd->src;
                        return out;
                    }
                }
            }
        }
        // Parameters
        for(paramDef* p : fn->params) {
            if(p->name == loweredName) {
                out.kind = LspSymbolRef::Parameter;
                out.typeName = p->type.name;
                out.displayName = p->displayName.empty() ? p->name : p->displayName;
                out.declSrc = fn->src;  // paramDef has no src; fall back to function signature
                return out;
            }
        }
        // Tier 2: if the function is a class/object member, check the containing class's members
        // for a variable or method with the same name.
        auto findEnclosingType = [&](functionDef* target) -> std::pair<classDef*, objectDef*> {
            for(typeDef* t : languageService.objectTypes) {
                if(auto* cd = dynamic_cast<classDef*>(t))
                    for(typeMember* m : cd->members)
                        if(m == target) return {cd, nullptr};
            }
            for(typeDef* t : languageService.objectInstances) {
                if(auto* od = dynamic_cast<objectDef*>(t))
                    for(typeMember* m : od->members)
                        if(m == target) return {nullptr, od};
            }
            return {nullptr, nullptr};
        };
        auto [encCls, encObj] = findEnclosingType(fn);
        vector<typeMember*>* members = encCls ? &encCls->members : (encObj ? &encObj->members : nullptr);
        if(members) {
            std::function<bool(classDef*)> searchClass = [&](classDef* c) -> bool {
                if(!c) return false;
                for(typeMember* m : c->members) {
                    if(m->name != loweredName) continue;
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                        out.kind = LspSymbolRef::ClassMember;
                        out.enclosingClass = c;
                        out.typeName = vd->type.name;
                        out.displayName = vd->displayName.empty() ? vd->name : vd->displayName;
                        out.declSrc = vd->src;
                        return true;
                    }
                    if(auto* mfd = dynamic_cast<functionDef*>(m)) {
                        out.kind = LspSymbolRef::ClassMember;
                        out.enclosingClass = c;
                        out.typeName = mfd->returnType.name;
                        out.displayName = mfd->displayName.empty() ? mfd->name : mfd->displayName;
                        out.declSrc = mfd->src;
                        return true;
                    }
                }
                for(classDef* base : c->baseClasses) if(searchClass(base)) return true;
                return false;
            };
            for(typeMember* m : *members) {
                if(m->name == loweredName) {
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                        out.kind = LspSymbolRef::ClassMember;
                        out.enclosingClass = encCls;
                        out.typeName = vd->type.name;
                        out.displayName = vd->displayName.empty() ? vd->name : vd->displayName;
                        out.declSrc = vd->src;
                        return out;
                    }
                    if(auto* mfd = dynamic_cast<functionDef*>(m)) {
                        out.kind = LspSymbolRef::ClassMember;
                        out.enclosingClass = encCls;
                        out.typeName = mfd->returnType.name;
                        out.displayName = mfd->displayName.empty() ? mfd->name : mfd->displayName;
                        out.declSrc = mfd->src;
                        return out;
                    }
                }
            }
            // Walk base classes
            if(encCls) {
                for(classDef* base : encCls->baseClasses)
                    if(searchClass(base)) return out;
            }
        }
    }

    // Tier 2: globals, classes, verbs, enum values
    typeDef& td = languageService.getType(loweredName);
    if(auto* cd = dynamic_cast<classDef*>(&td)) {
        out.kind = LspSymbolRef::Global;
        out.typeName = cd->name;
        out.displayName = cd->displayName.empty() ? cd->name : cd->displayName;
        out.declSrc = cd->src;
        return out;
    }
    if(auto* ed = dynamic_cast<enumDef*>(&td)) {
        out.kind = LspSymbolRef::Global;
        out.typeName = ed->name;
        out.displayName = ed->displayName.empty() ? ed->name : ed->displayName;
        out.declSrc = ed->src;
        return out;
    }
    for(typeDef* g : languageService.globals) {
        if(g->name != loweredName) continue;
        out.kind = LspSymbolRef::Global;
        if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
            out.typeName = vd->type.name;
            out.displayName = vd->displayName.empty() ? vd->name : vd->displayName;
            out.declSrc = vd->src;
        } else if(auto* fd = dynamic_cast<functionDef*>(g)) {
            out.typeName = fd->returnType.name;
            out.displayName = fd->displayName.empty() ? fd->name : fd->displayName;
            out.declSrc = fd->src;
        } else if(auto* od = dynamic_cast<objectDef*>(g)) {
            out.typeName = od->objectClass ? od->objectClass->name : "object";
            out.displayName = od->displayName.empty() ? od->name : od->displayName;
            out.declSrc = od->src;
        }
        return out;
    }
    return out;
}

//=============================================================================
// Hover
//=============================================================================

json LspServer::handleHover(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    // .inf-mode gating: outside #bgl{} regions, no Beguile hover.
    if(!requestAllowedAt(uri, line, col)) return nullptr;

    string path = uriToPath(uri);

    // Read the line from the open document
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;

    // Extract the word at the cursor position
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    // Find the word boundaries at col
    if(col >= (int)lineText.size()) return nullptr;
    int start = col, end = col;
    while(start > 0 && (isalnum(lineText[start-1]) || lineText[start-1] == '_')) start--;
    while(end < (int)lineText.size() && (isalnum(lineText[end]) || lineText[end] == '_')) end++;
    string word = lineText.substr(start, end - start);
    if(word.empty()) return nullptr;

    // Check for dotted path: walk back through full owner chain (bgl.glulx.window → owner = "bgl.glulx")
    string ownerName;
    if(start > 0 && lineText[start - 1] == '.') {
        int i = start - 1; // at the '.'
        string path;
        while(i >= 0 && lineText[i] == '.'){
            i--;
            int segEnd = i + 1;
            while(i >= 0 && (isalnum(lineText[i]) || lineText[i] == '_')) i--;
            int segStart = i + 1;
            if(segStart >= segEnd) break;
            string seg = lineText.substr(segStart, segEnd - segStart);
            path = path.empty() ? seg : seg + "." + path;
        }
        ownerName = path;
    }

    // Resolve the type
    string lower = word;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    string typeInfo;

    // ── #beguilerSettings block: resolve bare identifiers as beguilerSettingsType members ──
    // The block isn't a real AST node, so the regular resolver won't find these. Handle it here
    // so hover on property names (LHS of assignment inside the block) shows the declared type.
    if(ownerName.empty() && isInBeguilerSettingsBlock(uri, line, col)) {
        classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilersettingstype"));
        if(schema) {
            for(typeMember* m : schema->members) {
                if(m->name != lower) continue;
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                    typeInfo = "beguilerSettingsType::" + typeDisplay(vd->type.name) + " " +
                               (vd->displayName.empty() ? vd->name : vd->displayName);
                    break;
                }
            }
            // Also consider enum-value hits (e.g. hover on 'Glulx' in 'target = Glulx')
            if(typeInfo.empty()) {
                string enumTypeName = languageService.getEnumType(lower);
                if(!enumTypeName.empty()) {
                    if(auto* ed = dynamic_cast<enumDef*>(&languageService.getType(enumTypeName))) {
                        for(enumValueDef* ev : ed->namedValues)
                            if(ev->name == lower) {
                                typeInfo = (ed->displayName.empty() ? ed->name : ed->displayName) + " " +
                                           (ev->displayName.empty() ? ev->name : ev->displayName);
                                break;
                            }
                    }
                }
            }
        }
    }

    // If we have a dotted path (owner.member), resolve the member
    if(typeInfo.empty() && !ownerName.empty()) {
        string ownerLower = ownerName;
        transform(ownerLower.begin(), ownerLower.end(), ownerLower.begin(), ::tolower);
        classDef* cls = nullptr;

        // Resolve owner to a class. Check scopes in order: enclosing-function locals/params,
        // then classes, then global objects, then global variables.
        LspSymbolRef ownerRef = resolveSymbol(uri, line, ownerLower);
        if(ownerRef.kind == LspSymbolRef::Local || ownerRef.kind == LspSymbolRef::Parameter)
            cls = dynamic_cast<classDef*>(&languageService.getType(ownerRef.typeName));
        if(!cls) {
            typeDef& td = languageService.getType(ownerLower);
            if(auto* cd = dynamic_cast<classDef*>(&td)) cls = cd;
        }
        if(!cls) {
            for(typeDef* g : languageService.globals)
                if(auto* od = dynamic_cast<objectDef*>(g))
                    if(od->name == ownerLower) { cls = od->objectClass; break; }
        }
        if(!cls) {
            for(typeDef* g : languageService.globals)
                if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                    if(vd->name == ownerLower) {
                        cls = dynamic_cast<classDef*>(&languageService.getType(vd->type.name));
                        break;
                    }
        }

        // Search class hierarchy for the member
        if(cls) {
            function<bool(classDef*)> findMember = [&](classDef* c) -> bool {
                if(!c) return false;
                for(typeMember* m : c->members) {
                    if(m->name == lower) {
                        if(auto* fd = dynamic_cast<functionDef*>(m)) {
                            typeInfo = typeDisplay(fd->returnType.name) + " " +
                                       (fd->displayName.empty() ? fd->name : fd->displayName);
                            if(!fd->isValueEmitter) {
                                typeInfo += "(";
                                for(size_t i = 0; i < fd->params.size(); i++) {
                                    if(i > 0) typeInfo += ", ";
                                    typeInfo += typeDisplay(fd->params[i]->type.name) + " " +
                                                (fd->params[i]->displayName.empty() ? fd->params[i]->name : fd->params[i]->displayName);
                                }
                                typeInfo += ")";
                            }
                            if(fd->isEmitter) typeInfo = "emitter " + typeInfo;
                        } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                            typeInfo = typeDisplay(vd->type.name) + " " +
                                       (vd->displayName.empty() ? vd->name : vd->displayName);
                        }
                        return true;
                    }
                }
                for(classDef* base : c->baseClasses)
                    if(findMember(base)) return true;
                return false;
            };
            findMember(cls);
        }

        // Also check object instance members (including namespace walking for dotted paths)
        if(typeInfo.empty()) {
            // Walk dotted owner path to find the target object
            objectDef* nsObj = nullptr;
            size_t ownerDot = ownerLower.find('.');
            if(ownerDot != string::npos){
                // Multi-level: walk the chain
                string head = ownerLower.substr(0, ownerDot);
                string rest = ownerLower.substr(ownerDot + 1);
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == head){ nsObj = od; break; }
                while(nsObj && !rest.empty()){
                    ownerDot = rest.find('.');
                    string seg = (ownerDot == string::npos) ? rest : rest.substr(0, ownerDot);
                    rest = (ownerDot == string::npos) ? "" : rest.substr(ownerDot + 1);
                    objectDef* next = nullptr;
                    for(typeMember* m : nsObj->members){
                        auto* vd = dynamic_cast<variableDeclaration*>(m);
                        if(!vd || vd->name != seg) continue;
                        string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                        if(!initName.empty())
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == initName){ next = od; break; }
                        if(!next)
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == vd->type.name){ next = od; break; }
                        break;
                    }
                    nsObj = next;
                }
            } else {
                // Single-level: direct object lookup
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == ownerLower){ nsObj = od; break; }
            }
            if(nsObj){
                for(typeMember* m : nsObj->members){
                    if(m->name == lower){
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                            if(vd->isAlias)
                                typeInfo = "type alias → " + typeDisplay(vd->type.name);
                            else
                                typeInfo = typeDisplay(vd->type.name) + " " + (vd->displayName.empty() ? vd->name : vd->displayName);
                        } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                            typeInfo = typeDisplay(fd->returnType.name) + " " + (fd->displayName.empty() ? fd->name : fd->displayName) + "(...)";
                        }
                        break;
                    }
                }
            }
        }
    }

    // Non-dotted: resolve as top-level identifier via shared scope resolver
    if(typeInfo.empty() && ownerName.empty()) {
        LspSymbolRef ref = resolveSymbol(uri, line, lower);
        if(ref.kind == LspSymbolRef::Local) {
            typeInfo = "local: " + typeDisplay(ref.typeName) + " " + ref.displayName;
        } else if(ref.kind == LspSymbolRef::Parameter) {
            typeInfo = "param: " + typeDisplay(ref.typeName) + " " + ref.displayName;
        } else if(ref.kind == LspSymbolRef::ClassMember) {
            string clsName = ref.enclosingClass ? (ref.enclosingClass->displayName.empty() ? ref.enclosingClass->name : ref.enclosingClass->displayName) : "";
            typeInfo = (clsName.empty() ? "member: " : clsName + "::") + typeDisplay(ref.typeName) + " " + ref.displayName;
        } else if(ref.kind == LspSymbolRef::Global) {
            // resolveSymbol set enough info; enrich with type/class specifics
            typeDef& td = languageService.getType(lower);
            if(auto* cd = dynamic_cast<classDef*>(&td)) {
                typeInfo = "class " + (cd->displayName.empty() ? cd->name : cd->displayName);
                if(cd->isEmitterClass) typeInfo = "emitter " + typeInfo;
                if(cd->isExternal) typeInfo = "extern " + typeInfo;
            } else if(auto* ed = dynamic_cast<enumDef*>(&td)) {
                typeInfo = (ed->isBnum ? "bnum " : "enum ") +
                           (ed->displayName.empty() ? ed->name : ed->displayName);
                if(ed->isExternal) typeInfo = "extern " + typeInfo;
            } else {
                for(typeDef* g : languageService.globals) {
                    if(g->name != lower) continue;
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
                        typeInfo = typeDisplay(vd->type.name) + " " +
                                   (vd->displayName.empty() ? vd->name : vd->displayName);
                    } else if(auto* fd = dynamic_cast<functionDef*>(g)) {
                        typeInfo = typeDisplay(fd->returnType.name) + " " +
                                   (fd->displayName.empty() ? fd->name : fd->displayName) + "(";
                        for(size_t i = 0; i < fd->params.size(); i++) {
                            if(i > 0) typeInfo += ", ";
                            typeInfo += typeDisplay(fd->params[i]->type.name) + " " +
                                        (fd->params[i]->displayName.empty() ? fd->params[i]->name : fd->params[i]->displayName);
                        }
                        typeInfo += ")";
                        if(fd->isEmitter) typeInfo = "emitter " + typeInfo;
                    } else if(auto* od = dynamic_cast<objectDef*>(g)) {
                        typeInfo = typeDisplay(od->objectClass ? od->objectClass->name : "object") + " " +
                                   (od->displayName.empty() ? od->name : od->displayName);
                    }
                    break;
                }
            }
        }
        // Check verbs
        if(typeInfo.empty()) {
            for(auto* v : languageService.verbs)
                if(v->name == lower) {
                    typeInfo = "verb " + (v->displayName.empty() ? v->name : v->displayName);
                    break;
                }
        }
        // Check enum values
        if(typeInfo.empty()) {
            string enumType = languageService.getEnumType(lower);
            if(!enumType.empty()) typeInfo = enumType + "." + word;
        }
    }

    if(typeInfo.empty()) return nullptr;

    return {
        {"contents", {
            {"kind", "markdown"},
            {"value", "```bgl\n" + typeInfo + "\n```"}
        }}
    };
}

//=============================================================================
// Completion (member completion after '.' + #beguilerSettings property completion)
//
// Return convention:
//   - json::array() when this handler IS authoritative for the context but has no suggestions
//     (dot-access on an unknown type, etc.). An empty array suppresses VS Code's word fallback.
//   - nullptr when the handler does NOT recognize the context — lets the client fall back to its
//     built-in word-based suggestions. Use this for bare-identifier completion, keywords, and
//     any position that isn't a member-access or a known structured-completion context.
//=============================================================================

json LspServer::handleCompletion(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    // .inf-mode gating: outside #bgl{} regions, defer to other providers / word fallback.
    if(!requestAllowedAt(uri, line, col)) return nullptr;

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;
    const string& docText = docIt->second;

    // Get the text before the cursor on this line
    istringstream stream(docText);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    // ── Phase order ──
    //   1. Dotted access (foo.|)           — return members of foo's type / enum values if foo is an enum
    //   2. Enum RHS in bs-block (prop = |) — return enum values for the property's declared type
    //   3. Bare identifier in bs-block     — return beguilerSettingsType property names
    //   4. Fallthrough                      — return nullptr (let client do word completion)
    // Dotted-access wins over bs-block member completion so `target = eTarget.` correctly
    // offers Glulx/Z3/Z5/Z8 instead of the 24 property names.

    bool insideBsBlock = isInBeguilerSettingsBlock(uri, line, col);

    // Suppress completion inside string literals — the user is typing prose.
    auto isInsideStringLiteral = [&]() -> bool {
        int limit = col;
        if(limit > (int)lineText.size()) limit = (int)lineText.size();
        bool inStr = false;
        for(int i = 0; i < limit; i++) {
            char c = lineText[i];
            if(c == '"' && (i == 0 || lineText[i-1] != '\\')) inStr = !inStr;
        }
        return inStr;
    };
    if(isInsideStringLiteral()) return nullptr;

    // ── Phase 1: dotted access ────────────────────────────────────────────
    // Detect `identifier.|` — if the char immediately before the cursor (skipping a partial
    // identifier in progress) is '.', we're completing a member of the prefix. This runs
    // regardless of whether we're inside a bs-block.
    auto findDotPrefix = [&](string& outPrefix) -> bool {
        int i = col - 1;
        if(i >= (int)lineText.size()) i = (int)lineText.size() - 1;
        // Skip a partial identifier the user is typing (e.g. 'G' in 'eTarget.G|')
        while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
        if(i < 0 || lineText[i] != '.') return false;
        // Walk back through the full dotted path: bgl.glulx.window → prefix = "bgl.glulx"
        string path;
        while(i >= 0 && lineText[i] == '.'){
            i--;
            int segEnd = i + 1;
            while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
            int segStart = i + 1;
            if(segStart >= segEnd) break;
            string seg = lineText.substr(segStart, segEnd - segStart);
            path = path.empty() ? seg : seg + "." + path;
        }
        if(path.empty()) return false;
        outPrefix = path;
        return true;
    };

    string dotPrefix;
    if(findDotPrefix(dotPrefix)) {
        string lower = dotPrefix;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Resolve the prefix to a type. Check in order: enclosing-function locals/params,
        // classes (including enums), global objects, global variables.
        classDef* cls = nullptr;
        enumDef*  ed  = nullptr;

        LspSymbolRef ownerRef = resolveSymbol(uri, line, lower);
        if(ownerRef.kind == LspSymbolRef::Local || ownerRef.kind == LspSymbolRef::Parameter) {
            typeDef& td = languageService.getType(ownerRef.typeName);
            cls = dynamic_cast<classDef*>(&td);
            if(!cls) ed = dynamic_cast<enumDef*>(&td);
        }
        if(!cls && !ed) {
            typeDef& td = languageService.getType(lower);
            cls = dynamic_cast<classDef*>(&td);
            if(!cls) ed = dynamic_cast<enumDef*>(&td);
        }
        if(!cls && !ed) {
            for(typeDef* g : languageService.globals)
                if(auto* od = dynamic_cast<objectDef*>(g))
                    if(od->name == lower) { cls = od->objectClass; break; }
        }
        if(!cls && !ed) {
            for(typeDef* g : languageService.globals)
                if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                    if(vd->name == lower) {
                        typeDef& td = languageService.getType(vd->type.name);
                        cls = dynamic_cast<classDef*>(&td);
                        if(!cls) ed = dynamic_cast<enumDef*>(&td);
                        break;
                    }
        }

        // Namespace object resolution: walk dotted path to find the target object
        // and show its members (including alias type members).
        if(!cls && !ed) {
            // Walk the dotted path: bgl.glulx → find _bglGlulx object
            objectDef* nsObj = nullptr;
            size_t dot = lower.find('.');
            if(dot != string::npos){
                string head = lower.substr(0, dot);
                string rest = lower.substr(dot + 1);
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == head){ nsObj = od; break; }
                while(nsObj && !rest.empty()){
                    dot = rest.find('.');
                    string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
                    rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                    objectDef* next = nullptr;
                    for(typeMember* m : nsObj->members){
                        auto* vd = dynamic_cast<variableDeclaration*>(m);
                        if(!vd || vd->name != seg) continue;
                        string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                        if(!initName.empty())
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == initName){ next = od; break; }
                        if(!next)
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == vd->type.name){ next = od; break; }
                        break;
                    }
                    nsObj = next;
                }
            } else {
                // Single segment — check if it's a global object
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == lower){ nsObj = od; break; }
            }
            if(nsObj){
                // Show namespace object's members: alias types, auto aliases, methods, properties
                json items = json::array();
                for(typeMember* m : nsObj->members){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                        string label = vd->displayName.empty() ? vd->name : vd->displayName;
                        if(vd->isAlias){
                            // Type alias: show as Class kind with the target type
                            string targetDisplay = vd->type.dName();
                            items.push_back({{"label", label}, {"kind", 7}, {"detail", "type alias → " + targetDisplay}});
                        } else {
                            // Value alias or property
                            string typeDisplay = vd->type.dName();
                            items.push_back({{"label", label}, {"kind", 6}, {"detail", typeDisplay}});
                        }
                    } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                        string label = fd->displayName.empty() ? fd->name : fd->displayName;
                        items.push_back({{"label", label}, {"kind", fd->isValueEmitter ? 6 : 2}, {"detail", fd->returnType.dName()}});
                    }
                }
                return items;
            }
        }

        // Enum prefix: return its named values.
        if(ed) {
            json items = json::array();
            string enumLabel = ed->displayName.empty() ? ed->name : ed->displayName;
            for(enumValueDef* ev : ed->namedValues) {
                items.push_back({
                    {"label", ev->displayName.empty() ? ev->name : ev->displayName},
                    {"kind", 20},  // EnumMember
                    {"detail", enumLabel}
                });
            }
            return items;
        }

        if(!cls) return json::array();

        // Collect class members walking the hierarchy.
        json items = json::array();
        function<void(classDef*)> collectMembers = [&](classDef* c) {
            if(!c) return;
            for(typeMember* m : c->members) {
                if(auto* fd = dynamic_cast<functionDef*>(m)) {
                    if(fd->isPrePassStub) continue;
                    int kind = fd->isValueEmitter ? 6 : 2;  // 6=Variable, 2=Method
                    string detail = fd->returnType.name;
                    string label = fd->displayName.empty() ? fd->name : fd->displayName;
                    if(!fd->isValueEmitter) {
                        label += "(";
                        for(size_t i = 0; i < fd->params.size(); i++) {
                            if(i > 0) label += ", ";
                            label += fd->params[i]->type.name;
                        }
                        label += ")";
                    }
                    items.push_back({{"label", label}, {"kind", kind}, {"detail", detail}});
                } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                    items.push_back({
                        {"label", vd->displayName.empty() ? vd->name : vd->displayName},
                        {"kind", 6},
                        {"detail", vd->type.name}
                    });
                }
            }
            for(classDef* base : c->baseClasses) collectMembers(base);
        };
        collectMembers(cls);

        // Also collect instance-specific members if the prefix is a global object.
        for(typeDef* g : languageService.globals)
            if(auto* od = dynamic_cast<objectDef*>(g))
                if(od->name == lower) {
                    for(typeMember* m : od->members) {
                        if(auto* fd = dynamic_cast<functionDef*>(m)) {
                            string label = fd->displayName.empty() ? fd->name : fd->displayName;
                            if(!fd->isValueEmitter) label += "(...)";
                            items.push_back({{"label", label}, {"kind", 2}, {"detail", fd->returnType.name}});
                        } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                            string label = vd->displayName.empty() ? vd->name : vd->displayName;
                            items.push_back({{"label", label}, {"kind", 6}, {"detail", vd->type.name}});
                        }
                    }
                    break;
                }

        return items;
    }

    // ── Phases 2 & 3: #beguilerSettings block ─────────────────────────────
    if(insideBsBlock) {
        classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilersettingstype"));
        if(!schema) return json::array();  // type not registered (shouldn't happen)

        // Phase 2: enum RHS context — `property = |` resolves the property's type and offers
        // its enum values. Walks back past any partial identifier + whitespace looking for '='.
        auto isValueRhsContext = [&](string& outPropertyName) -> bool {
            int i = col - 1;
            if(i >= (int)lineText.size()) i = (int)lineText.size() - 1;
            while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
            while(i >= 0 && (lineText[i] == ' ' || lineText[i] == '\t')) i--;
            if(i < 0 || lineText[i] != '=') return false;
            i--;
            while(i >= 0 && (lineText[i] == ' ' || lineText[i] == '\t')) i--;
            int nameEnd = i + 1;
            while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
            int nameStart = i + 1;
            if(nameStart >= nameEnd) return false;
            outPropertyName = lineText.substr(nameStart, nameEnd - nameStart);
            return true;
        };

        string propName;
        if(isValueRhsContext(propName)) {
            string lowerProp = propName;
            transform(lowerProp.begin(), lowerProp.end(), lowerProp.begin(), ::tolower);
            string propType;
            for(typeMember* m : schema->members)
                if(m->name == lowerProp)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) { propType = vd->type.name; break; }
            if(propType.empty()) return json::array();
            auto* ed = dynamic_cast<enumDef*>(&languageService.getType(propType));
            if(!ed) return nullptr;  // primitive (string, int, bool) — let client fall back
            json items = json::array();
            for(enumValueDef* ev : ed->namedValues) {
                items.push_back({
                    {"label", ev->displayName.empty() ? ev->name : ev->displayName},
                    {"kind", 20},
                    {"detail", ed->displayName.empty() ? ed->name : ed->displayName}
                });
            }
            return items;
        }

        // Phase 3: LHS context — return all beguilerSettingsType property names.
        json items = json::array();
        for(typeMember* m : schema->members) {
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                items.push_back({
                    {"label", vd->displayName.empty() ? vd->name : vd->displayName},
                    {"kind", 10},
                    {"detail", vd->type.name}
                });
            }
        }
        return items;
    }

    // ── Phase 4: bare-identifier position — offer #using-imported type aliases ──
    // Scan the doc above the cursor for active `#using <path>` directives and surface
    // their bare alias members as completions. Runs alongside VS Code's word-based
    // provider so normal identifier fallback still works.
    {
        // Collect #using targets from the doc up to the cursor line. File-scoped per spec.
        vector<string> usingPaths;
        {
            istringstream ds(docText);
            string dl;
            int lineIdx = 0;
            while(getline(ds, dl)){
                if(lineIdx > line) break;
                // Strip leading whitespace
                size_t p = dl.find_first_not_of(" \t");
                if(p != string::npos && dl.compare(p, 7, "#using ") == 0){
                    size_t s = p + 7;
                    while(s < dl.size() && (dl[s] == ' ' || dl[s] == '\t')) s++;
                    size_t e = s;
                    while(e < dl.size() && (isalnum((unsigned char)dl[e]) || dl[e] == '_' || dl[e] == '.')) e++;
                    if(e > s){
                        string path = dl.substr(s, e - s);
                        transform(path.begin(), path.end(), path.begin(), ::tolower);
                        usingPaths.push_back(path);
                    }
                }
                lineIdx++;
            }
        }

        if(!usingPaths.empty()){
            // Resolve each path to a target (class or object) and collect members
            // eligible as bare type names (type aliases) or bare values (value aliases/emitters).
            json items = json::array();

            auto walkObjectPath = [&](const string& path) -> objectDef* {
                size_t dot = path.find('.');
                string head = (dot == string::npos) ? path : path.substr(0, dot);
                string rest = (dot == string::npos) ? "" : path.substr(dot + 1);
                objectDef* curObj = nullptr;
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == head){ curObj = od; break; }
                while(curObj && !rest.empty()){
                    dot = rest.find('.');
                    string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
                    rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                    objectDef* next = nullptr;
                    for(typeMember* m : curObj->members){
                        auto* vd = dynamic_cast<variableDeclaration*>(m);
                        if(!vd || vd->name != seg) continue;
                        string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                        if(!initName.empty())
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == initName){ next = od; break; }
                        if(!next)
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == vd->type.name){ next = od; break; }
                        break;
                    }
                    curObj = next;
                }
                return curObj;
            };

            for(const string& path : usingPaths){
                // Try object namespace path first
                objectDef* nsObj = walkObjectPath(path);
                if(nsObj){
                    for(typeMember* m : nsObj->members){
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                            string label = vd->displayName.empty() ? vd->name : vd->displayName;
                            if(vd->isAlias){
                                items.push_back({{"label", label}, {"kind", 7},
                                                 {"detail", "type alias → " + vd->type.dName()}});
                            } else {
                                items.push_back({{"label", label}, {"kind", 6},
                                                 {"detail", vd->type.dName()}});
                            }
                        } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                            string label = fd->displayName.empty() ? fd->name : fd->displayName;
                            items.push_back({{"label", label},
                                             {"kind", fd->isValueEmitter ? 6 : 2},
                                             {"detail", fd->returnType.dName()}});
                        }
                    }
                    continue;
                }
                // Fall back to class target (emitter class etc.)
                if(path.find('.') == string::npos){
                    if(auto* cls = dynamic_cast<classDef*>(&languageService.getType(path))){
                        for(typeMember* m : cls->members){
                            if(auto* fd = dynamic_cast<functionDef*>(m)){
                                if(fd->isPrePassStub) continue;
                                string label = fd->displayName.empty() ? fd->name : fd->displayName;
                                items.push_back({{"label", label},
                                                 {"kind", fd->isValueEmitter ? 6 : 2},
                                                 {"detail", fd->returnType.dName()}});
                            } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                                string label = vd->displayName.empty() ? vd->name : vd->displayName;
                                items.push_back({{"label", label},
                                                 {"kind", vd->isAlias ? 7 : 6},
                                                 {"detail", vd->type.dName()}});
                            }
                        }
                    }
                }
            }

            if(!items.empty()){
                // isIncomplete:true lets VS Code keep re-querying and merges with word-based.
                return json{{"isIncomplete", true}, {"items", items}};
            }
        }
    }

    return nullptr;
}

//=============================================================================
// Definition (go-to-definition)
//=============================================================================

json LspServer::handleDefinition(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    // .inf-mode gating: outside #bgl{} regions, no Beguile go-to-definition.
    if(!requestAllowedAt(uri, line, col)) return nullptr;

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;

    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    if(col < 0 || col >= (int)lineText.size()) return nullptr;
    int start = col, end = col;
    while(start > 0 && (isalnum((unsigned char)lineText[start-1]) || lineText[start-1] == '_')) start--;
    while(end < (int)lineText.size() && (isalnum((unsigned char)lineText[end]) || lineText[end] == '_')) end++;
    string word = lineText.substr(start, end - start);
    if(word.empty()) return nullptr;

    string lower = word;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Check for dotted member access: walk the FULL dotted prefix so namespace paths
    // (bgl.glulx.window) resolve, not just one-level owners.
    string ownerName;
    if(start > 0 && lineText[start - 1] == '.') {
        int i = start - 1;
        string path;
        while(i >= 0 && lineText[i] == '.'){
            i--;
            int segEnd = i + 1;
            while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
            int segStart = i + 1;
            if(segStart >= segEnd) break;
            string seg = lineText.substr(segStart, segEnd - segStart);
            path = path.empty() ? seg : seg + "." + path;
        }
        ownerName = path;
    }

    sourceLocation src;

    if(!ownerName.empty()) {
        // Dotted member: resolve owner's type, then look up member in class hierarchy
        string ownerLower = ownerName;
        transform(ownerLower.begin(), ownerLower.end(), ownerLower.begin(), ::tolower);
        LspSymbolRef ownerRef = resolveSymbol(uri, line, ownerLower);
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(ownerRef.typeName));
        if(!cls) {
            typeDef& td = languageService.getType(ownerLower);
            cls = dynamic_cast<classDef*>(&td);
        }
        function<bool(classDef*)> findMember = [&](classDef* c) -> bool {
            if(!c) return false;
            for(typeMember* m : c->members) {
                if(m->name != lower) continue;
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)) { src = vd->src; return true; }
                if(auto* fd = dynamic_cast<functionDef*>(m)) { src = fd->src; return true; }
            }
            for(classDef* base : c->baseClasses) if(findMember(base)) return true;
            return false;
        };
        findMember(cls);

        // Namespace walk: if class lookup didn't find it, walk the dotted path through
        // namespace objects to locate the target object, then search its members.
        if(src.file.empty()) {
            objectDef* nsObj = nullptr;
            size_t dot = ownerLower.find('.');
            if(dot != string::npos){
                string head = ownerLower.substr(0, dot);
                string rest = ownerLower.substr(dot + 1);
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == head){ nsObj = od; break; }
                while(nsObj && !rest.empty()){
                    dot = rest.find('.');
                    string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
                    rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                    objectDef* next = nullptr;
                    for(typeMember* m : nsObj->members){
                        auto* vd = dynamic_cast<variableDeclaration*>(m);
                        if(!vd || vd->name != seg) continue;
                        string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                        if(!initName.empty())
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == initName){ next = od; break; }
                        if(!next)
                            for(typeDef* g : languageService.globals)
                                if(auto* od = dynamic_cast<objectDef*>(g))
                                    if(od->name == vd->type.name){ next = od; break; }
                        break;
                    }
                    nsObj = next;
                }
            } else {
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == ownerLower){ nsObj = od; break; }
            }
            if(nsObj){
                for(typeMember* m : nsObj->members){
                    if(m->name != lower) continue;
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                        // For type aliases, prefer jumping to the resolved flat type's definition
                        // (e.g. `class glulxWindow`) over the alias line itself. Fall back to the
                        // alias declaration if the target class has no source location.
                        if(vd->isAlias){
                            if(auto* tc = dynamic_cast<classDef*>(&languageService.getType(vd->type.name)))
                                if(!tc->src.file.empty()){ src = tc->src; break; }
                        }
                        src = vd->src;
                    } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                        src = fd->src;
                    }
                    break;
                }
            }
        }
    } else {
        // Non-dotted: use the scoped resolver
        LspSymbolRef ref = resolveSymbol(uri, line, lower);
        if(ref.declSrc.line > 0) src = ref.declSrc;
    }

    // Verbs fallback
    if(src.file.empty()) {
        for(auto* v : languageService.verbs)
            if(v->name == lower) { src = v->src; break; }
    }

    if(src.file.empty() || src.line == 0) return nullptr;

    return {
        {"uri", pathToUri(src.file)},
        {"range", {
            {"start", {{"line", src.line - 1}, {"character", 0}}},
            {"end", {{"line", src.line - 1}, {"character", 0}}}
        }}
    };
}

//=============================================================================
// Document Symbol (outline view)
//=============================================================================

json LspServer::handleDocumentSymbol(const json& params) {
    string uri = params["textDocument"]["uri"];

    // .inf-mode: no Beguile declarations are allowed at the .inf top level, and #bgl{}
    // bodies run in loose statement mode (no declarations either). Return empty so the
    // outline is driven by whatever I6 tooling the user has installed.
    auto modeIt = documentModes.find(uri);
    if(modeIt != documentModes.end() && modeIt->second == DocMode::Inf) return json::array();

    string path = uriToPath(uri);
    json symbols = json::array();

    // Determine the canonical path for filtering — only show symbols from this file
    string parsePath;
    auto ppIt = documentParsePaths.find(uri);
    if(ppIt != documentParsePaths.end()) parsePath = ppIt->second;

    auto isFromFile = [&](const sourceLocation& src) -> bool {
        if(parsePath.empty()) return true;  // no path info — show everything
        return src.file == parsePath;
    };

    // Classes and enums
    for(typeDef* t : languageService.objectTypes) {
        if(auto* cd = dynamic_cast<classDef*>(t)) {
            if(cd->src.file.empty() || !isFromFile(cd->src)) continue;
            int kind = 5;  // Class
            if(cd->isEmitterClass) kind = 11;  // Function (closest to emitter namespace)
            json sym = {
                {"name", cd->displayName.empty() ? cd->name : cd->displayName},
                {"kind", kind},
                {"range", {
                    {"start", {{"line", cd->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", cd->src.line - 1}, {"character", 0}}}
                }},
                {"selectionRange", {
                    {"start", {{"line", cd->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", cd->src.line - 1}, {"character", 0}}}
                }}
            };
            // Add members as children
            json children = json::array();
            for(typeMember* m : cd->members) {
                if(m->isPrePassStub) continue;
                if(auto* fd = dynamic_cast<functionDef*>(m)) {
                    int mk = fd->isValueEmitter ? 13 : 6;  // 13=Variable, 6=Method
                    children.push_back({
                        {"name", fd->displayName.empty() ? fd->name : fd->displayName},
                        {"kind", mk},
                        {"range", {
                            {"start", {{"line", max(0, fd->src.line - 1)}, {"character", 0}}},
                            {"end", {{"line", max(0, fd->src.line - 1)}, {"character", 0}}}
                        }},
                        {"selectionRange", {
                            {"start", {{"line", max(0, fd->src.line - 1)}, {"character", 0}}},
                            {"end", {{"line", max(0, fd->src.line - 1)}, {"character", 0}}}
                        }}
                    });
                } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                    children.push_back({
                        {"name", vd->displayName.empty() ? vd->name : vd->displayName},
                        {"kind", 8},  // Field
                        {"range", {
                            {"start", {{"line", max(0, vd->src.line - 1)}, {"character", 0}}},
                            {"end", {{"line", max(0, vd->src.line - 1)}, {"character", 0}}}
                        }},
                        {"selectionRange", {
                            {"start", {{"line", max(0, vd->src.line - 1)}, {"character", 0}}},
                            {"end", {{"line", max(0, vd->src.line - 1)}, {"character", 0}}}
                        }}
                    });
                }
            }
            if(!children.empty()) sym["children"] = children;
            symbols.push_back(sym);
        }
        else if(auto* ed = dynamic_cast<enumDef*>(t)) {
            if(ed->src.file.empty() || !isFromFile(ed->src)) continue;
            json sym = {
                {"name", ed->displayName.empty() ? ed->name : ed->displayName},
                {"kind", 10},  // Enum
                {"range", {
                    {"start", {{"line", ed->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", ed->src.line - 1}, {"character", 0}}}
                }},
                {"selectionRange", {
                    {"start", {{"line", ed->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", ed->src.line - 1}, {"character", 0}}}
                }}
            };
            json children = json::array();
            for(auto* v : ed->namedValues) {
                children.push_back({
                    {"name", v->displayName.empty() ? v->name : v->displayName},
                    {"kind", 22},  // EnumMember
                    {"range", {
                        {"start", {{"line", ed->src.line - 1}, {"character", 0}}},
                        {"end", {{"line", ed->src.line - 1}, {"character", 0}}}
                    }},
                    {"selectionRange", {
                        {"start", {{"line", ed->src.line - 1}, {"character", 0}}},
                        {"end", {{"line", ed->src.line - 1}, {"character", 0}}}
                    }}
                });
            }
            if(!children.empty()) sym["children"] = children;
            symbols.push_back(sym);
        }
    }

    // Object instances
    for(typeDef* t : languageService.objectInstances) {
        if(auto* od = dynamic_cast<objectDef*>(t)) {
            if(od->src.file.empty() || !isFromFile(od->src)) continue;
            string className = od->objectClass ? od->objectClass->name : "object";
            json sym = {
                {"name", od->displayName.empty() ? od->name : od->displayName},
                {"kind", 19},  // Object
                {"detail", className},
                {"range", {
                    {"start", {{"line", od->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", od->src.line - 1}, {"character", 0}}}
                }},
                {"selectionRange", {
                    {"start", {{"line", od->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", od->src.line - 1}, {"character", 0}}}
                }}
            };
            symbols.push_back(sym);
        }
    }

    // Global functions and variables (not objects — those are above)
    for(typeDef* g : languageService.globals) {
        if(dynamic_cast<objectDef*>(g)) continue;  // already handled
        if(auto* fd = dynamic_cast<functionDef*>(g)) {
            if(fd->isPrePassStub || fd->src.file.empty() || !isFromFile(fd->src)) continue;
            symbols.push_back({
                {"name", fd->displayName.empty() ? fd->name : fd->displayName},
                {"kind", 12},  // Function
                {"detail", fd->returnType.name},
                {"range", {
                    {"start", {{"line", fd->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", fd->src.line - 1}, {"character", 0}}}
                }},
                {"selectionRange", {
                    {"start", {{"line", fd->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", fd->src.line - 1}, {"character", 0}}}
                }}
            });
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
            if(vd->isPrePassStub || vd->src.file.empty() || !isFromFile(vd->src)) continue;
            symbols.push_back({
                {"name", vd->displayName.empty() ? vd->name : vd->displayName},
                {"kind", 13},  // Variable
                {"detail", vd->type.name},
                {"range", {
                    {"start", {{"line", vd->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", vd->src.line - 1}, {"character", 0}}}
                }},
                {"selectionRange", {
                    {"start", {{"line", vd->src.line - 1}, {"character", 0}}},
                    {"end", {{"line", vd->src.line - 1}, {"character", 0}}}
                }}
            });
        }
    }

    // Verbs
    for(auto* v : languageService.verbs) {
        if(v->src.file.empty() || !isFromFile(v->src)) continue;
        symbols.push_back({
            {"name", v->displayName.empty() ? v->name : v->displayName},
            {"kind", 19},  // Object
            {"detail", "verb"},
            {"range", {
                {"start", {{"line", v->src.line - 1}, {"character", 0}}},
                {"end", {{"line", v->src.line - 1}, {"character", 0}}}
            }},
            {"selectionRange", {
                {"start", {{"line", v->src.line - 1}, {"character", 0}}},
                {"end", {{"line", v->src.line - 1}, {"character", 0}}}
            }}
        });
    }

    return symbols;
}

//=============================================================================
// Signature Help
//=============================================================================

json LspServer::handleSignatureHelp(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    // .inf-mode gating: outside #bgl{} regions, no Beguile signature help.
    if(!requestAllowedAt(uri, line, col)) return nullptr;

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;

    // Get the line text
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    // Walk backward from cursor to find the function name and count commas
    int parenDepth = 0;
    int commaCount = 0;
    int funcEnd = -1;
    for(int i = col - 1; i >= 0; i--) {
        char c = lineText[i];
        if(c == ')') parenDepth++;
        else if(c == '(') {
            if(parenDepth == 0) {
                funcEnd = i;
                break;
            }
            parenDepth--;
        }
        else if(c == ',' && parenDepth == 0) commaCount++;
    }
    if(funcEnd < 0) return nullptr;

    // Extract function name
    int nameEnd = funcEnd;
    int nameStart = nameEnd - 1;
    while(nameStart >= 0 && (isalnum(lineText[nameStart]) || lineText[nameStart] == '_')) nameStart--;
    nameStart++;
    string funcName = lineText.substr(nameStart, nameEnd - nameStart);
    if(funcName.empty()) return nullptr;

    string lower = funcName;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Check if there's a dot before the function name — member call
    string objName;
    if(nameStart > 0 && lineText[nameStart - 1] == '.') {
        int objEnd = nameStart - 1;
        int objStart = objEnd - 1;
        while(objStart >= 0 && (isalnum(lineText[objStart]) || lineText[objStart] == '_')) objStart--;
        objStart++;
        objName = lineText.substr(objStart, objEnd - objStart);
    }

    // Find the function definition
    vector<functionDef*> candidates;

    if(!objName.empty()) {
        // Member function — resolve the object/class type via scoped resolver
        string objLower = objName;
        transform(objLower.begin(), objLower.end(), objLower.begin(), ::tolower);
        classDef* cls = nullptr;

        // Check enclosing function scope first (locals/parameters)
        LspSymbolRef ownerRef = resolveSymbol(uri, line, objLower);
        if(ownerRef.kind == LspSymbolRef::Local || ownerRef.kind == LspSymbolRef::Parameter)
            cls = dynamic_cast<classDef*>(&languageService.getType(ownerRef.typeName));
        if(!cls) {
            typeDef& td = languageService.getType(objLower);
            if(auto* cd = dynamic_cast<classDef*>(&td)) cls = cd;
        }
        if(!cls) {
            for(typeDef* g : languageService.globals)
                if(auto* od = dynamic_cast<objectDef*>(g))
                    if(od->name == objLower) { cls = od->objectClass; break; }
        }
        if(!cls) {
            for(typeDef* g : languageService.globals)
                if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                    if(vd->name == objLower) {
                        cls = dynamic_cast<classDef*>(&languageService.getType(vd->type.name));
                        break;
                    }
        }

        // Walk class hierarchy for matching methods
        function<void(classDef*)> findMethods = [&](classDef* c) {
            if(!c) return;
            for(typeMember* m : c->members) {
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == lower && !fd->isValueEmitter && !fd->isPrePassStub)
                        candidates.push_back(fd);
            }
            for(classDef* base : c->baseClasses) findMethods(base);
        };
        if(cls) findMethods(cls);
    } else {
        // Global function
        for(typeDef* g : languageService.globals) {
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->name == lower && !fd->isPrePassStub)
                    candidates.push_back(fd);
        }
    }

    if(candidates.empty()) return nullptr;

    json signatures = json::array();
    for(functionDef* fd : candidates) {
        string label = typeDisplay(fd->returnType.name) + " " +
                       (fd->displayName.empty() ? fd->name : fd->displayName) + "(";
        json parameters = json::array();
        int labelStart = (int)label.size();
        for(size_t i = 0; i < fd->params.size(); i++) {
            if(i > 0) { label += ", "; labelStart = (int)label.size(); }
            else labelStart = (int)label.size();
            string paramLabel = typeDisplay(fd->params[i]->type.name) + " " +
                                (fd->params[i]->displayName.empty() ? fd->params[i]->name : fd->params[i]->displayName);
            label += paramLabel;
            parameters.push_back({
                {"label", {labelStart, (int)label.size()}}
            });
        }
        label += ")";

        json sig = {
            {"label", label},
            {"parameters", parameters}
        };
        if(fd->isEmitter) sig["documentation"] = "emitter function";
        signatures.push_back(sig);
    }

    return {
        {"signatures", signatures},
        {"activeSignature", 0},
        {"activeParameter", commaCount}
    };
}

//=============================================================================
// Reset and Re-parse (for didChange)
//=============================================================================

void LspServer::resetAndReparse(const string& uri) {
    // Reset both parser and language service state, then re-parse
    parser.reset();
    languageService.reset();
    parseDocument(uri);
    publishDiagnostics(uri);
    publishInactiveRegions(uri);
}

//=============================================================================
// References (find all references to a symbol)
//=============================================================================

// Compute the [startLine, endLine) range of a function's body by finding the next same-file
// function's start line. Returns {-1,-1} if unknown. Lines are 1-based to match src.line.
static pair<int,int> functionLineRange(functionDef* fn) {
    if(!fn || fn->src.line == 0) return {-1, -1};
    int startLine = fn->src.line;
    int endLine = INT_MAX;
    for(typeDef* g : languageService.globals) {
        auto* other = dynamic_cast<functionDef*>(g);
        if(!other || other == fn || other->src.line == 0) continue;
        if(other->src.file != fn->src.file) continue;
        if(other->src.line > startLine && other->src.line < endLine)
            endLine = other->src.line;
    }
    return {startLine, endLine};
}

json LspServer::handleReferences(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    // .inf-mode gating: outside #bgl{} regions, no Beguile references.
    if(!requestAllowedAt(uri, line, col)) return json::array();

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return json::array();

    // Extract the word at the cursor
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    if(col >= (int)lineText.size()) return json::array();
    int start = col, end = col;
    while(start > 0 && (isalnum(lineText[start-1]) || lineText[start-1] == '_')) start--;
    while(end < (int)lineText.size() && (isalnum(lineText[end]) || lineText[end] == '_')) end++;
    string word = lineText.substr(start, end - start);
    if(word.empty()) return json::array();

    // Scope-gate: if the symbol is a local or parameter, restrict search to the enclosing
    // function's line range in the current file. Otherwise scan all open documents.
    string lowered = word;
    transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
    LspSymbolRef ref = resolveSymbol(uri, line, lowered);
    bool restrictToFunction = (ref.kind == LspSymbolRef::Local || ref.kind == LspSymbolRef::Parameter);
    int fnStart1 = -1, fnEnd1 = -1;
    string restrictFile;
    if(restrictToFunction) {
        auto rng = functionLineRange(ref.enclosingFunc);
        fnStart1 = rng.first;
        fnEnd1   = rng.second;
        restrictFile = ref.enclosingFunc->src.file;
    }

    // Search all open documents for whole-word occurrences
    json locations = json::array();
    for(auto& [docUri, docText] : openDocuments) {
        // For scope-restricted searches, only scan the file containing the function.
        if(restrictToFunction) {
            string docPath = documentParsePaths.count(docUri) ? documentParsePaths[docUri] : uriToPath(docUri);
            if(docPath != restrictFile) continue;
        }
        istringstream docStream(docText);
        string docLine;
        int lineNum = 0;  // 0-based LSP line
        while(getline(docStream, docLine)) {
            // For scope-restricted: only include lines within the function's range (1-based).
            int line1Based = lineNum + 1;
            if(restrictToFunction && (line1Based < fnStart1 || line1Based >= fnEnd1)) { lineNum++; continue; }
            size_t pos = 0;
            while(pos < docLine.size()) {
                size_t found = docLine.find(word, pos);
                if(found == string::npos) break;
                bool leftOk = (found == 0) || !(isalnum(docLine[found-1]) || docLine[found-1] == '_');
                size_t afterEnd = found + word.size();
                bool rightOk = (afterEnd >= docLine.size()) || !(isalnum(docLine[afterEnd]) || docLine[afterEnd] == '_');
                if(leftOk && rightOk) {
                    locations.push_back({
                        {"uri", docUri},
                        {"range", {
                            {"start", {{"line", lineNum}, {"character", (int)found}}},
                            {"end", {{"line", lineNum}, {"character", (int)afterEnd}}}
                        }}
                    });
                }
                pos = found + 1;
            }
            lineNum++;
        }
    }

    return locations;
}

//=============================================================================
// Rename (rename symbol across files)
//=============================================================================

json LspServer::handleRename(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();
    string newName = params["newName"];

    // .inf-mode gating: outside #bgl{} regions, refuse to rename (would touch I6 code).
    if(!requestAllowedAt(uri, line, col)) return nullptr;

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;

    // Extract the word at the cursor
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    if(col >= (int)lineText.size()) return nullptr;
    int start = col, end = col;
    while(start > 0 && (isalnum(lineText[start-1]) || lineText[start-1] == '_')) start--;
    while(end < (int)lineText.size() && (isalnum(lineText[end]) || lineText[end] == '_')) end++;
    string oldName = lineText.substr(start, end - start);
    if(oldName.empty()) return nullptr;

    // Scope-gate (same as handleReferences): locals/parameters only rename within
    // their enclosing function's line range.
    string lowered = oldName;
    transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
    LspSymbolRef ref = resolveSymbol(uri, line, lowered);
    bool restrictToFunction = (ref.kind == LspSymbolRef::Local || ref.kind == LspSymbolRef::Parameter);
    int fnStart1 = -1, fnEnd1 = -1;
    string restrictFile;
    if(restrictToFunction) {
        auto rng = functionLineRange(ref.enclosingFunc);
        fnStart1 = rng.first;
        fnEnd1   = rng.second;
        restrictFile = ref.enclosingFunc->src.file;
    }

    // Build workspace edit — replace all whole-word occurrences in all open documents
    json changes = json::object();
    for(auto& [docUri, docText] : openDocuments) {
        if(restrictToFunction) {
            string docPath = documentParsePaths.count(docUri) ? documentParsePaths[docUri] : uriToPath(docUri);
            if(docPath != restrictFile) continue;
        }
        json edits = json::array();
        istringstream docStream(docText);
        string docLine;
        int lineNum = 0;
        while(getline(docStream, docLine)) {
            int line1Based = lineNum + 1;
            if(restrictToFunction && (line1Based < fnStart1 || line1Based >= fnEnd1)) { lineNum++; continue; }
            size_t pos = 0;
            while(pos < docLine.size()) {
                size_t found = docLine.find(oldName, pos);
                if(found == string::npos) break;
                bool leftOk = (found == 0) || !(isalnum(docLine[found-1]) || docLine[found-1] == '_');
                size_t afterEnd = found + oldName.size();
                bool rightOk = (afterEnd >= docLine.size()) || !(isalnum(docLine[afterEnd]) || docLine[afterEnd] == '_');
                if(leftOk && rightOk) {
                    edits.push_back({
                        {"range", {
                            {"start", {{"line", lineNum}, {"character", (int)found}}},
                            {"end", {{"line", lineNum}, {"character", (int)afterEnd}}}
                        }},
                        {"newText", newName}
                    });
                }
                pos = found + 1;
            }
            lineNum++;
        }
        if(!edits.empty()) changes[docUri] = edits;
    }

    return {{"changes", changes}};
}

//=============================================================================
// Semantic Tokens
//=============================================================================

// Token type indices (must match the legend in handleInitialize)
enum SemanticTokenType {
    stNamespace = 0, stType = 1, stClass = 2, stEnum = 3, stInterface = 4,
    stStruct = 5, stTypeParameter = 6, stParameter = 7, stVariable = 8,
    stProperty = 9, stEnumMember = 10, stEvent = 11, stFunction = 12,
    stMethod = 13, stMacro = 14, stKeyword = 15, stModifier = 16,
    stComment = 17, stString = 18, stNumber = 19, stRegexp = 20, stOperator = 21
};

// Beguile keywords for semantic classification
static const set<string> bglKeywords = {
    "if", "else", "for", "while", "do", "switch", "case", "default",
    "return", "break", "continue", "in", "to", "new", "try", "catch", "throw",
    "print", "true", "false", "nothing", "self"
};
// Real declaration / member modifiers only. Class type names (object, int, bool, char,
// string, verb, attribute, attributeList, array, etc.) are intentionally NOT in this set —
// they're registered classes in the language service and should fall through to the
// stClass branch in classifyWord so they color the same as user-defined types. Same for
// non-class registered types like void/var/auto, which are caught by a separate check
// further down. `has` / `hasnt` were removed entirely — they're methods on object, not
// keywords.
static const set<string> bglModifiers = {
    "class", "enum", "extern", "emitter", "static", "const",
    "replace", "extend", "readonly", "explicit", "default"
};
static const set<string> bglDirectives = {
    "#include", "#once", "#define", "#if", "#else", "#endif", "#ifdef",
    "#ifndef", "#i6", "#startup", "#emitfirst", "#emitlast", "#using", "#exit"
};

int LspServer::classifyWord(const string& word) const {
    // Check language keywords first
    string lower = word;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if(bglKeywords.count(lower)) return stKeyword;
    if(bglModifiers.count(lower)) return stModifier;

    // Check language service
    typeDef& td = languageService.getType(lower);
    if(auto* cd = dynamic_cast<classDef*>(&td)) {
        if(cd->isEmitterClass) return stInterface;
        return stClass;
    }
    if(dynamic_cast<enumDef*>(&td)) return stEnum;
    // Non-class registered base types: void, var, auto, func. These are real types in the
    // language service (registered via registerType) but aren't classDef instances. Color
    // them as stClass too so all type names render uniformly across themes. Explicitly
    // exclude objectDef (instances) so global object names like `foyer` still classify
    // as variables, not classes.
    if(!td.name.empty() && td.name == lower
       && !dynamic_cast<objectDef*>(&td)
       && !dynamic_cast<verbObjectDef*>(&td)) return stClass;

    // Check if it's a known enum value
    if(!languageService.getEnumType(lower).empty()) return stEnumMember;

    // Check globals
    for(typeDef* g : languageService.globals) {
        if(g->name == lower) {
            if(dynamic_cast<functionDef*>(g)) return stFunction;
            if(dynamic_cast<objectDef*>(g)) return stVariable;
            if(dynamic_cast<verbObjectDef*>(g)) return stVariable;
            if(dynamic_cast<variableDeclaration*>(g)) return stVariable;
            break;
        }
    }

    // Check verbs
    for(auto* v : languageService.verbs)
        if(v->name == lower) return stVariable;

    return -1;  // unknown — don't emit a token
}

json LspServer::handleSemanticTokensFull(const json& params) {
    string uri = params["textDocument"]["uri"];
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return {{"data", json::array()}};

    // .inf-mode: don't classify the I6 host code. Beguile-aware highlighting inside #bgl{}
    // regions is Tier-2 work (TextMate injection); for now we hand off to the I6 highlighter.
    auto modeIt = documentModes.find(uri);
    if(modeIt != documentModes.end() && modeIt->second == DocMode::Inf)
        return {{"data", json::array()}};

    // Semantic tokens are encoded as delta-encoded quintuplets:
    // [deltaLine, deltaStartChar, length, tokenType, tokenModifiers]
    vector<int> data;
    int prevLine = 0, prevChar = 0;

    // Helper: emit a token at (lineNum, start) with given length and type
    auto emit = [&](int lineNum, int start, int len, int tokenType) {
        int deltaLine = lineNum - prevLine;
        int deltaChar = (deltaLine == 0) ? start - prevChar : start;
        data.insert(data.end(), {deltaLine, deltaChar, len, tokenType, 0});
        prevLine = lineNum; prevChar = start;
    };

    // Pre-scan the document for #beguilerSettings block ranges so we can tokenize property
    // names inside them as stProperty. Each range is [startLine, endLine] inclusive.
    vector<pair<int,int>> bsBlockRanges;
    {
        const string& txt = docIt->second;
        int scanLine = 0;
        size_t pos = 0;
        while(pos < txt.size()) {
            // Find '#beguilerSettings' (case-insensitive)
            bool found = false;
            size_t tagStart = string::npos;
            int tagLine = scanLine;
            for(size_t j = pos; j + 17 <= txt.size(); j++) {
                if(txt[j] == '\n') { tagLine++; continue; }
                if((txt[j] == '#') &&
                   strncasecmp(txt.c_str() + j, "#beguilerSettings", 17) == 0) {
                    tagStart = j; found = true; break;
                }
            }
            if(!found) break;
            // Advance scanLine to reflect lines consumed up to tagStart
            scanLine = tagLine;
            // Find the '{' after the tag
            size_t bracePos = txt.find('{', tagStart + 17);
            if(bracePos == string::npos) break;
            int openLine = scanLine;
            for(size_t j = tagStart; j < bracePos; j++) if(txt[j] == '\n') openLine++;
            // Walk forward counting braces to find matching '}'
            int depth = 1;
            int closeLine = openLine;
            size_t k = bracePos + 1;
            for(; k < txt.size() && depth > 0; k++) {
                if(txt[k] == '\n') closeLine++;
                else if(txt[k] == '{') depth++;
                else if(txt[k] == '}') depth--;
            }
            bsBlockRanges.push_back({openLine, closeLine});
            pos = k;
            scanLine = closeLine;
        }
    }
    auto isInBsBlock = [&](int lineNum) -> bool {
        for(auto& r : bsBlockRanges)
            if(lineNum >= r.first && lineNum <= r.second) return true;
        return false;
    };

    // Collect beguilerSettingsType member names (lowercased) for property tokenization
    std::set<string> bsMemberNames;
    if(auto* schema = dynamic_cast<classDef*>(&languageService.getType("beguilersettingstype")))
        for(typeMember* m : schema->members) bsMemberNames.insert(m->name);

    // Pre-scan instance/class body ranges so bare identifiers inside them can be resolved as
    // members via class-hierarchy walk. Each entry: (startLine0, endLine0Exclusive, classDef*).
    // For 'room foyer { short_name = ... }', the block covers the interior of foyer's body and
    // maps to the 'room' classDef so 'short_name' (inherited from room) resolves as a property.
    // For 'class Counter { ... }', maps to Counter itself so bare member names inside the class
    // body resolve as members.
    struct InstanceBlockRange { int startLine0; int endLine0Exclusive; classDef* cls; objectDef* obj; };
    vector<InstanceBlockRange> instanceBlocks;
    {
        const string& txt = docIt->second;
        // Helper: find the matching '}' for an open brace at openPos, skipping strings and
        // // and /* */ comments. Returns the char index AFTER the matching '}', or npos if EOF.
        auto matchClose = [&](size_t openPos) -> size_t {
            int depth = 1;
            size_t j = openPos + 1;
            while(j < txt.size() && depth > 0) {
                char c = txt[j];
                if(c == '"') {
                    j++;
                    while(j < txt.size() && txt[j] != '"') {
                        if(txt[j] == '\\') j++;
                        if(j < txt.size()) j++;
                    }
                    if(j < txt.size()) j++;
                } else if(c == '/' && j + 1 < txt.size() && txt[j+1] == '/') {
                    while(j < txt.size() && txt[j] != '\n') j++;
                } else if(c == '/' && j + 1 < txt.size() && txt[j+1] == '*') {
                    j += 2;
                    while(j + 1 < txt.size() && !(txt[j] == '*' && txt[j+1] == '/')) j++;
                    if(j + 1 < txt.size()) j += 2;
                } else {
                    if(c == '{') depth++;
                    else if(c == '}') { depth--; if(depth == 0) return j + 1; }
                    j++;
                }
            }
            return txt.size();
        };
        // Line-number-at-offset cache (linear scan ok for typical file sizes)
        auto lineAt = [&](size_t offset) -> int {
            int ln = 0;
            for(size_t j = 0; j < offset && j < txt.size(); j++) if(txt[j] == '\n') ln++;
            return ln;
        };

        string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
        auto addFromSrcLine = [&](int srcLine1, classDef* cls, objectDef* obj) {
            if(!cls && !obj) return;
            if(srcLine1 <= 0) return;
            // Find the first '{' on or after srcLine1-1 (0-based)
            // Skip to the start of srcLine1
            int targetLine = srcLine1 - 1;
            int ln = 0; size_t startOff = 0;
            while(startOff < txt.size() && ln < targetLine) {
                if(txt[startOff] == '\n') ln++;
                startOff++;
            }
            // Find first '{' from startOff (honouring strings/comments)
            size_t bracePos = string::npos;
            for(size_t j = startOff; j < txt.size(); j++) {
                char c = txt[j];
                if(c == '"') {
                    j++;
                    while(j < txt.size() && txt[j] != '"') { if(txt[j] == '\\') j++; if(j < txt.size()) j++; }
                } else if(c == '/' && j + 1 < txt.size() && txt[j+1] == '/') {
                    while(j < txt.size() && txt[j] != '\n') j++;
                } else if(c == '/' && j + 1 < txt.size() && txt[j+1] == '*') {
                    j += 2;
                    while(j + 1 < txt.size() && !(txt[j] == '*' && txt[j+1] == '/')) j++;
                    if(j + 1 < txt.size()) j++;
                } else if(c == '{') { bracePos = j; break; }
            }
            if(bracePos == string::npos) return;
            size_t closeEnd = matchClose(bracePos);
            instanceBlocks.push_back({ lineAt(bracePos) + 1, lineAt(closeEnd), cls, obj });
        };
        // Instance bodies: record both the objectDef (for its own members, including methods
        // declared inline) and its declared class (for inherited members).
        for(typeDef* t : languageService.objectInstances) {
            if(auto* od = dynamic_cast<objectDef*>(t)) {
                if(od->src.file != curFile) continue;
                addFromSrcLine(od->src.line, od->objectClass, od);
            }
        }
        // Class bodies: the class itself is the lookup scope
        for(typeDef* t : languageService.objectTypes) {
            if(auto* cd = dynamic_cast<classDef*>(t)) {
                if(cd->src.file != curFile) continue;
                addFromSrcLine(cd->src.line, cd, nullptr);
            }
        }
    }
    // Return the innermost block containing lineNum, or nullptr.
    auto blockForLine = [&](int lineNum) -> const InstanceBlockRange* {
        const InstanceBlockRange* best = nullptr;
        int bestStart = -1;
        for(const auto& b : instanceBlocks) {
            if(lineNum >= b.startLine0 && lineNum < b.endLine0Exclusive)
                if(b.startLine0 > bestStart) { bestStart = b.startLine0; best = &b; }
        }
        return best;
    };
    // Walk a block's member scope (objectDef members first, then class hierarchy) looking for
    // 'name'. Returns kind: 1 = variable/property, 2 = function/method, 0 = not found.
    auto findInBlock = [&](const InstanceBlockRange* b, const string& name) -> int {
        if(!b) return 0;
        // objectDef's own members (includes methods declared inline in the instance body)
        if(b->obj) {
            for(typeMember* m : b->obj->members) {
                if(m->name != name) continue;
                if(dynamic_cast<variableDeclaration*>(m)) return 1;
                if(dynamic_cast<functionDef*>(m))         return 2;
            }
        }
        // Class hierarchy
        function<int(classDef*)> walk = [&](classDef* c) -> int {
            if(!c) return 0;
            for(typeMember* m : c->members) {
                if(m->name != name) continue;
                if(dynamic_cast<variableDeclaration*>(m)) return 1;
                if(dynamic_cast<functionDef*>(m))         return 2;
            }
            for(classDef* base : c->baseClasses) { int r = walk(base); if(r) return r; }
            return 0;
        };
        return walk(b->cls);
    };
    // Resolve a dotted-access receiver name to a transient block-style lookup context.
    // Returns a pair (classDef*, objectDef*) — the caller uses findInBlock via a temp range.
    auto lookupDotted = [&](const string& receiverLower, int lineNum, const string& member) -> int {
        if(receiverLower == "self") {
            return findInBlock(blockForLine(lineNum), member);
        }
        // Class or enum lookup by name (e.g. bglWorld.ofClass)
        typeDef& td = languageService.getType(receiverLower);
        if(auto* cd = dynamic_cast<classDef*>(&td)) {
            InstanceBlockRange tmp{0,0,cd,nullptr};
            return findInBlock(&tmp, member);
        }
        // Check objectInstances directly (they're not always in globals).
        for(typeDef* t : languageService.objectInstances) {
            if(t->name != receiverLower) continue;
            if(auto* od = dynamic_cast<objectDef*>(t)) {
                InstanceBlockRange tmp{0,0,od->objectClass,od};
                return findInBlock(&tmp, member);
            }
        }
        // Global object or variable: check its members first, then its class hierarchy
        for(typeDef* g : languageService.globals) {
            if(g->name != receiverLower) continue;
            if(auto* od = dynamic_cast<objectDef*>(g)) {
                InstanceBlockRange tmp{0,0,od->objectClass,od};
                return findInBlock(&tmp, member);
            }
            if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
                auto* cd = dynamic_cast<classDef*>(&languageService.getType(vd->type.name));
                if(cd) { InstanceBlockRange tmp{0,0,cd,nullptr}; return findInBlock(&tmp, member); }
            }
        }
        return 0;
    };

    istringstream stream(docIt->second);
    string lineText;
    int lineNum = 0;
    bool inBlockComment = false;

    while(getline(stream, lineText)) {
        size_t i = 0;

        // If a /* */ block comment was opened on a prior line, consume until we find '*/'.
        if(inBlockComment) {
            size_t close = lineText.find("*/");
            int len = (close == string::npos) ? (int)lineText.size() : (int)close + 2;
            if(len > 0) emit(lineNum, 0, len, stComment);
            if(close == string::npos) { lineNum++; continue; }
            inBlockComment = false;
            i = close + 2;
        }

        while(i < lineText.size()) {
            char c = lineText[i];

            // Skip whitespace
            if(isspace(c)) { i++; continue; }

            // Comments: // to end of line
            if(c == '/' && i + 1 < lineText.size() && lineText[i+1] == '/') {
                int len = (int)lineText.size() - (int)i;
                emit(lineNum, (int)i, len, stComment);
                break;  // rest of line is comment
            }

            // Comments: /* ... */ — may span multiple lines
            if(c == '/' && i + 1 < lineText.size() && lineText[i+1] == '*') {
                size_t start = i;
                size_t close = lineText.find("*/", i + 2);
                if(close == string::npos) {
                    // Block comment runs to end of line; set flag for next line
                    int len = (int)lineText.size() - (int)start;
                    emit(lineNum, (int)start, len, stComment);
                    inBlockComment = true;
                    break;
                } else {
                    int len = (int)(close + 2 - start);
                    emit(lineNum, (int)start, len, stComment);
                    i = close + 2;
                    continue;
                }
            }

            // String literals
            if(c == '"') {
                size_t start = i;
                i++;  // skip opening quote
                while(i < lineText.size() && lineText[i] != '"') {
                    if(lineText[i] == '\\') i++;  // skip escape
                    i++;
                }
                if(i < lineText.size()) i++;  // skip closing quote
                emit(lineNum, (int)start, (int)(i - start), stString);
                continue;
            }

            // Character literals
            if(c == '\'') {
                size_t start = i;
                i++;
                while(i < lineText.size() && lineText[i] != '\'') {
                    if(lineText[i] == '\\') i++;
                    i++;
                }
                if(i < lineText.size()) i++;
                emit(lineNum, (int)start, (int)(i - start), stString);
                continue;
            }

            // Directives: intentionally not emitted as semantic tokens. The TextMate grammar
            // colors every `#directive` keyword uniformly via `keyword.control.directive.beguile`
            // and its hierarchy. Emitting stMacro for a subset of directives produced inconsistent
            // colors across themes (e.g. #include blue, #beguilerSettings magenta in Dark+) because
            // semantic tokens beat TextMate scopes. Let the grammar handle all directives.
            if(c == '#') {
                // Still advance past the directive so subsequent scanning continues correctly.
                i++;
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                continue;
            }

            // Numbers
            if(isdigit(c) || (c == '-' && i + 1 < lineText.size() && isdigit(lineText[i+1]))) {
                size_t start = i;
                if(c == '-') i++;
                if(i + 1 < lineText.size() && lineText[i] == '0' && lineText[i+1] == 'x') {
                    i += 2;
                    while(i < lineText.size() && isxdigit(lineText[i])) i++;
                } else {
                    while(i < lineText.size() && isdigit(lineText[i])) i++;
                }
                // Only classify as number if the character before wasn't alphanumeric
                if(start == 0 || !(isalnum(lineText[start-1]) || lineText[start-1] == '_'))
                    emit(lineNum, (int)start, (int)(i - start), stNumber);
                continue;
            }

            // Identifiers and keywords
            if(isalpha(c) || c == '_') {
                size_t start = i;
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                string word = lineText.substr(start, i - start);
                string lowerWord = word;
                transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);

                // Inside #beguilerSettings { }, classify recognized property names as stProperty.
                // This fires regardless of whatever classifyWord would say (e.g. 'headline' is
                // also registered as a const global via i6StandardLibrary.bgl, which would
                // otherwise tokenize it as a variable — overriding here keeps block members
                // visually consistent).
                if(isInBsBlock(lineNum) && bsMemberNames.count(lowerWord)) {
                    emit(lineNum, (int)start, (int)(i - start), stProperty);
                    continue;
                }

                // Dotted access: obj.member or self.member — walk the receiver's members and
                // class hierarchy so inherited methods/properties colorize correctly. Applies
                // only when the char immediately before the identifier is '.' and the receiver
                // is a single identifier.
                if(start > 0 && lineText[start - 1] == '.') {
                    int rEnd = (int)start - 1;
                    int rStart = rEnd - 1;
                    while(rStart >= 0 && (isalnum((unsigned char)lineText[rStart]) || lineText[rStart] == '_')) rStart--;
                    rStart++;
                    if(rStart < rEnd) {
                        string recvLower = lineText.substr(rStart, rEnd - rStart);
                        transform(recvLower.begin(), recvLower.end(), recvLower.begin(), ::tolower);
                        int kind = lookupDotted(recvLower, lineNum, lowerWord);
                        if(kind == 2) { emit(lineNum, (int)start, (int)(i - start), stMethod);   continue; }
                        if(kind == 1) { emit(lineNum, (int)start, (int)(i - start), stProperty); continue; }
                    }
                }

                // Bare identifier inside an instance/class body: resolve against the enclosing
                // block's member scope (objectDef own members first, then class hierarchy).
                // Covers Gap 1 (property initializers like `short_name = ...`) and Gap 2
                // (method declarations like `bool before() { }` that live on the objectDef).
                if(const InstanceBlockRange* blk = blockForLine(lineNum)) {
                    int kind = findInBlock(blk, lowerWord);
                    if(kind == 2) { emit(lineNum, (int)start, (int)(i - start), stMethod);   continue; }
                    if(kind == 1) { emit(lineNum, (int)start, (int)(i - start), stProperty); continue; }
                }

                int tokenType = classifyWord(word);
                if(tokenType >= 0)
                    emit(lineNum, (int)start, (int)(i - start), tokenType);
                continue;
            }

            // Dictionary words: .word or ..word (but NOT member access like ball.count)
            if(c == '.' && i + 1 < lineText.size() && isalpha(lineText[i+1])
               && (i == 0 || !(isalnum(lineText[i-1]) || lineText[i-1] == '_'))) {
                size_t start = i;
                i++;
                if(i < lineText.size() && lineText[i] == '.') i++;  // ..word
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                emit(lineNum, (int)start, (int)(i - start), stString);
                continue;
            }

            i++;  // skip unrecognized characters
        }
        lineNum++;
    }

    return {{"data", data}};
}

//=============================================================================
// Workspace Symbol
//=============================================================================

json LspServer::handleWorkspaceSymbol(const json& params) {
    string query = params.value("query", "");
    string queryLower = query;
    transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

    json symbols = json::array();

    auto matchesQuery = [&](const string& name) -> bool {
        if(queryLower.empty()) return true;
        string nameLower = name;
        transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        return nameLower.find(queryLower) != string::npos;
    };

    auto makeLocation = [&](const sourceLocation& src) -> json {
        return {
            {"uri", pathToUri(src.file)},
            {"range", {
                {"start", {{"line", max(0, src.line - 1)}, {"character", 0}}},
                {"end", {{"line", max(0, src.line - 1)}, {"character", 0}}}
            }}
        };
    };

    // Classes and enums
    for(typeDef* t : languageService.objectTypes) {
        if(auto* cd = dynamic_cast<classDef*>(t)) {
            string name = cd->displayName.empty() ? cd->name : cd->displayName;
            if(!matchesQuery(name) || cd->src.file.empty()) continue;
            int kind = cd->isEmitterClass ? 11 : 5;  // Interface or Class
            symbols.push_back({
                {"name", name},
                {"kind", kind},
                {"location", makeLocation(cd->src)}
            });
        }
        else if(auto* ed = dynamic_cast<enumDef*>(t)) {
            string name = ed->displayName.empty() ? ed->name : ed->displayName;
            if(!matchesQuery(name) || ed->src.file.empty()) continue;
            symbols.push_back({
                {"name", name},
                {"kind", 10},  // Enum
                {"location", makeLocation(ed->src)}
            });
        }
    }

    // Object instances
    for(typeDef* t : languageService.objectInstances) {
        if(auto* od = dynamic_cast<objectDef*>(t)) {
            string name = od->displayName.empty() ? od->name : od->displayName;
            if(!matchesQuery(name) || od->src.file.empty()) continue;
            string container = od->objectClass ? od->objectClass->name : "object";
            symbols.push_back({
                {"name", name},
                {"kind", 19},  // Object
                {"containerName", container},
                {"location", makeLocation(od->src)}
            });
        }
    }

    // Global functions and variables
    for(typeDef* g : languageService.globals) {
        if(dynamic_cast<objectDef*>(g)) continue;  // already handled above
        if(auto* fd = dynamic_cast<functionDef*>(g)) {
            if(fd->isPrePassStub || fd->src.file.empty()) continue;
            string name = fd->displayName.empty() ? fd->name : fd->displayName;
            if(!matchesQuery(name)) continue;
            symbols.push_back({
                {"name", name},
                {"kind", 12},  // Function
                {"location", makeLocation(fd->src)}
            });
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
            if(vd->isPrePassStub || vd->src.file.empty()) continue;
            string name = vd->displayName.empty() ? vd->name : vd->displayName;
            if(!matchesQuery(name)) continue;
            symbols.push_back({
                {"name", name},
                {"kind", 13},  // Variable
                {"location", makeLocation(vd->src)}
            });
        }
    }

    // Verbs
    for(auto* v : languageService.verbs) {
        if(v->src.file.empty()) continue;
        string name = v->displayName.empty() ? v->name : v->displayName;
        if(!matchesQuery(name)) continue;
        symbols.push_back({
            {"name", name},
            {"kind", 19},  // Object
            {"containerName", "verb"},
            {"location", makeLocation(v->src)}
        });
    }

    return symbols;
}

//=============================================================================
// Helpers
//=============================================================================

string LspServer::uriToPath(const string& uri) {
    // file:///path/to/file.bgl → /path/to/file.bgl
    string path = uri;
    if(path.rfind("file://", 0) == 0) path = path.substr(7);
    // URL-decode %20 etc.
    string decoded;
    for(size_t i = 0; i < path.size(); i++) {
        if(path[i] == '%' && i + 2 < path.size()) {
            int hex = stoi(path.substr(i+1, 2), nullptr, 16);
            decoded += (char)hex;
            i += 2;
        } else {
            decoded += path[i];
        }
    }
    return decoded;
}

string LspServer::pathToUri(const string& path) {
    string absPath = filesystem::absolute(path).string();
    // Encode spaces and special chars
    string uri = "file://";
    for(char c : absPath) {
        if(c == ' ') uri += "%20";
        else uri += c;
    }
    return uri;
}
