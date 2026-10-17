#include "platform.h"
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
#include "blorb.h"
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
    // dump() defaults to strict UTF-8 validation and throws on invalid bytes — that kills
    // the LSP process when source-derived content (e.g. parser diagnostic messages that
    // echo identifier or string content from a file with bad encoding) reaches the wire.
    // Use replace mode so bad bytes become U+FFFD; the client still gets a coherent reply.
    string body = msg.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
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

        try {
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
        } catch(const exception& e) {
            sendError(id, -32603, string("Internal error: ") + e.what());
        }

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
        else if(method == "beguile/setEntryPoint")
            handleSetEntryPoint(params);
        else if(method == "beguile/setConfig")
            handleSetConfig(params);
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
            // Trigger characters that auto-pop the completion dropdown:
            //   '.' — member access  (foo.bar)
            //   '=' — RHS of #beguilerSettings property
            //   '<' — `#include <…>` library completion (Phase 0)
            //   '"' — `#include "…"` project-file completion (Phase 0b)
            //   '/' — next path segment inside `#include "sub/…"` (Phase 0b)
            //   ':' — class inheritance position after `class Foo :` (Phase 0.5)
            //   ',' — multi-inheritance continuation, and the next arg of an enum-param call
            //   '(' — first arg of a call whose parameter is an enum (enum-argument completion)
            //   '{' — an attributeList initializer literal (`attributes = { … }`)
            // A trigger that doesn't match its phase simply returns nothing — no dropdown pops.
            {"triggerCharacters", {".", "=", "<", "\"", "/", ":", ",", "(", "{"}}
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
        // Detect #bgl / #bglDecl / #bglStmt
        if(c == '#' && i+3 < n && docText[i+1] == 'b' && docText[i+2] == 'g' && docText[i+3] == 'l'){
            // Word-boundary check after `bgl`: a non-identifier follow-up means plain #bgl;
            // an alphabetic follow-up may be the recognized suffixes "Decl" or "Stmt"
            // (mirroring the lexer's getRawTextUntilCloseOrBgl matcher).
            int afterTag = i + 4;
            char b = (afterTag < n) ? docText[afterTag] : '\0';
            bool extended = false;
            if(isalpha((unsigned char)b)){
                auto matchSuffix = [&](const char* suf){
                    int p = afterTag, k = 0;
                    while(suf[k]){
                        if(p >= n || docText[p] != suf[k]) return -1;
                        p++; k++;
                    }
                    char e = (p < n) ? docText[p] : '\0';
                    if(isalnum((unsigned char)e) || e == '_') return -1;
                    return p;
                };
                int p = matchSuffix("Decl");
                if(p < 0) p = matchSuffix("Stmt");
                if(p >= 0){ afterTag = p; extended = true; }
                else { i++; continue; }  // alphanumeric not matching a known suffix — skip
            } else if(b == '_' || isdigit((unsigned char)b)){
                i++; continue;  // identifier extension — not a directive
            }
            (void)extended;  // for now both forms participate as bgl regions for handler gating
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

// Canonicalize a filesystem path for equality comparison (resolves symlinks/.. when the file
// exists; falls back to the raw string otherwise).
static string canonPath(const string& p) {
    try { return filesystem::canonical(filesystem::absolute(p)).string(); }
    catch(...) { return p; }
}

// Designate (or clear) the project entry point. `params.uri` (a file URI) or `params.path`
// names the entry-point .bgl; an empty/absent value clears it. On change, invalidate the
// last-parsed marker so the next feature request reparses in the (new) context.
void LspServer::handleSetEntryPoint(const json& params) {
    string p;
    if(params.contains("uri") && params["uri"].is_string())
        p = uriToPath(params["uri"].get<string>());
    else if(params.contains("path") && params["path"].is_string())
        p = params["path"].get<string>();
    string newEntry = p.empty() ? string() : canonPath(p);
    if(newEntry == entryPointPath) return;  // no change → nothing to re-publish
    entryPointPath = newEntry;
    lastParsedUri.clear();  // force a fresh context parse on the next request

    // Re-parse and re-publish EVERY open document under the new entry-point context.
    // Without this, a document opened BEFORE this notification arrived keeps its stale
    // decorations. The concrete case: on window reload VS Code restores the focused editor
    // and the client's textDocument/didOpen races ahead of the entry-point re-assert. If the
    // focused file is an *included* core file (e.g. _glulxCore.bgl), it gets parsed standalone
    // first — so a `#if generateBlorb` region grays out and its members vanish from completion —
    // and clearing lastParsedUri alone never refreshes it (nothing re-requests until the user
    // edits). Proactively refreshing here makes an entry-point change take effect immediately
    // for all open files, whether it arrives on reload or from a mid-session Set Entry Point.
    vector<string> openUris;
    openUris.reserve(openDocuments.size());
    for(auto& kv : openDocuments) openUris.push_back(kv.first);
    for(const string& u : openUris) {
        parseDocument(u);
        publishDiagnostics(u);
        publishInactiveRegions(u);
    }
}

// Editor settings pushed from the extension. Currently just `syntaxHints` (default on): toggles the
// keyword syntax-snippet completions (e.g. the `enum` declaration popup). Completion is pulled per
// keystroke, so the next request honors the new value — no reparse needed.
void LspServer::handleSetConfig(const json& params) {
    if(params.contains("syntaxHints") && params["syntaxHints"].is_boolean())
        syntaxHintsEnabled = params["syntaxHints"].get<bool>();
}

// Parse `uri` rooted at the entry point so an opened *included* file resolves with whole-program
// context (settings symbols, `#if` gating, cross-file symbols). Returns true when the entry point
// actually includes `uri` (caller then skips the standalone parse); false to fall back.
bool LspServer::parseDocumentInEntryContext(const string& uri) {
    if(entryPointPath.empty()) return false;
    string openedCanon = canonPath(uriToPath(uri));
    string entryCanon  = canonPath(entryPointPath);
    if(openedCanon == entryCanon) return false;  // the entry point itself → parse standalone

    // Fresh state (mirrors parseDocument's reset + include-path reseed).
    parser.reset();
    languageService.reset();
    beguilerSettings = beguilerSettingsDef();
    beguilerSettings.includePaths = cliIncludePaths;
    parser.lspMode = true;
    parser.lspErrors.clear();
    parser.inactiveRegions.clear();

    // Resolve the entry point's source text: its live editor buffer if open, else disk.
    string entryContent;
    bool haveEntry = false;
    for(auto& [u, text] : openDocuments)
        if(canonPath(uriToPath(u)) == entryCanon) { entryContent = text; haveEntry = true; break; }
    if(!haveEntry) {
        ifstream f(entryPointPath, ios::binary);
        if(!f) return false;  // entry point unreadable → fall back to standalone
        stringstream ss; ss << f.rdbuf(); entryContent = ss.str();
    }

    // Overlay EVERY open buffer so unsaved edits in any file (the opened include, the entry
    // point, sibling includes) are honored during the whole-program parse.
    g_virtualBglFiles.clear();
    for(auto& [u, text] : openDocuments)
        g_virtualBglFiles[virtualFileKey(uriToPath(u))] = text;

    // Seed settings symbols from the ENTRY POINT (not the opened include, which has no
    // #beguilerSettings): target_* must be defined before preScanFile walks the core library,
    // and generateblorb gates blorb-only members. Mirrors parseDocument's standalone pre-scan.
    bool genBlorb = false;
    {
        const string& text = entryContent;
        size_t tagPos = 0;
        while((tagPos = text.find("#beguilerSettings", tagPos)) != string::npos) {
            size_t open = text.find('{', tagPos);
            if(open == string::npos) break;
            int depth = 1; size_t cur = open + 1;
            while(cur < text.size() && depth > 0) { if(text[cur]=='{') depth++; else if(text[cur]=='}') depth--; cur++; }
            string block = text.substr(open + 1, cur - open - 2);
            preReadIncludePaths(block, entryPointPath);   // as the compiler does before its pre-scan
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
                        if(val.rfind("etarget.", 0) == 0) val = val.substr(8);   // the qualified form
                        if(val == "glulx") parser.defineSymbol("target_glulx");
                        else if(val.size() == 2 && val[0] == 'z' && isdigit(val[1]))
                            parser.defineSymbol("target_zcode");
                    }
                }
            }
            size_t gb = lower.find("generateblorb");
            if(gb != string::npos) {
                size_t eq = lower.find('=', gb + 13);
                if(eq != string::npos) {
                    size_t vs = lower.find_first_not_of(" \t\r\n", eq + 1);
                    if(vs != string::npos) {
                        size_t ve = lower.find_first_of(" \t\r\n;}", vs);
                        if(lower.substr(vs, ve - vs) == "true") genBlorb = true;
                    }
                }
            }
            tagPos = cur;
        }
        parser.declareSymbol("generateblorb", genBlorb ? "true" : "false");
        // No target named: the default, Glulx, as in a compile.
        if(!parser.isSymbolDefined("target_glulx") && !parser.isSymbolDefined("target_zcode")) parser.defineSymbol("target_glulx");
    }

    // Register the virtual `_blorbAssets.bgl` from the ENTRY POINT's asset dir (mirrors the
    // standalone scan, but anchored at the entry point so `eAssets` matches the real build).
    {
        string assetPathSetting;
        const string& text = entryContent;
        size_t tagPos = 0;
        while((tagPos = text.find("#beguilerSettings", tagPos)) != string::npos) {
            size_t open = text.find('{', tagPos);
            if(open == string::npos) break;
            int depth = 1; size_t cur = open + 1;
            while(cur < text.size() && depth > 0) { if(text[cur]=='{') depth++; else if(text[cur]=='}') depth--; cur++; }
            string block = text.substr(open + 1, cur - open - 2);
            string lower = block;
            transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            size_t k = lower.find("blorbassetpath");
            if(k != string::npos) {
                size_t eq = lower.find('=', k + 14);
                if(eq != string::npos) {
                    size_t q1 = block.find('"', eq);
                    if(q1 != string::npos) { size_t q2 = block.find('"', q1 + 1);
                        if(q2 != string::npos) assetPathSetting = block.substr(q1 + 1, q2 - q1 - 1); }
                }
            }
            tagPos = cur;
        }
        filesystem::path srcDir = filesystem::path(entryPointPath).parent_path();
        string assetDir = assetPathSetting.empty()
            ? (srcDir / "assets").string()
            : (filesystem::path(assetPathSetting).is_absolute()
                ? assetPathSetting : (srcDir / assetPathSetting).string());
        error_code ec;
        if(filesystem::is_directory(assetDir, ec)) {
            Blorb blorb;
            vector<BlorbAsset> assets = blorb.scanAssets(assetDir);
            string enumSrc = blorb.buildEnumSource(assets, filesystem::path(entryPointPath).filename().string());
            g_virtualBglFiles[virtualFileKey((srcDir / "_blorbAssets.bgl").string())] = enumSrc;
        }
    }

    documentParsePaths[uri] = openedCanon;  // attribute symbols/regions to the OPENED file

    streambuf* oldCout = cout.rdbuf();
    ostringstream nullStream;
    cout.rdbuf(nullStream.rdbuf());
    vector<string> errors;
    try {
        parser.preScanFile(entryPointPath, &entryContent);
        parser.resolveStubParamAliases();
        parser.parseFile(entryPointPath, &entryContent);
    } catch(exception& e) { errors.push_back(e.what()); }
      catch(...) { errors.push_back("Unknown compilation error"); }
    cout.rdbuf(oldCout);

    // Coverage: did the entry point's include tree actually reach the opened file? If not, this
    // file is an orphan relative to the entry point — let the caller parse it standalone.
    if(!parser.includedFilePaths.count(openedCanon)) {
        parser.lspMode = false;
        return false;
    }

    // Keep only diagnostics that belong to the opened file (the whole-program parse surfaces
    // errors across every included file; the client asked about this one).
    vector<string> ownErrors;
    for(const string& e : errors) ownErrors.push_back(e);  // exceptions aren't file-tagged; keep them
    for(const string& e : parser.lspErrors) {
        size_t firstColon = e.find(':');
        string filePart = (firstColon == string::npos) ? string() : e.substr(0, firstColon);
        if(canonPath(filePart) == openedCanon) ownErrors.push_back(e);
    }
    parser.lspMode = false;

    documentDiagnostics[uri] = ownErrors;
    lastParsedUri = uri;
    return true;
}

void LspServer::parseDocument(const string& uri) {
    // Entry-point context parse: when an included file is opened and an entry point is set, parse
    // the whole program so #if gating, settings symbols, and cross-file symbols resolve correctly.
    // Falls through to the standalone parse below when there's no entry point, this IS the entry
    // point, or the entry point doesn't include this file.
    if(parseDocumentInEntryContext(uri)) return;

    string path = uriToPath(uri);
    documentDiagnostics.clear();

    // Reset all parser/language-service/settings state before every parse. Without this,
    // declarations from previously-parsed documents leak in: e.g. opening file A that does
    // `extend class object { string description; }` then opening file B that also adds
    // `description` on object would produce a spurious "already defined" error in B (and
    // includePaths accumulated from A could mask file-not-found diagnostics in B). Each
    // open document is parsed in isolation; the parser holds the last-parsed file's state
    // for hover/completion, which always reflects whatever document the user just edited.
    parser.reset();
    languageService.reset();
    beguilerSettings = beguilerSettingsDef();
    // Re-seed CLI include paths (from -includepaths=, i.e. the beguiler.includePaths setting):
    // the reset above wipes them and LSP mode never runs parseArgs(). The document's own
    // #beguilerSettings.includePaths is appended later during the parse. Without this, quoted
    // `#include "…"` can't see configured include dirs (completion + file-not-found diagnostics).
    beguilerSettings.includePaths = cliIncludePaths;

    // Pass the editor buffer to the parser via the in-memory contentOverride parameter, so we
    // never write a temp file alongside the source. This avoids the workspace-file-list churn
    // that the previous .lsp_tmp file caused on every keystroke. The parser still uses `path`
    // for include resolution (sourceDir) and error reporting.
    auto docIt = openDocuments.find(uri);
    const string* content = (docIt != openDocuments.end()) ? &docIt->second : nullptr;

    // Suppress compiler stdout output
    streambuf* oldCout = cout.rdbuf();
    ostringstream nullStream;
    cout.rdbuf(nullStream.rdbuf());

    // Store the canonical path so documentSymbol can filter to this file
    try {
        documentParsePaths[uri] = filesystem::canonical(filesystem::absolute(path)).string();
    } catch(...) {
        documentParsePaths[uri] = path;
    }

    // Enable LSP error recovery mode — parser collects errors instead of halting
    parser.lspMode = true;
    parser.lspErrors.clear();
    parser.inactiveRegions.clear();

    // Pre-scan the document text for #beguilerSettings target so TARGET_ZCODE/TARGET_GLULX are
    // defined BEFORE preScanFile walks the core library (which uses them to pick a backend).
    // Also declare `generateblorb` (with its bool value) so `#if (generateBlorb == true)` gates —
    // e.g. the `extend _bglPrintRules { void img(eAssets …) }` in _glulxCore.bgl — resolve the same
    // way they do in a real compile; otherwise blorb-gated members are invisible to hover/completion.
    // This mirrors beguiler::extractBlorbSettings + the declareSymbol at beguiler.cpp (compile mode).
    if(docIt != openDocuments.end()) {
        const string& text = docIt->second;
        bool genBlorb = false;
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
            preReadIncludePaths(block, path);   // as the compiler does before its pre-scan
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
                        if(val.rfind("etarget.", 0) == 0) val = val.substr(8);   // the qualified form
                        if(val == "glulx")                              parser.defineSymbol("target_glulx");
                        else if(val.size() == 2 && val[0] == 'z' && isdigit(val[1]))
                            parser.defineSymbol("target_zcode");
                    }
                }
            }
            size_t gb = lower.find("generateblorb");
            if(gb != string::npos) {
                size_t eq = lower.find('=', gb + 13);
                if(eq != string::npos) {
                    size_t vs = lower.find_first_not_of(" \t\r\n", eq + 1);
                    if(vs != string::npos) {
                        size_t ve = lower.find_first_of(" \t\r\n;}", vs);
                        if(lower.substr(vs, ve - vs) == "true") genBlorb = true;
                    }
                }
            }
            tagPos = cur;
        }
        // Always declare (matches compile mode: declared true|false, never absent) so both
        // `#if (generateBlorb == true)` and bare `#if generateBlorb` presence-tests behave.
        parser.declareSymbol("generateblorb", genBlorb ? "true" : "false");
        // No target named: the default, Glulx, as in a compile.
        if(!parser.isSymbolDefined("target_glulx") && !parser.isSymbolDefined("target_zcode")) parser.defineSymbol("target_glulx");
    }

    // Register a virtual `_blorbAssets.bgl` from a live scan of the asset directory, so that
    // `#include "_blorbAssets.bgl"` resolves — and `eAssets` members autocomplete/color —
    // WITHOUT a compile and WITHOUT writing anything to disk. The scan re-runs on every
    // reparse (so a freshly dropped image appears on the next parse), and the live result
    // wins over any stale on-disk copy (see resolveIncludePath). Mirrors the compile-mode
    // asset scan in beguiler.cpp. If the asset dir is absent we register nothing, leaving a
    // previously-compiled on-disk _blorbAssets.bgl to resolve normally.
    g_virtualBglFiles.clear();
    {
        // Extract blorbAssetPath = "…" from any #beguilerSettings block (default: "assets").
        string assetPathSetting;
        if(docIt != openDocuments.end()){
            const string& text = docIt->second;
            size_t tagPos = 0;
            while((tagPos = text.find("#beguilerSettings", tagPos)) != string::npos){
                size_t open = text.find('{', tagPos);
                if(open == string::npos) break;
                int depth = 1; size_t cur = open + 1;
                while(cur < text.size() && depth > 0){
                    if(text[cur] == '{') depth++;
                    else if(text[cur] == '}') depth--;
                    cur++;
                }
                string block = text.substr(open + 1, cur - open - 2);
                string lower = block;
                transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                size_t k = lower.find("blorbassetpath");
                if(k != string::npos){
                    size_t eq = lower.find('=', k + 14);
                    if(eq != string::npos){
                        size_t q1 = block.find('"', eq);
                        if(q1 != string::npos){
                            size_t q2 = block.find('"', q1 + 1);
                            if(q2 != string::npos) assetPathSetting = block.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                }
                tagPos = cur;
            }
        }
        filesystem::path srcDir = filesystem::path(path).parent_path();
        string assetDir = assetPathSetting.empty()
            ? (srcDir / "assets").string()
            : (filesystem::path(assetPathSetting).is_absolute()
                ? assetPathSetting
                : (srcDir / assetPathSetting).string());
        error_code ec;
        if(filesystem::is_directory(assetDir, ec)){
            Blorb blorb;
            vector<BlorbAsset> assets = blorb.scanAssets(assetDir);
            string enumSrc = blorb.buildEnumSource(assets, filesystem::path(path).filename().string());
            g_virtualBglFiles[virtualFileKey((srcDir / "_blorbAssets.bgl").string())] = enumSrc;
        }
    }

    vector<string> errors;
    try {
        parser.preScanFile(path, content);
        parser.resolveStubParamAliases();
        parser.parseFile(path, content);
    } catch(exception& e) {
        errors.push_back(e.what());
    } catch(...) {
        errors.push_back("Unknown compilation error");
    }
    // Collect any errors gathered during recovery
    for(const string& e : parser.lspErrors) errors.push_back(e);
    parser.lspMode = false;

    cout.rdbuf(oldCout);  // restore cout

    documentDiagnostics[uri] = errors;
    lastParsedUri = uri;  // shared parser/languageService state now reflects this document
}

// Ensure the shared parse reflects the document a feature request targets. parseDocument holds
// only the last-parsed file's symbol table (each open doc is parsed in isolation, resetting state),
// so completing/hovering in file B right after editing included file A would otherwise resolve
// against A's parse. Reparsing B here restores a self-consistent view. No-op if already current.
void LspServer::ensureParsedForRequest(const string& uri) {
    if(uri == lastParsedUri) return;
    if(openDocuments.find(uri) == openDocuments.end()) return;
    parser.reset();
    languageService.reset();
    parseDocument(uri);
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
            // An error in another file (an #include) has that file's line: show it at the top of this
            // document, naming where it is.
            if(firstColon != string::npos && firstColon > 0 && line + col > 0) {
                string errFile = err.substr(0, firstColon);
                if(canonPath(errFile) != canonPath(uriToPath(uri))) {
                    if(severity == 2) continue;   // another file's warning belongs to that file
                    message = filesystem::path(errFile).filename().string() + ":" + to_string(line + 1) + ": " + message;
                    line = 0; col = 0;
                }
            }

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

// Offset of the first character of 0-based `line0`; npos past the end of the text.
static size_t lineStartOffset(const string& t, int line0) {
    size_t i = 0;
    for(int l = 0; l < line0; l++) {
        i = t.find('\n', i);
        if(i == string::npos) return string::npos;
        i++;
    }
    return i;
}

// Offset just past the comment, string or character literal that starts at `i`; `i` itself when none does.
static size_t skipLiteral(const string& t, size_t i) {
    size_t n = t.size();
    if(i >= n) return i;
    if(t.compare(i, 2, "//") == 0) { while(i < n && t[i] != '\n') i++; return i; }
    if(t.compare(i, 2, "/*") == 0) { size_t e = t.find("*/", i + 2); return e == string::npos ? n : e + 2; }
    if(t[i] == '"' || t[i] == '\'') {
        char q = t[i++];
        while(i < n && t[i] != q && !(q == '\'' && t[i] == '\n')) i += (t[i] == '\\' ? 2 : 1);
        return std::min(i + 1, n);
    }
    return i;
}

// Offset just past the '}' that closes the '{' at `open`; the end of the text when it never closes.
static size_t matchBrace(const string& t, size_t open) {
    int depth = 0;
    for(size_t i = open; i < t.size();) {
        size_t j = skipLiteral(t, i);
        if(j != i) { i = j; continue; }
        if(t[i] == '{') depth++;
        else if(t[i] == '}' && --depth == 0) return i + 1;
        i++;
    }
    return t.size();
}

static bool isWordChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// The names of the parameters in a lambda's parameter list text (`int a, func<int,int> f`), lowercased.
static vector<string> lambdaParamNames(const string& list) {
    vector<string> names;
    int depth = 0;
    string seg;
    auto flush = [&]() {
        size_t e = seg.find_last_not_of(" \t\r\n");
        if(e != string::npos) {
            size_t b = e;
            while(b > 0 && isWordChar(seg[b - 1])) b--;
            string w = seg.substr(b, e - b + 1);
            transform(w.begin(), w.end(), w.begin(), ::tolower);
            names.push_back(w);
        }
        seg.clear();
    };
    for(char c : list) {
        if(c == '<' || c == '(') depth++;
        else if(c == '>' || c == ')') depth--;
        if(c == ',' && depth == 0) flush();
        else seg += c;
    }
    flush();
    return names;
}

// A lambda records only a line near its text, and the lexer's lookahead can leave even that a line or two
// off, so its text is found by its `=>`: the one nearby whose parameter names are the lambda's own (or, for
// `=> body`, which follows no parameter list). Lambdas written alike take the occurrences in turn.
static bool lambdaSpan(functionDef* fd, const string& text, LspFunctionSpan& sp, std::set<size_t>* claimed) {
    size_t n = text.size();
    size_t srcOff = lineStartOffset(text, fd->src.line - 1);
    if(srcOff == string::npos) return false;
    size_t from = lineStartOffset(text, std::max(fd->src.line - 4, 0));
    size_t to = lineStartOffset(text, fd->src.line + 3);
    if(to == string::npos) to = n;
    vector<string> want;
    for(paramDef* p : fd->params) { string w = p->name; transform(w.begin(), w.end(), w.begin(), ::tolower); want.push_back(w); }
    size_t bestArrow = string::npos, bestBegin = 0, bestDist = string::npos;
    bool bestClaimed = true;
    for(size_t i = from; i < to;) {
        size_t j = skipLiteral(text, i);
        if(j != i) { i = j; continue; }
        if(text.compare(i, 2, "=>") != 0) { i++; continue; }
        size_t arrow = i;
        i += 2;
        size_t h = arrow;
        while(h > 0 && isspace((unsigned char)text[h - 1])) h--;
        size_t begin = arrow;
        vector<string> names;
        if(h > 0 && text[h - 1] == ')') {
            int depth = 0;
            size_t k = h - 1;
            for(;; k--) {
                if(text[k] == ')') depth++;
                else if(text[k] == '(' && --depth == 0) break;
                if(k == 0) break;
            }
            if(text[k] != '(') continue;
            begin = k;
            names = lambdaParamNames(text.substr(k + 1, h - 2 - k));
        }
        if(names != want) continue;
        bool isClaimed = claimed && claimed->count(arrow);
        size_t dist = begin > srcOff ? begin - srcOff : srcOff - begin;
        if(bestArrow == string::npos || (bestClaimed && !isClaimed) || (bestClaimed == isClaimed && dist < bestDist)) {
            bestArrow = arrow; bestBegin = begin; bestDist = dist; bestClaimed = isClaimed;
        }
    }
    if(bestArrow == string::npos) return false;
    if(claimed) claimed->insert(bestArrow);
    sp.begin = bestBegin;
    sp.bodyOpen = string::npos;
    size_t body = bestArrow + 2;
    while(body < n && isspace((unsigned char)text[body])) body++;
    if(body < n && text[body] == '{') { sp.bodyOpen = body; sp.end = matchBrace(text, body); return true; }
    // An expression body ends at the first `)`, `]`, `}`, `,` or `;` that is not inside brackets of its own.
    int depth = 0;
    size_t i = body;
    while(i < n) {
        size_t j = skipLiteral(text, i);
        if(j != i) { i = j; continue; }
        char c = text[i];
        if(c == '(' || c == '[' || c == '{') depth++;
        else if(c == ')' || c == ']' || c == '}') { if(depth == 0) break; depth--; }
        else if(depth == 0 && (c == ',' || c == ';')) break;
        i++;
    }
    sp.end = i;
    return true;
}

// Where a routine's text sits. A routine records the line of its name.
bool LspServer::functionSpan(functionDef* fd, const string& text, FunctionSpan& sp, std::set<size_t>* claimed) {
    if(!fd || fd->src.line <= 0) return false;
    size_t ls = lineStartOffset(text, fd->src.line - 1);
    if(ls == string::npos) return false;
    size_t n = text.size();
    size_t at = std::min(ls + (size_t)std::max(fd->src.col - 1, 0), n);
    sp.begin = ls;
    sp.bodyOpen = string::npos;
    // A signature may start on earlier lines (`emitter` and the return type above the name).
    for(int up = 0; up < 8 && !fd->isLambda; up++) {
        size_t q = sp.begin;
        while(q > 0 && isspace((unsigned char)text[q - 1])) q--;
        if(q == 0 || text[q - 1] == ';' || text[q - 1] == '{' || text[q - 1] == '}') break;
        size_t prev = text.rfind('\n', q - 1);
        sp.begin = prev == string::npos ? 0 : prev + 1;
    }
    if(fd->isLambda) return lambdaSpan(fd, text, sp, claimed);
    int depth = 0;
    size_t i = at;
    sp.end = at;
    while(i < n) {
        size_t j = skipLiteral(text, i);
        if(j != i) { i = j; continue; }
        char c = text[i];
        if(c == '(' || c == '[') depth++;
        else if(c == ')' || c == ']') depth--;
        else if(depth <= 0 && c == '{') { sp.bodyOpen = i; sp.end = matchBrace(text, i); return true; }
        else if(depth <= 0 && (c == ';' || c == '}')) { sp.end = i; return true; }
        i++;
    }
    sp.end = n;
    return true;
}

// The routines in the current file whose text holds (line, col), innermost first.
LspServer::FunctionContext LspServer::functionContext(const string& uri, int line, int col) {
    FunctionContext ctx;
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return ctx;
    const string& text = docIt->second;
    string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
    size_t cursor = std::min((size_t)std::max(positionToOffset(text, line, col), 0), text.size());
    struct Hit { functionDef* fn; FunctionSpan span; classDef* cls; objectDef* obj; };
    vector<Hit> hits;
    std::set<functionDef*> seen;
    std::set<size_t> claimed;
    auto consider = [&](typeMember* m, classDef* cls, objectDef* obj) {
        // A routine a later `replace` overrode is gone from its class but still holds the text it was written in.
        for(auto* fd = dynamic_cast<functionDef*>(m); fd; fd = fd->replacedFunc) {
            if(fd->src.line == 0 || fd->src.line > line + 1 + 8 || fd->src.file != curFile || !seen.insert(fd).second) continue;
            FunctionSpan sp;
            if(!functionSpan(fd, text, sp, &claimed) || cursor < sp.begin || cursor > sp.end) continue;
            hits.push_back({fd, sp, cls, obj});
        }
    };
    for(typeDef* g : languageService.globals) {
        consider(dynamic_cast<functionDef*>(g), nullptr, nullptr);
        if(auto* od = dynamic_cast<objectDef*>(g))
            for(typeMember* m : od->members) consider(m, nullptr, od);
    }
    for(typeDef* t : languageService.objectTypes) {
        if(auto* cd = dynamic_cast<classDef*>(t)) {
            for(typeMember* m : cd->members) consider(m, cd, nullptr);
        } else if(auto* ed = dynamic_cast<enumDef*>(t)) {
            if(ed->companion) for(typeMember* m : ed->companion->members) consider(m, ed->companion, nullptr);
        }
    }
    for(typeDef* t : languageService.objectInstances)
        if(auto* od = dynamic_cast<objectDef*>(t))
            for(typeMember* m : od->members) consider(m, nullptr, od);
    for(verbObjectDef* v : languageService.verbs)
        for(typeMember* m : v->members) consider(m, nullptr, v);
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.span.begin > b.span.begin; });
    for(const Hit& h : hits) {
        ctx.chain.push_back(h.fn);
        ctx.spans.push_back(h.span);
        if(h.cls || h.obj) { ctx.ownerClass = h.cls; ctx.ownerObject = h.obj; }
    }
    return ctx;
}

std::pair<int,int> LspServer::functionLines(const string& uri, int line, int col, functionDef* fn) {
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return {-1, -1};
    FunctionContext ctx = functionContext(uri, line, col);
    FunctionSpan sp;
    bool found = false;
    for(size_t i = 0; i < ctx.chain.size(); i++)
        if(ctx.chain[i] == fn) { sp = ctx.spans[i]; found = true; }
    if(!found && !functionSpan(fn, docIt->second, sp)) return {-1, -1};
    const string& t = docIt->second;
    auto lineOf = [&](size_t off) { return 1 + (int)std::count(t.begin(), t.begin() + std::min(off, t.size()), '\n'); };
    return {lineOf(sp.begin), lineOf(sp.end)};
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

// Source text of a file: the open buffer when one holds it, else the file on disk.
bool LspServer::sourceTextOf(const string& path, string& text) {
    for(auto& [docUri, docText] : openDocuments) {
        string docPath = documentParsePaths.count(docUri) ? documentParsePaths[docUri] : uriToPath(docUri);
        if(docPath == path) { text = docText; return true; }
    }
    ifstream in(path, ios::binary);
    if(!in) return false;
    stringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

// Whether `lower` (lowercase) occurs in `line` as a whole word, ignoring case.
static bool lineHasWord(const string& line, const string& lower) {
    string l = line;
    transform(l.begin(), l.end(), l.begin(), ::tolower);
    for(size_t pos = l.find(lower); pos != string::npos; pos = l.find(lower, pos + 1))
        if((pos == 0 || !isWordChar(l[pos - 1])) && (pos + lower.size() >= l.size() || !isWordChar(l[pos + lower.size()])))
            return true;
    return false;
}

// A declaration's recorded line can sit beside the one that names it (an object's `{` on its own line, a
// parameter in a multi-line list); look a few lines up, then down, for the name.
sourceLocation LspServer::settleDeclLine(const sourceLocation& src, const string& nameLower) {
    string text;
    if(src.line <= 0 || nameLower.empty() || !sourceTextOf(src.file, text)) return src;
    vector<string> lines;
    { istringstream in(text); string l; while(getline(in, l)) lines.push_back(l); }
    auto has = [&](int l0) { return l0 >= 0 && l0 < (int)lines.size() && lineHasWord(lines[l0], nameLower); };
    int at = src.line - 1;
    if(has(at)) return src;
    sourceLocation moved = src;
    for(int d = 1; d <= 5; d++) if(has(at - d)) { moved.line = src.line - d; return moved; }
    for(int d = 1; d <= 3; d++) if(has(at + d)) { moved.line = src.line + d; return moved; }
    return src;
}

// 1-based line of the last declaration of `lower` in text[begin, end) that starts at or before `limit`
// (the first one after it when none does); 0 when there is none. A declaration is the name written
// after a type, as in `int k`, `array<int> a` or `catch (int e)`.
static int declarationLine(const string& text, size_t begin, size_t end, const string& lower, size_t limit) {
    static const std::set<string> notTypes = {"return", "new", "else", "case", "in", "print", "throw", "delete", "do", "is", "as", "to"};
    end = std::min(end, text.size());
    size_t foundAt = string::npos, firstAfter = string::npos;
    for(size_t i = begin; i < end;) {
        size_t j = skipLiteral(text, i);
        if(j != i) { i = j; continue; }
        if(!isWordChar(text[i]) || (i > begin && isWordChar(text[i - 1]))) { i++; continue; }
        size_t e = i;
        while(e < end && isWordChar(text[e])) e++;
        string w = text.substr(i, e - i);
        transform(w.begin(), w.end(), w.begin(), ::tolower);
        size_t at = i;
        i = e;
        if(w != lower) continue;
        size_t p = at;
        while(p > begin && isspace((unsigned char)text[p - 1])) p--;
        if(p == begin) continue;
        char pc = text[p - 1];
        if(!(isWordChar(pc) || pc == '>' || pc == ']')) continue;
        if(isWordChar(pc)) {
            size_t ws = p;
            while(ws > begin && isWordChar(text[ws - 1])) ws--;
            string pw = text.substr(ws, p - ws);
            transform(pw.begin(), pw.end(), pw.begin(), ::tolower);
            if(notTypes.count(pw)) continue;
        }
        size_t q = e;
        while(q < end) {
            if(isspace((unsigned char)text[q])) q++;
            else if(text[q] == '/' && skipLiteral(text, q) != q) q = skipLiteral(text, q);
            else break;
        }
        bool endsDecl = q < end && string("=;,):[").find(text[q]) != string::npos;
        if(!endsDecl) {   // `in` of a for-in loop, or the `asI6`/`asBgl` clause of a declaration
            size_t we = q;
            while(we < end && isWordChar(text[we])) we++;
            string next = text.substr(q, we - q);
            transform(next.begin(), next.end(), next.begin(), ::tolower);
            endsDecl = next == "in" || next == "asi6" || next == "asbgl";
        }
        if(!endsDecl) continue;
        if(at <= limit) foundAt = at;
        else if(firstAfter == string::npos) firstAfter = at;
    }
    size_t use = foundAt != string::npos ? foundAt : firstAfter;
    if(use == string::npos) return 0;
    return 1 + (int)std::count(text.begin(), text.begin() + use, '\n');
}

// Where `ed` declares `valueLower`.
sourceLocation LspServer::enumValueSource(enumDef* ed, const string& valueLower) {
    string text;
    if(!ed || ed->src.line <= 0 || !sourceTextOf(ed->src.file, text)) return ed ? ed->src : sourceLocation();
    size_t from = lineStartOffset(text, std::max(ed->src.line - 2, 0));
    size_t open = from == string::npos ? string::npos : text.find('{', from);
    if(open == string::npos) return ed->src;
    size_t end = matchBrace(text, open);
    int line = declarationLine(text, open, end, valueLower, string::npos);
    if(line == 0) {
        for(size_t i = open + 1; i < end;) {
            size_t j = skipLiteral(text, i);
            if(j != i) { i = j; continue; }
            if(isWordChar(text[i]) && !isWordChar(text[i - 1])) {
                size_t e = i;
                while(e < end && isWordChar(text[e])) e++;
                string w = text.substr(i, e - i);
                transform(w.begin(), w.end(), w.begin(), ::tolower);
                if(w == valueLower) { line = 1 + (int)std::count(text.begin(), text.begin() + i, '\n'); break; }
                i = e;
            } else i++;
        }
    }
    sourceLocation out = ed->src;
    if(line > 0) out.line = line;
    return out;
}

static void fillFromMember(LspSymbolRef& out, typeMember* m, classDef* cls) {
    out.kind = LspSymbolRef::ClassMember;
    out.enclosingClass = cls;
    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
        out.typeName = vd->type.name;
        out.decl = vd;
        out.declSrc = vd->src;
        if(vd->isAlias) out.kind = LspSymbolRef::Alias;
    } else if(auto* fd = dynamic_cast<functionDef*>(m)) {
        out.typeName = fd->returnType.name;
        out.isFunction = true;
        out.declSrc = fd->src;
    }
    out.displayName = m->dName();
    out.docComment = m->docComment;
}

static vector<string> activeUsingPaths(const string& docText, int line);

// The member of `members` named `lower`, a pre-scan stub only when nothing else carries the name.
static typeMember* findNamedMember(const vector<typeMember*>& members, const string& lower) {
    typeMember* stub = nullptr;
    for(typeMember* m : members) {
        if(m->name != lower || !(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))) continue;
        if(!m->isPrePassStub) return m;
        if(!stub) stub = m;
    }
    return stub;
}

static bool isNamedValueOrRoutine(typeMember* m, const string& lower) {
    return m->name == lower && (dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m));
}

// resolveSymbol tier: a class member whose declaration sits on the cursor line. Handles hover at a
// member declaration line (inside a class body, not in a method).
bool LspServer::resolveTierSourceLineClassMember(const string& curFile, int cursorLine1Based,
                                                 const string& loweredName, LspSymbolRef& out) {
    for(typeDef* t : languageService.objectTypes) {
        auto* cd = dynamic_cast<classDef*>(t);
        if(!cd) continue;
        for(typeMember* m : cd->members) {
            if(!isNamedValueOrRoutine(m, loweredName)) continue;
            sourceLocation src = dynamic_cast<variableDeclaration*>(m) ? static_cast<variableDeclaration*>(m)->src
                                                                       : static_cast<functionDef*>(m)->src;
            if(src.file == curFile && src.line == cursorLine1Based) { fillFromMember(out, m, cd); return true; }
        }
    }
    return false;
}

// resolveSymbol tier: an object member whose declaration sits on the cursor line — the members of an
// instance, an extern object or a verb. Handles hover inside a `room foyer { short_name = ...; }`
// initializer, where the property assignment is stored as a member on the object with its own line. For
// definition, prefer jumping to the owning class's member declaration rather than the instance override.
bool LspServer::resolveTierSourceLineInstanceMember(const string& curFile, int cursorLine1Based,
                                                    const string& loweredName, LspSymbolRef& out) {
    std::set<objectDef*> seen;
    vector<objectDef*> objects;
    auto add = [&](typeDef* t) { if(auto* od = dynamic_cast<objectDef*>(t)) if(seen.insert(od).second) objects.push_back(od); };
    for(typeDef* t : languageService.objectInstances) add(t);
    for(typeDef* t : languageService.globals) add(t);
    for(verbObjectDef* v : languageService.verbs) add(v);
    for(objectDef* od : objects) {
        for(typeMember* m : od->members) {
            if(!isNamedValueOrRoutine(m, loweredName)) continue;
            sourceLocation src = dynamic_cast<variableDeclaration*>(m) ? static_cast<variableDeclaration*>(m)->src
                                                                       : static_cast<functionDef*>(m)->src;
            if(src.file != curFile || src.line != cursorLine1Based) continue;
            fillFromMember(out, m, od->objectClass);
            if(od->objectClass)
                if(typeMember* hit = od->objectClass->findMember([&](typeMember* c) { return isNamedValueOrRoutine(c, loweredName); })) {
                    sourceLocation cs = dynamic_cast<variableDeclaration*>(hit) ? static_cast<variableDeclaration*>(hit)->src
                                                                                : static_cast<functionDef*>(hit)->src;
                    if(cs.line > 0) out.declSrc = cs;
                }
            return true;
        }
    }
    return false;
}

// The object whose member an inline accessor's class is (`outer` in its routines); nullptr for any other class.
static objectDef* accessorHost(classDef* c) {
    if(!c || c->name.rfind("_bglaccessor", 0) != 0) return nullptr;
    auto holds = [&](objectDef* od) {
        for(typeMember* m : od->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->type.name == c->name) return true;
        return false;
    };
    for(typeDef* t : languageService.objectInstances)
        if(auto* od = dynamic_cast<objectDef*>(t); od && holds(od)) return od;
    for(typeDef* t : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(t); od && holds(od)) return od;
    for(verbObjectDef* v : languageService.verbs)
        if(holds(v)) return v;
    return nullptr;
}

// A local named `name` declared in `blk` or a block nested in it.
struct LocalCandidate { variableDeclaration* vd = nullptr; string catchType; };
static void collectLocals(statementBlock* blk, const string& name, vector<LocalCandidate>& out) {
    if(!blk) return;
    for(statement* st : blk->statements) {
        if(auto* vd = dynamic_cast<variableDeclaration*>(st)) {
            if(vd->name == name && !vd->isSynthetic) out.push_back({vd, ""});
        }
        else if(auto* is = dynamic_cast<ifStatement*>(st))      { collectLocals(is->thenBlock, name, out); collectLocals(is->elseBlock, name, out); }
        else if(auto* ws = dynamic_cast<whileStatement*>(st))   collectLocals(ws->body, name, out);
        else if(auto* ds = dynamic_cast<doStatement*>(st))      collectLocals(ds->body, name, out);
        else if(auto* fs = dynamic_cast<forStatement*>(st))     collectLocals(fs->body, name, out);
        else if(auto* fi = dynamic_cast<forInStatement*>(st))   collectLocals(fi->body, name, out);
        else if(auto* ss = dynamic_cast<switchStatement*>(st))  { for(switchCase* c : ss->cases) collectLocals(c->body, name, out); }
        else if(auto* tc = dynamic_cast<tryCatchStatement*>(st)) {
            if(tc->catchVarName == name) out.push_back({nullptr, tc->catchVarType});
            collectLocals(tc->tryBody, name, out);
            collectLocals(tc->catchBody, name, out);
        }
    }
}

// resolveSymbol tier: the routines around the cursor. Each, innermost lambda first, offers its locals
// (at any block depth, declared above the cursor) and parameters; then the members of the class or object
// owning the outermost one. Sets out.enclosingFunc even when it resolves nothing.
bool LspServer::resolveTierFunctionScope(const string& uri, int cursorLine, int cursorCol,
                                         const string& loweredName, LspSymbolRef& out) {
    FunctionContext ctx = functionContext(uri, cursorLine, cursorCol);
    if(ctx.chain.empty()) return false;
    out.enclosingFunc = ctx.chain.front();
    const string& text = openDocuments[uri];
    size_t cursorOff = std::min((size_t)std::max(positionToOffset(text, cursorLine, cursorCol), 0), text.size());
    for(size_t fi = 0; fi < ctx.chain.size(); fi++) {
        functionDef* fn = ctx.chain[fi];
        const FunctionSpan& sp = ctx.spans[fi];
        vector<LocalCandidate> cands;
        collectLocals(dynamic_cast<statementBlock*>(fn->body), loweredName, cands);
        const LocalCandidate* best = nullptr;
        int bestLine = 0;
        for(const LocalCandidate& c : cands) {
            int line = c.vd ? c.vd->src.line : 0;
            if(line == 0) line = declarationLine(text, sp.begin, sp.end, loweredName, cursorOff);
            if(line == 0 || line > cursorLine + 1) continue;
            if(!best || line > bestLine) { best = &c; bestLine = line; }
        }
        if(best) {
            out.kind = LspSymbolRef::Local;
            out.enclosingFunc = fn;
            out.typeName = best->vd ? best->vd->type.name : best->catchType;
            out.displayName = best->vd ? best->vd->dName() : loweredName;
            if(best->vd) out.docComment = best->vd->docComment;
            out.decl = best->vd;
            out.declSrc = fn->src;
            out.declSrc.line = bestLine;
            return true;
        }
        for(paramDef* p : fn->params) {
            if(p->name != loweredName) continue;
            out.kind = LspSymbolRef::Parameter;
            out.enclosingFunc = fn;
            out.typeName = p->type.name;
            out.displayName = p->dName();
            out.docComment = p->docComment;
            out.declSrc = fn->src;
            size_t sigEnd = sp.bodyOpen != string::npos ? sp.bodyOpen : sp.end;
            if(int pl = declarationLine(text, sp.begin, sigEnd, loweredName, string::npos)) out.declSrc.line = pl;
            return true;
        }
    }
    auto isMember = [&](typeMember* m) { return isNamedValueOrRoutine(m, loweredName); };
    if(ctx.ownerObject) {
        if(typeMember* m = findNamedMember(ctx.ownerObject->members, loweredName)) { fillFromMember(out, m, nullptr); return true; }
        if(classDef* oc = ctx.ownerObject->objectClass)
            if(typeMember* hit = oc->findMember(isMember)) { fillFromMember(out, hit, oc->declaringClassOf(hit)); return true; }
    }
    if(classDef* c = ctx.ownerClass) {
        if(typeMember* hit = c->findMember(isMember)) { fillFromMember(out, hit, c->declaringClassOf(hit)); return true; }
        if(objectDef* host = accessorHost(c))   // the host's type aliases are in scope in its accessors
            if(auto* vd = dynamic_cast<variableDeclaration*>(findNamedMember(host->members, loweredName)); vd && vd->isNamespaceAlias()) {
                fillFromMember(out, vd, nullptr);
                return true;
            }
        for(const string& tp : c->typeParameters)
            if(tp == loweredName) {
                out.kind = LspSymbolRef::TypeParameter;
                out.enclosingClass = c;
                out.displayName = tp;
                transform(out.displayName.begin(), out.displayName.end(), out.displayName.begin(), ::toupper);
                out.declSrc = c->src;
                return true;
            }
    }
    return false;
}

// resolveSymbol tier: the body of a class, object, verb or inline `Type{ … }` literal holding the cursor
// outside any routine — the name is one of that type's members (`inline name;`, `y = 2` in `point{ y = 2; }`),
// found from the text of the header before the nearest unclosed '{'.
bool LspServer::resolveTierTypeBody(const string& uri, int cursorLine, int cursorCol, const string& loweredName, LspSymbolRef& out) {
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return false;
    const string& text = docIt->second;
    size_t cursor = std::min((size_t)std::max(positionToOffset(text, cursorLine, cursorCol), 0), text.size());
    int depth = 0;
    size_t open = string::npos;
    for(size_t i = cursor; i-- > 0;) {
        if(text[i] == '}') depth++;
        else if(text[i] == '{') { if(depth == 0) { open = i; break; } depth--; }
    }
    if(open == string::npos) return false;
    size_t b = open;
    while(b > 0 && text[b - 1] != ';' && text[b - 1] != '{' && text[b - 1] != '}') b--;
    string header;
    for(size_t i = b; i < open;) {   // the header without its comments
        size_t j = skipLiteral(text, i);
        if(j != i && text[i] == '/') { i = j; header += ' '; }
        else header += text[i++];
    }
    if(size_t c = header.find(':'); c != string::npos) header.erase(c);
    for(char closer : {']', '>'}) {   // a pool size or type parameters after the name
        size_t e = header.find_last_not_of(" \t\r\n");
        if(e == string::npos || header[e] != closer) continue;
        size_t opener = header.rfind(closer == ']' ? '[' : '<');
        if(opener != string::npos) header.erase(opener);
    }
    size_t e = header.find_last_not_of(" \t\r\n");
    if(e == string::npos || !isWordChar(header[e])) return false;
    size_t st = e;
    while(st > 0 && isWordChar(header[st - 1])) st--;
    string typeName = header.substr(st, e - st + 1);
    transform(typeName.begin(), typeName.end(), typeName.begin(), ::tolower);
    classDef* cls = nullptr;
    objectDef* obj = nullptr;
    typeDef& td = languageService.getType(typeName);
    if(auto* cd = dynamic_cast<classDef*>(&td)) cls = cd;
    else if(auto* od = dynamic_cast<objectDef*>(&td)) { obj = od; cls = od->objectClass; }
    for(verbObjectDef* v : languageService.verbs)
        if(v->name == typeName) { obj = v; cls = v->objectClass; }
    if(!cls && !obj) return false;
    LspTarget target;
    target.cls = cls;
    target.obj = obj;
    LspMember m = findMemberOn(target, loweredName);
    if(!m.member) return false;
    fillFromMember(out, m.member, m.owner);
    // A declaration of the name on this very line is the declaration (a later `replace` may have superseded it)
    size_t ls = lineStartOffset(text, cursorLine);
    size_t le = text.find('\n', ls);
    if(ls != string::npos && declarationLine(text, ls, le == string::npos ? text.size() : le, loweredName, string::npos) > 0) {
        out.declSrc.file = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
        out.declSrc.line = cursorLine + 1;
    }
    return true;
}

// resolveSymbol tier: file-scope names.
bool LspServer::resolveTierGlobal(const string& loweredName, bool isCall, LspSymbolRef& out) {
    typeDef& td = languageService.getType(loweredName);
    if(auto* cd = dynamic_cast<classDef*>(&td)) {
        out.kind = LspSymbolRef::Global;
        out.typeName = cd->name;
        out.displayName = cd->dName();
        out.docComment = cd->docComment;
        out.declSrc = cd->src;
        return true;
    }
    if(auto* ed = dynamic_cast<enumDef*>(&td)) {
        out.kind = LspSymbolRef::Global;
        out.typeName = ed->name;
        out.displayName = ed->dName();
        out.docComment = ed->docComment;
        out.declSrc = ed->src;
        return true;
    }
    typeDef* g = nullptr;
    for(typeDef* cand : languageService.globals) {
        if(cand->name != loweredName) continue;
        if(!g) g = cand;
        if((dynamic_cast<functionDef*>(cand) != nullptr) == isCall) { g = cand; break; }
    }
    if(g) {
        out.kind = LspSymbolRef::Global;
        out.global = g;
        if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
            out.typeName = vd->type.name;
            out.displayName = vd->dName();
            out.docComment = vd->docComment;
            out.declSrc = vd->src;
            out.decl = vd;
        } else if(auto* fd = dynamic_cast<functionDef*>(g)) {
            out.typeName = fd->returnType.name;
            out.displayName = fd->dName();
            out.docComment = fd->docComment;
            out.declSrc = fd->src;
            out.isFunction = true;
        } else if(auto* od = dynamic_cast<objectDef*>(g)) {
            out.typeName = od->objectClass ? od->objectClass->name : "_bglobject";
            out.displayName = od->dName();
            out.docComment = od->docComment;
            out.declSrc = od->src;
        }
        return true;
    }
    return false;
}

bool LspServer::resolveTierUsing(const string& uri, int cursorLine, int cursorCol, const string& loweredName, LspSymbolRef& out) {
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return false;
    for(const string& path : activeUsingPaths(docIt->second, cursorLine)) {
        LspTarget target;
        if(!resolveTarget(uri, cursorLine, cursorCol, path, target)) continue;
        LspMember m = findMemberOn(target, loweredName);
        if(m.member) { fillFromMember(out, m.member, nullptr); return true; }
    }
    return false;
}

bool LspServer::resolveTierEnumValue(const string& loweredName, LspSymbolRef& out) {
    string enumType = languageService.getEnumType(loweredName);
    if(enumType.empty()) return false;
    enumDef* ed = languageService.findEnum(enumType);
    if(!ed) return false;
    for(enumValueDef* ev : ed->namedValues) {
        if(ev->name != loweredName) continue;
        out.kind = LspSymbolRef::EnumValue;
        out.enumType = ed;
        out.typeName = ed->name;
        out.displayName = ev->dName();
        out.docComment = ev->docComment;
        out.declSrc = enumValueSource(ed, loweredName);
        return true;
    }
    return false;
}

// Finds the `#define`/`#declare` of `lower` in `text`, filling out.
static bool scanDefines(const string& text, const string& path, const string& lower, LspSymbolRef& out) {
    istringstream in(text);
    string line;
    for(int n = 1; getline(in, line); n++) {
        size_t i = line.find_first_not_of(" \t");
        if(i == string::npos || line[i] != '#') continue;
        size_t e = ++i;
        while(e < line.size() && isalnum((unsigned char)line[e])) e++;
        string directive = line.substr(i, e - i);
        transform(directive.begin(), directive.end(), directive.begin(), ::tolower);
        if(directive != "define" && directive != "declare" && directive != "redef" && directive != "definei6") continue;
        i = line.find_first_not_of(" \t", e);
        if(i == string::npos) continue;
        e = i;
        while(e < line.size() && isWordChar(line[e])) e++;
        string sym = line.substr(i, e - i), symLower = sym;
        transform(symLower.begin(), symLower.end(), symLower.begin(), ::tolower);
        if(symLower != lower) continue;
        string value = line.substr(e);
        size_t c = value.find("//");
        if(c != string::npos) value.erase(c);
        size_t vs = value.find_first_not_of(" \t"), ve = value.find_last_not_of(" \t\r");
        out.valueText = vs == string::npos ? "" : value.substr(vs, ve - vs + 1);
        out.kind = LspSymbolRef::Define;
        out.displayName = sym;
        out.declSrc.file = path;
        out.declSrc.line = n;
        return true;
    }
    return false;
}

// The symbol is looked for in this document, then in the entry point and the files either includes by name.
bool LspServer::resolveTierDefine(const string& uri, const string& loweredName, LspSymbolRef& out) {
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return false;
    string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
    if(scanDefines(docIt->second, curFile, loweredName, out)) return true;
    vector<pair<string, string>> files;   // path, text
    std::set<string> seenPaths{curFile};
    auto add = [&](const string& path) {
        string text;
        if(seenPaths.insert(path).second && sourceTextOf(path, text)) files.push_back({path, text});
    };
    if(!entryPointPath.empty()) add(entryPointPath);
    vector<pair<string, string>> roots = files;
    roots.push_back({curFile, docIt->second});
    for(const auto& [path, text] : roots) {
        istringstream in(text);
        string line;
        while(getline(in, line)) {
            size_t i = line.find_first_not_of(" \t");
            if(i == string::npos || line.compare(i, 8, "#include") != 0) continue;
            size_t q = line.find('"', i);
            size_t e = q == string::npos ? string::npos : line.find('"', q + 1);
            if(e == string::npos) continue;
            filesystem::path inc = filesystem::path(path).parent_path() / line.substr(q + 1, e - q - 1);
            if(inc.extension().empty()) inc += ".bgl";
            add(inc.lexically_normal().string());
        }
    }
    for(const auto& [path, text] : files)
        if(scanDefines(text, path, loweredName, out)) return true;
    return false;
}

// resolveSymbol tier: `grammar` opening a standalone grammar object stands for the verb class's member.
bool LspServer::resolveTierGrammarKeyword(const string& loweredName, LspSymbolRef& out) {
    if(loweredName != "grammar") return false;
    classDef* verbClass = languageService.findClass("verb");
    typeMember* m = verbClass ? verbClass->findMember([](typeMember* c) { return c->name == "grammar"; }) : nullptr;
    if(!m) return false;
    fillFromMember(out, m, verbClass->declaringClassOf(m));
    return true;
}

LspSymbolRef LspServer::resolveSymbol(const string& uri, int cursorLine, int cursorCol, const string& loweredName, bool isCall) {
    LspSymbolRef out;
    int cursorLine1Based = cursorLine + 1;
    string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);

    // Tiers in order; the first that resolves fills `out` and stops the walk.
    if(resolveTierSourceLineClassMember(curFile, cursorLine1Based, loweredName, out)) return out;
    if(resolveTierSourceLineInstanceMember(curFile, cursorLine1Based, loweredName, out)) return out;
    if(resolveTierFunctionScope(uri, cursorLine, cursorCol, loweredName, out)) return out;
    if(resolveTierTypeBody(uri, cursorLine, cursorCol, loweredName, out)) return out;
    // A variable no source declares (one the compiler made) yields to anything later tiers can name.
    LspSymbolRef synthesized;
    if(resolveTierGlobal(loweredName, isCall, out)) {
        if(out.declSrc.line > 0 || !out.decl) return out;
        synthesized = out;
        out = LspSymbolRef();
    }
    if(resolveTierUsing(uri, cursorLine, cursorCol, loweredName, out)) return out;
    if(resolveTierEnumValue(loweredName, out)) return out;
    if(resolveTierDefine(uri, loweredName, out)) return out;
    if(resolveTierGrammarKeyword(loweredName, out)) return out;
    return synthesized.kind != LspSymbolRef::None ? synthesized : out;
}

//=============================================================================
// Access targets — what `a.b.c` resolves to, shared by hover, definition and completion
//=============================================================================

// What a value of type `typeName` (or the type itself, when !asValue) gives access to. A value of an enum
// type reaches the enum's emitter methods through its companion class; the enum type itself lists values.
LspServer::LspTarget LspServer::targetOfType(const string& typeName, bool asValue) {
    LspTarget t;
    if(typeName.empty()) return t;
    typeDef& td = languageService.getType(typeName.substr(0, typeName.find('<')));   // `array<int>` is the class `array`
    if(auto* cd = dynamic_cast<classDef*>(&td)) t.cls = cd;
    else if(auto* ed = dynamic_cast<enumDef*>(&td)) { if(asValue) t.cls = ed->companion; else t.enumType = ed; }
    else if(auto* od = dynamic_cast<objectDef*>(&td)) { t.obj = od; t.cls = od->objectClass; }
    t.isType = !asValue && t.valid();
    return t;
}

// What a variable gives access to. A namespace hook (`alias x = obj;`, `auto asm = emitterClass;`) names
// its target in the initializer; any other variable is reached through its declared type.
LspServer::LspTarget LspServer::targetOfVariable(variableDeclaration* vd) {
    if(!vd) return LspTarget();
    if(vd->isAlias) return targetOfType(vd->type.name, false);
    string init = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
    transform(init.begin(), init.end(), init.begin(), ::tolower);
    const string& tn = vd->type.name;
    bool untyped = tn.empty() || tn == "auto" || tn == "var" || tn == "_bglobject";
    auto viaInitializer = [&]() {
        LspTarget t;
        if(init.empty()) return t;
        if(auto* od = languageService.findGlobalAs<objectDef>(init)) { t.obj = od; t.cls = od->objectClass; return t; }
        if(auto* cd = languageService.findClass(init)) { t.cls = cd; t.isType = true; }
        return t;
    };
    if(vd->isNamespaceAlias() || untyped) {
        LspTarget t = viaInitializer();
        if(t.valid()) return t;
    }
    LspTarget t = targetOfType(tn, true);
    if(!t.valid()) t = viaInitializer();
    return t;
}

bool LspServer::resolveTargetHead(const string& uri, int line, int col, const string& nameLower, LspTarget& out) {
    out = LspTarget();
    bool shadowed = false;   // a local or parameter may be named `outer`
    if(nameLower == "outer") {
        LspSymbolRef::Kind k = resolveSymbol(uri, line, col, nameLower).kind;
        shadowed = k == LspSymbolRef::Local || k == LspSymbolRef::Parameter;
    }
    if(!shadowed && (nameLower == "self" || nameLower == "outer")) {
        FunctionContext ctx = functionContext(uri, line, col);
        if(nameLower == "self") {
            if(ctx.ownerObject) { out.obj = ctx.ownerObject; out.cls = ctx.ownerObject->objectClass; }
            else out.cls = ctx.ownerClass;
        } else if(objectDef* host = accessorHost(ctx.ownerClass)) {   // `outer` in an inline accessor is its host object
            out.obj = host;
            out.cls = host->objectClass;
        }
        return out.valid();
    }
    LspSymbolRef ref = resolveSymbol(uri, line, col, nameLower);
    switch(ref.kind) {
        case LspSymbolRef::Local:
        case LspSymbolRef::Parameter:
        case LspSymbolRef::ClassMember:
        case LspSymbolRef::Alias:
            if(ref.isFunction) return false;
            out = ref.decl ? targetOfVariable(ref.decl) : targetOfType(ref.typeName, true);
            return out.valid();
        case LspSymbolRef::EnumValue:
            out.cls = ref.enumType ? ref.enumType->companion : nullptr;
            return out.valid();
        case LspSymbolRef::Global: {
            typeDef& td = languageService.getType(nameLower);
            if(dynamic_cast<classDef*>(&td) || dynamic_cast<enumDef*>(&td)) { out = targetOfType(nameLower, false); return out.valid(); }
            if(auto* od = languageService.findGlobalAs<objectDef>(nameLower)) { out.obj = od; out.cls = od->objectClass; return true; }
            if(ref.decl) { out = targetOfVariable(ref.decl); return out.valid(); }
            return false;
        }
        default: break;
    }
    for(verbObjectDef* v : languageService.verbs)
        if(v->name == nameLower) { out.obj = v; out.cls = v->objectClass; return out.valid(); }
    return false;
}

bool LspServer::resolveTarget(const string& uri, int line, int col, const string& pathLower, LspTarget& out) {
    size_t dot = pathLower.find('.');
    if(!resolveTargetHead(uri, line, col, pathLower.substr(0, dot), out)) return false;
    while(dot != string::npos) {
        size_t next = pathLower.find('.', dot + 1);
        string seg = pathLower.substr(dot + 1, next == string::npos ? string::npos : next - dot - 1);
        dot = next;
        LspMember m = findMemberOn(out, seg);
        LspTarget step;
        if(auto* vd = dynamic_cast<variableDeclaration*>(m.member)) step = targetOfVariable(vd);
        else if(m.enumValue && m.enumType) step.cls = m.enumType->companion;
        if(!step.valid()) return false;
        out = step;
    }
    return true;
}

LspServer::LspMember LspServer::findMemberOn(const LspTarget& t, const string& nameLower) {
    LspMember r;
    if(t.enumType)
        for(enumValueDef* ev : t.enumType->namedValues)
            if(ev->name == nameLower) { r.enumType = t.enumType; r.enumValue = ev; return r; }
    if(t.obj)
        for(typeMember* m : t.obj->members)
            if(isNamedValueOrRoutine(m, nameLower) && !m->isPrePassStub) { r.member = m; r.object = t.obj; return r; }
    if(t.cls) {
        typeMember* hit = t.cls->findMember([&](typeMember* m) { return isNamedValueOrRoutine(m, nameLower) && !m->isPrePassStub; });
        if(!hit) hit = t.cls->findMember([&](typeMember* m) { return isNamedValueOrRoutine(m, nameLower); });
        if(hit) { r.member = hit; r.owner = t.cls->declaringClassOf(hit); return r; }
        // A value that converts (`operator ()`) to another type also offers that type's members
        LspTarget converted = conversionTarget(t.cls);
        if(converted.cls && converted.cls != t.cls) return findMemberOn(converted, nameLower);
    }
    return r;
}

// What a value of class `c` reads as: the type its `operator ()` returns.
LspServer::LspTarget LspServer::conversionTarget(classDef* c) {
    LspTarget t;
    typeMember* conv = c->findMember([](typeMember* m) { auto* fd = dynamic_cast<functionDef*>(m); return fd && fd->name == "operator()" && fd->params.empty(); });
    if(auto* fd = dynamic_cast<functionDef*>(conv)) t = targetOfType(fd->returnType.name, true);
    return t;
}

//=============================================================================
// Hover
//=============================================================================

// Hover case: a verb-extend member keyword. Sets `handled` and returns the hover when the
// cursor owns it; otherwise leaves `handled` false and its return value is ignored.
    // Verb-extend member keywords — hover doc for `synonyms` / `priority` / `grammar`, gated on being
    // inside a `verb <name> { … }` or `extend <verb> { … }` body (so a user identifier of the same
    // name elsewhere never false-hovers). Mirrors the extend-body completion docs.
json LspServer::hoverVerbExtendKeyword(const string& word, const string& docText, int line, int col,
                                       bool& handled) {
    handled = false;
    if(word == "synonyms" || word == "priority" || word == "grammar") {
        const string& dt = docText;
        size_t cursorOffset = 0;
        { int cl = 0; size_t i = 0; while(i < dt.size() && cl < line) { if(dt[i] == '\n') cl++; i++; } cursorOffset = i + (size_t)col; if(cursorOffset > dt.size()) cursorOffset = dt.size(); }
        ptrdiff_t openerPos = -1; int depth = 0;
        for(ptrdiff_t i = (ptrdiff_t)cursorOffset - 1; i >= 0; i--) { char c = dt[(size_t)i]; if(c == '}') depth++; else if(c == '{') { depth--; if(depth < 0) { openerPos = i; break; } } }
        bool inVerbBody = false;
        if(openerPos >= 0) {
            ptrdiff_t i = openerPos - 1;
            while(i >= 0 && isspace((unsigned char)dt[(size_t)i])) i--;
            ptrdiff_t ne = i + 1; while(i >= 0 && (isalnum((unsigned char)dt[(size_t)i]) || dt[(size_t)i] == '_')) i--; ptrdiff_t ns = i + 1;
            string nm = (ns < ne) ? dt.substr((size_t)ns, (size_t)(ne - ns)) : "";
            while(i >= 0 && isspace((unsigned char)dt[(size_t)i])) i--;
            ptrdiff_t ke = i + 1; while(i >= 0 && (isalnum((unsigned char)dt[(size_t)i]) || dt[(size_t)i] == '_')) i--;
            string kw = (i + 1 < ke) ? dt.substr((size_t)(i + 1), (size_t)(ke - (i + 1))) : "";
            transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
            transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
            if((kw == "extend" || kw == "verb") && !nm.empty())
                for(auto* v : languageService.verbs) if(v->name == nm) { inVerbBody = true; break; }
        }
        if(inVerbBody) {
            string hd;
            if(word == "synonyms")      hd = "**`synonyms`** — declare true I6 aliases of this verb: `synonyms = {.w1, .w2};` emits `Verb 'w1' 'w2' = '<anchor>';`, so any later `extend` of the verb flows to them too (unlike copying a grammar line, which is a one-time snapshot).";
            else if(word == "priority") hd = "**`priority`** — grammar match order. On the verb block it sets the anchor (default `10`); inside an `extend` it is a block-local directive stamped onto the lines that block adds. Lower number = matched earlier.";
            else                        hd = "**`grammar`** — this verb's grammar lines. In an `extend`: `grammar += { … }` appends, `grammar -= { … }` removes matching lines (or evicts an extern verb's word), `replace grammar = { … }` wipes and replaces.";
            handled = true;
            return {{"contents", {{"kind", "markdown"}, {"value", hd}}}};
        }
    }
    return json();
}

// Hover case: a #beguilerSettings property or enum value. Returns the signature line it
// resolved, or empty when this case does not own the cursor (the convention for all three).
    // ── #beguilerSettings block: resolve bare identifiers as beguilerSettingsType members ──
    // The block isn't a real AST node, so the regular resolver won't find these. Handle it here
    // so hover on property names (LHS of assignment inside the block) shows the declared type.
string LspServer::hoverBeguilerSettingsMember(const string& uri, int line, int col,
                                              const string& lower, const string& ownerName) {
    string typeInfo;
    if(ownerName.empty() && isInBeguilerSettingsBlock(uri, line, col)) {
        classDef* schema = languageService.findClass("beguilersettingstype");
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
                    if(auto* ed = languageService.findEnum(enumTypeName)) {
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
    return typeInfo;
}

// A parameter as shown in hover and signature help: `type name`, plus ` = default` when it has one,
// so an optional parameter reads as optional.
static string paramDisplay(const paramDef* p){
    string out = typeDisplay(p->type.name) + " " + (p->displayName.empty() ? p->name : p->displayName);
    if(!p->defaultValue.empty()) out += " = " + (p->defaultSource.empty() ? p->defaultValue : p->defaultSource);
    return out;
}

// Hover case: `owner.member` — resolve the owner chain, then the member on it.
string LspServer::hoverDottedMember(const string& uri, int line, int col, const string& lower,
                                    const string& ownerName, string& docComment) {
    if(ownerName.empty()) return "";
    string ownerLower = ownerName;
    transform(ownerLower.begin(), ownerLower.end(), ownerLower.begin(), ::tolower);
    LspTarget target;
    if(!resolveTarget(uri, line, col, ownerLower, target)) return "";
    LspMember m = findMemberOn(target, lower);
    if(m.enumValue) {
        docComment = m.enumValue->docComment;
        return typeDisplay(m.enumType->name) + "." + m.enumValue->dName();
    }
    if(auto* fd = dynamic_cast<functionDef*>(m.member)) {
        string sig = typeDisplay(fd->returnType.name) + " " + fd->dName();
        if(!fd->isValueEmitter) {
            sig += "(";
            for(size_t i = 0; i < fd->params.size(); i++) {
                if(i > 0) sig += ", ";
                sig += paramDisplay(fd->params[i]);
            }
            sig += ")";
        }
        if(fd->isEmitter) sig = "emitter " + sig;
        docComment = fd->docComment;
        return sig;
    }
    if(auto* vd = dynamic_cast<variableDeclaration*>(m.member)) {
        docComment = vd->docComment;
        if(vd->isAlias) return "type alias → " + typeDisplay(vd->type.name);
        return typeDisplay(vd->type.name) + " " + vd->dName();
    }
    return "";
}

// Hover case: a bare identifier — locals, params, class members, globals, verbs, enum values.
string LspServer::hoverIdentifier(const string& uri, int line, int col, bool isCall, const string& lower,
                                  const string& ownerName, string& docComment) {
    string typeInfo;
    if(!ownerName.empty()) return typeInfo;
    LspSymbolRef ref = resolveSymbol(uri, line, col, lower, isCall);
    if(!ref.docComment.empty()) docComment = ref.docComment;
    if(ref.kind == LspSymbolRef::Local) {
        typeInfo = "local: " + typeDisplay(ref.typeName) + " " + ref.displayName;
    } else if(ref.kind == LspSymbolRef::Parameter) {
        typeInfo = "param: " + typeDisplay(ref.typeName) + " " + ref.displayName;
    } else if(ref.kind == LspSymbolRef::Alias) {
        typeInfo = "type alias → " + typeDisplay(ref.typeName);
    } else if(ref.kind == LspSymbolRef::ClassMember) {
        string clsName = ref.enclosingClass ? ref.enclosingClass->dName() : "";
        typeInfo = (clsName.empty() ? "member: " : clsName + "::") + typeDisplay(ref.typeName) + " " + ref.displayName;
    } else if(ref.kind == LspSymbolRef::TypeParameter) {
        typeInfo = "type parameter " + ref.displayName;
    } else if(ref.kind == LspSymbolRef::Define) {
        typeInfo = "#define " + ref.displayName + (ref.valueText.empty() ? "" : " " + ref.valueText);
    } else if(ref.kind == LspSymbolRef::EnumValue) {
        typeInfo = typeDisplay(ref.typeName) + "." + ref.displayName;
    } else if(ref.kind == LspSymbolRef::Global) {
        // resolveSymbol set enough info; enrich with type/class specifics
        typeDef& td = languageService.getType(lower);
        if(auto* cd = dynamic_cast<classDef*>(&td)) {
            typeInfo = (cd->isValue && !cd->isEmitterClass ? "struct " : "class ") + cd->dName();
            if(cd->isEmitterClass) typeInfo = "emitter " + typeInfo;
            if(cd->isExternal) typeInfo = "extern " + typeInfo;
        } else if(auto* ed = dynamic_cast<enumDef*>(&td)) {
            typeInfo = (ed->isBnum ? "bnum " : "enum ") + ed->dName();
            if(ed->isExternal) typeInfo = "extern " + typeInfo;
        } else if(typeDef* g = ref.global) {
            if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
                typeInfo = typeDisplay(vd->type.name) + " " + vd->dName();
            } else if(auto* fd = dynamic_cast<functionDef*>(g)) {
                typeInfo = typeDisplay(fd->returnType.name) + " " + fd->dName() + "(";
                for(size_t i = 0; i < fd->params.size(); i++) {
                    if(i > 0) typeInfo += ", ";
                    typeInfo += paramDisplay(fd->params[i]);
                }
                typeInfo += ")";
                if(fd->isEmitter) typeInfo = "emitter " + typeInfo;
            } else if(auto* od = dynamic_cast<objectDef*>(g)) {
                typeInfo = typeDisplay(od->objectClass ? od->objectClass->name : "_bglobject") + " " + od->dName();
            }
        }
    }
    if(typeInfo.empty()) {
        for(auto* v : languageService.verbs)
            if(v->name == lower) {
                typeInfo = "verb " + v->dName();
                docComment = v->docComment;
                break;
            }
    }
    return typeInfo;
}

// True when the word starting at `start` follows the `operator` keyword (`operator prefix++ ()`).
static bool followsOperatorKeyword(const string& lineText, int start) {
    int e = start;
    while(e > 0 && isspace((unsigned char)lineText[e - 1])) e--;
    int b = e;
    while(b > 0 && isWordChar(lineText[b - 1])) b--;
    string w = lineText.substr(b, e - b);
    transform(w.begin(), w.end(), w.begin(), ::tolower);
    return w == "operator";
}

json LspServer::handleHover(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();
    ensureParsedForRequest(uri);

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

    bool handled = false;
    if(json r = hoverVerbExtendKeyword(word, docIt->second, line, col, handled); handled) return r;

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

    size_t after = lineText.find_first_not_of(" \t", end);
    bool isCall = after != string::npos && lineText[after] == '(';

    string typeInfo;
    // Doc-comment for the resolved symbol (if any). Populated alongside typeInfo at each
    // resolution branch. Rendered as Markdown below the signature in the hover response.
    string docComment;

    // Hover cases, tried in order; each returns the signature line it resolved, or empty.
    if((lower == "prefix" || lower == "postfix") && followsOperatorKeyword(lineText, start)) {
        typeInfo = "operator " + lower;
        docComment = lower == "prefix" ? "Declares the prefix form of `++` or `--`: `operator prefix++ ()`."
                                       : "Declares the postfix form of `++` or `--`: `operator postfix++ ()`.";
    }
    if(typeInfo.empty()) typeInfo = hoverBeguilerSettingsMember(uri, line, col, lower, ownerName);
    if(typeInfo.empty()) typeInfo = hoverDottedMember(uri, line, col, lower, ownerName, docComment);
    if(typeInfo.empty()) typeInfo = hoverIdentifier(uri, line, col, isCall, lower, ownerName, docComment);

    if(typeInfo.empty()) return nullptr;

    string markdown = "```bgl\n" + typeInfo + "\n```";
    if(!docComment.empty())
        markdown += "\n\n---\n\n" + docComment;

    return {
        {"contents", {
            {"kind", "markdown"},
            {"value", markdown}
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

// Resolve every active `#using <path>` above `line` to the namespace object it names.
// Mirrors Phase 4's path-collection + object walk, factored out so the enum-argument
// completion phase (which runs before Phase 4) can reuse it.
// Walk a dotted namespace path (e.g. "bgl.printrules") to its objectDef, following alias/typed
// member links. `path` is lowercased. nullptr when any segment fails to resolve to an object.
objectDef* LspServer::walkNamespacePath(const string& path) {
    size_t dot = path.find('.');
    string head = (dot == string::npos) ? path : path.substr(0, dot);
    string rest = (dot == string::npos) ? "" : path.substr(dot + 1);
    objectDef* curObj = languageService.findGlobalAs<objectDef>(head);
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
                if(auto* od = languageService.findGlobalAs<objectDef>(initName)) next = od;
            if(!next)
                if(auto* od = languageService.findGlobalAs<objectDef>(vd->type.name)) next = od;
            break;
        }
        curObj = next;
    }
    return curObj;
}

// The lowercased paths of the `#using` directives at or above `line`.
static vector<string> activeUsingPaths(const string& docText, int line) {
    vector<string> usingPaths;
    istringstream ds(docText);
    string dl;
    int lineIdx = 0;
    while(getline(ds, dl)){
        if(lineIdx > line) break;
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
    return usingPaths;
}

vector<objectDef*> LspServer::activeUsingNamespaces(const string& docText, int line) {
    vector<objectDef*> out;
    for(const string& path : activeUsingPaths(docText, line))
        if(objectDef* ns = walkNamespacePath(path)) out.push_back(ns);
    return out;
}

// Resolve a call site to every matching callee (all overloads). See header.
vector<functionDef*> LspServer::resolveCallees(const string& uri, int line, int col,
                                                 const string& funcName, const string& objName,
                                                 const string& docText) {
    string lower = funcName;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    vector<functionDef*> candidates;

    auto addFrom = [&](typeMember* m) {
        if(auto* fd = dynamic_cast<functionDef*>(m))
            if(fd->name == lower && !fd->isPrePassStub
               && std::find(candidates.begin(), candidates.end(), fd) == candidates.end())
                candidates.push_back(fd);
    };

    if(!objName.empty()) {
        // Member call `recv.method(` — the receiver's own members and its class's.
        string objLower = objName;
        transform(objLower.begin(), objLower.end(), objLower.begin(), ::tolower);
        LspTarget target;
        if(resolveTarget(uri, line, col, objLower, target)) {
            if(target.obj) for(typeMember* m : target.obj->members) addFrom(m);
            if(target.cls) target.cls->forEachMember(addFrom);
        }
    } else {
        // Bare call: global functions first, then #using-imported namespace members.
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->name == lower && !fd->isPrePassStub) candidates.push_back(fd);
        if(candidates.empty())
            for(objectDef* ns : activeUsingNamespaces(docText, line))
                for(typeMember* m : ns->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == lower && !fd->isPrePassStub) candidates.push_back(fd);
    }

    return candidates;
}

vector<paramDef*> LspServer::resolveCalleeParams(const string& uri, int line, int col,
                                                 const string& funcName, const string& objName,
                                                 const string& docText) {
    vector<functionDef*> c = resolveCallees(uri, line, col, funcName, objName, docText);
    return c.empty() ? vector<paramDef*>{} : c[0]->params;
}

    // Build an include completion item. The edit range spans the WHOLE partial path (from just
    // after the opening delimiter to the cursor) so accepting replaces everything typed in the
    // delimiter — correct across '/', spaces, and '#'. `delimCol` is the 0-based column of the
    // opening '<' or '"'.
    //
    // filterText = `query + relStr`: we already substring-filtered server-side, so we want the
    // client to SHOW exactly what we return, but VS Code re-applies its own subsequence fuzzy
    // filter on top — which rejects matches that don't sit well against filterText (notably a
    // leading '#', e.g. `par` vs `#parser`). Prefixing the typed query makes the query a literal
    // prefix of filterText, which VS Code's matcher always accepts, so nothing we return is
    // dropped. `newText`/`label` stay the real path.
static json makeIncludeItem(const string& relStr, const char* detail, int delimCol,
                            const string& query, int line, int col) {
    return {
        {"label", relStr},
        {"kind", 17},               // CompletionItemKind.File
        {"detail", detail},
        {"filterText", query + relStr},
        {"textEdit", {
            {"range", {
                {"start", {{"line", line}, {"character", delimCol + 1}}},
                {"end",   {{"line", line}, {"character", col}}}
            }},
            {"newText", relStr}
        }}
    };
}

    // Case-insensitive SUBSTRING test. The include phases filter candidates server-side by
    // substring against the full relative path (and return isIncomplete=true so the client
    // re-queries each keystroke). This deliberately overrides VS Code's default subsequence
    // fuzzy match, which is too loose for paths — e.g. `par` fuzzy-matches `pkgOrEnhancements`
    // (p…a…r) but is NOT a substring, so it's correctly excluded here. A substring is always a
    // subsequence, so the client's own filter keeps everything we return.
static bool ciContains(const string& hay, const string& needle) {
    if(needle.empty()) return true;
    string h = hay, n = needle;
    transform(h.begin(), h.end(), h.begin(), ::tolower);
    transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != string::npos;
}

    // Extract the partial path already typed between the opening delimiter and the cursor.
static string includeQuery(const string& lineText, int col, int delimCol) {
    int qs = delimCol + 1;
    if(qs < 0) qs = 0;
    if(qs >= (int)lineText.size() || qs >= col) return "";
    int end = std::min(col, (int)lineText.size());
    return lineText.substr(qs, end - qs);
}

// Completion case: `#include "..."` project-file paths.
json LspServer::completeQuotedInclude(const string& uri, int line, int col, const string& lineText, bool& handled) {
    handled = false;
    // ── Phase 0b: `#include "…"` project-file completion ──────────────────
    // The quoted include form searches the current file's directory first, then
    // each configured includePath (mirrors resolveIncludePath in bglParser.cpp).
    // Offer the .bgl files reachable from those roots, named as their path
    // relative to the matching root (forward slashes, no .bgl). Unlike the `<…>`
    // library form (Phase 0), '_'-prefixed files are NOT filtered — project files
    // legitimately use them. Must run BEFORE the string-literal suppression below,
    // since an in-progress `#include "…"` looks like an unterminated string.
    {
        string pre = lineText.substr(0, std::min((int)lineText.size(), col));
        size_t lt = pre.find_first_not_of(" \t");
        string trimmed = (lt == string::npos) ? string() : pre.substr(lt);
        size_t q = trimmed.find('"');
        // Inside the quotes iff there's an opening quote and no closing one before the cursor.
        if(q != string::npos && trimmed.find('"', q + 1) == string::npos) {
            // Head before the quote must be `#include` (optionally `#include ?`), ignoring
            // whitespace and case.
            string headCompact;
            for(size_t k = 0; k < q; k++) {
                char c = trimmed[k];
                if(!isspace((unsigned char)c)) headCompact += (char)tolower((unsigned char)c);
            }
            if(headCompact == "#include" || headCompact == "#include?") {
                json items = json::array();
                std::set<string> seen;
                int delimCol = (int)(lt + q);
                string query = includeQuery(lineText, col, delimCol);

                string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
                filesystem::path srcDir = filesystem::path(curFile).parent_path();
                string curCanon;
                try { curCanon = filesystem::canonical(curFile).string(); } catch(...) {}

                // Search roots in resolution order: source dir, then each includePath
                // (as-is, and — if relative — also resolved against the source dir).
                vector<filesystem::path> roots;
                if(!srcDir.empty()) roots.push_back(srcDir);
                for(const string& sp : beguilerSettings.includePaths) {
                    if(sp.empty()) continue;
                    filesystem::path p(sp);
                    roots.push_back(p);
                    if(p.is_relative() && !srcDir.empty()) roots.push_back(srcDir / p);
                }

                for(const filesystem::path& root : roots) {
                    std::error_code ec;
                    if(!filesystem::exists(root, ec) || !filesystem::is_directory(root, ec)) continue;
                    filesystem::recursive_directory_iterator it(root, ec), end;
                    for(; !ec && it != end; it.increment(ec)) {
                        if(!it->is_regular_file(ec)) continue;
                        filesystem::path p = it->path();
                        string filename = p.filename().string();
                        if(filename.empty() || filename[0] == '.') continue;   // hidden
                        if(filename.size() < 4) continue;
                        string ext = filename.substr(filename.size() - 4);
                        for(char& c : ext) c = (char)tolower((unsigned char)c);
                        if(ext != ".bgl") continue;
                        // A file can't include itself.
                        try { if(!curCanon.empty() && filesystem::canonical(p).string() == curCanon) continue; } catch(...) {}
                        // Name = path relative to this root, forward slashes, no ".bgl".
                        filesystem::path rel = filesystem::relative(p, root, ec);
                        if(ec || rel.empty()) continue;
                        string relStr = rel.string();
                        for(char& c : relStr) if(c == '\\') c = '/';
                        relStr = relStr.substr(0, relStr.size() - 4);  // strip ".bgl"
                        if(relStr.empty() || relStr[0] == '/') continue;
                        if(!ciContains(relStr, query)) continue;     // server-side substring filter
                        if(!seen.insert(relStr).second) continue;   // dedup; earlier root wins
                        items.push_back(makeIncludeItem(relStr, "Beguile include (project file)", delimCol, query, line, col));
                    }
                }
                // isIncomplete: re-query each keystroke so the substring filter re-runs.
                handled = true; return json{{"isIncomplete", true}, {"items", items}};
            }
        }
    }
    return json();
}

// Completion case: `#includeI6 "..."` Inform 6 library files.
json LspServer::completeI6Include(const string& uri, int line, int col, const string& lineText, bool& handled) {
    handled = false;
    // ── Phase 0c: `#includeI6 "…"` Inform 6 library completion ─────────────
    // `#includeI6 "name"` resolves an I6 file (exact name, then name.h) from the source
    // dir + includePaths. Offer the .h files reachable from those roots, named without
    // the .h extension (the idiomatic form, e.g. `#includeI6 "parser"` → parser.h). Same
    // roots as Phase 0b; tolerates the optional `?` and raw `@` markers; runs before the
    // string-literal suppression. Distinct from `#include` (Phase 0b, .bgl only) — the I6
    // library folder holds .h files, which are correctly absent from #include completion.
    {
        string pre = lineText.substr(0, std::min((int)lineText.size(), col));
        size_t lt = pre.find_first_not_of(" \t");
        string trimmed = (lt == string::npos) ? string() : pre.substr(lt);
        size_t q = trimmed.find('"');
        if(q != string::npos && trimmed.find('"', q + 1) == string::npos) {
            string headCompact;
            for(size_t k = 0; k < q; k++) {
                char c = trimmed[k];
                if(!isspace((unsigned char)c)) headCompact += (char)tolower((unsigned char)c);
            }
            // Tolerate `#includeI6 ?"…"` (optional) and `#includeI6 @"…"` (raw) markers.
            while(!headCompact.empty() && (headCompact.back() == '?' || headCompact.back() == '@'))
                headCompact.pop_back();
            if(headCompact == "#includei6") {
                json items = json::array();
                std::set<string> seen;
                int delimCol = (int)(lt + q);
                string query = includeQuery(lineText, col, delimCol);
                string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
                filesystem::path srcDir = filesystem::path(curFile).parent_path();
                vector<filesystem::path> roots;
                if(!srcDir.empty()) roots.push_back(srcDir);
                for(const string& sp : beguilerSettings.includePaths) {
                    if(sp.empty()) continue;
                    filesystem::path p(sp);
                    roots.push_back(p);
                    if(p.is_relative() && !srcDir.empty()) roots.push_back(srcDir / p);
                }
                for(const filesystem::path& root : roots) {
                    std::error_code ec;
                    if(!filesystem::exists(root, ec) || !filesystem::is_directory(root, ec)) continue;
                    filesystem::recursive_directory_iterator it(root, ec), end;
                    for(; !ec && it != end; it.increment(ec)) {
                        if(!it->is_regular_file(ec)) continue;
                        filesystem::path p = it->path();
                        string filename = p.filename().string();
                        if(filename.empty() || filename[0] == '.') continue;
                        if(filename.size() < 3) continue;
                        string ext = filename.substr(filename.size() - 2);
                        for(char& c : ext) c = (char)tolower((unsigned char)c);
                        if(ext != ".h") continue;
                        filesystem::path rel = filesystem::relative(p, root, ec);
                        if(ec || rel.empty()) continue;
                        string relStr = rel.string();
                        for(char& c : relStr) if(c == '\\') c = '/';
                        relStr = relStr.substr(0, relStr.size() - 2);  // strip ".h"
                        if(relStr.empty() || relStr[0] == '/') continue;
                        if(!ciContains(relStr, query)) continue;     // server-side substring filter
                        if(!seen.insert(relStr).second) continue;
                        items.push_back(makeIncludeItem(relStr, "Inform 6 file", delimCol, query, line, col));
                    }
                }
                // isIncomplete: re-query each keystroke so the substring filter re-runs.
                handled = true; return json{{"isIncomplete", true}, {"items", items}};
            }
        }
    }
    return json();
}

// Completion case: `includePaths = "..."` directory segments inside #beguilerSettings.
json LspServer::completeIncludePathsDir(const string& uri, int line, int col, const string& lineText,
                                        bool insideBsBlock, bool& handled) {
    handled = false;
    // ── Phase 0d: `includePaths = "…"` directory completion (in #beguilerSettings) ──
    // Inside a #beguilerSettings block the includePaths value is a filesystem path. Offer only
    // DIRECTORIES (kind=Folder), resolved relative to the source file's directory (or absolute).
    // Completion is per path segment: text up to the last '/' selects the base dir to list; text
    // after it filters the entries (substring). So `../` lists the parent's dirs and `../s` narrows
    // to `src`, `super`, … Accepting inserts `<dir>/` and re-triggers so the next level lists
    // immediately. Runs before the string-literal suppression (the value is quoted).
    if(insideBsBlock) {
        string pre = lineText.substr(0, std::min((int)lineText.size(), col));
        size_t lt = pre.find_first_not_of(" \t");
        string trimmed = (lt == string::npos) ? string() : pre.substr(lt);
        size_t q = trimmed.find('"');
        if(q != string::npos && trimmed.find('"', q + 1) == string::npos) {
            string headCompact;
            for(size_t k = 0; k < q; k++) {
                char c = trimmed[k];
                if(!isspace((unsigned char)c)) headCompact += (char)tolower((unsigned char)c);
            }
            if(headCompact == "includepaths=") {
                int delimCol  = (int)(lt + q);
                int pathStart = delimCol + 1;
                string typedPath = includeQuery(lineText, col, delimCol);   // text from delim+1 to cursor
                size_t slash = typedPath.rfind('/');
                string dirPrefix = (slash == string::npos) ? string() : typedPath.substr(0, slash + 1);
                string segment   = (slash == string::npos) ? typedPath : typedPath.substr(slash + 1);
                int segStartCol  = pathStart + (int)((slash == string::npos) ? 0 : slash + 1);

                // Resolve the base directory to list (relative to the source file's dir).
                string curFile = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
                filesystem::path srcDir = filesystem::path(curFile).parent_path();
                filesystem::path base;
                if(dirPrefix.empty())                              base = srcDir;
                else if(filesystem::path(dirPrefix).is_absolute()) base = filesystem::path(dirPrefix);
                else                                               base = srcDir / dirPrefix;
                base = base.lexically_normal();

                json items = json::array();
                std::error_code ec;
                if(filesystem::exists(base, ec) && filesystem::is_directory(base, ec)) {
                    for(filesystem::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
                        if(!it->is_directory(ec)) continue;
                        string name = it->path().filename().string();
                        if(name.empty() || name[0] == '.') continue;   // skip hidden dirs
                        if(!ciContains(name, segment)) continue;       // substring filter on the segment
                        items.push_back({
                            {"label", name + "/"},
                            {"kind", 19},                              // CompletionItemKind.Folder
                            {"detail", "directory"},
                            {"filterText", segment + name},            // prefix-guarantee (see makeIncludeItem)
                            {"textEdit", {
                                {"range", {
                                    {"start", {{"line", line}, {"character", segStartCol}}},
                                    {"end",   {{"line", line}, {"character", col}}}
                                }},
                                {"newText", name + "/"}
                            }},
                            // Re-trigger so selecting a directory immediately lists its subdirectories.
                            {"command", {{"title", "Suggest"}, {"command", "editor.action.triggerSuggest"}}}
                        });
                    }
                }
                // isIncomplete: re-query each keystroke so the substring filter re-runs.
                handled = true; return json{{"isIncomplete", true}, {"items", items}};
            }
        }
    }
    return json();
}

// Locate the call argument the cursor sits in: scan back from `col` for the enclosing unmatched `(`,
// counting top-level commas. Yields the callee name, its receiver path (`a.b.fn(` → "a.b") if any, the
// 0-based argument index, and slotStart — the column just past the `(` or `,` that opens the slot.
static bool findCallSlot(const string& lineText, int col, string& funcName, string& objName,
                         int& argIndex, int& slotStart) {
    int scanLimit = col; if(scanLimit > (int)lineText.size()) scanLimit = (int)lineText.size();
    int parenDepth = 0, commaCount = 0, funcEnd = -1, nearestComma = -1;
    for(int i = scanLimit - 1; i >= 0; i--) {
        char c = lineText[i];
        if(c == ')') parenDepth++;
        else if(c == '(') { if(parenDepth == 0) { funcEnd = i; break; } parenDepth--; }
        else if(c == ',' && parenDepth == 0) { if(nearestComma < 0) nearestComma = i; commaCount++; }
    }
    if(funcEnd < 0) return false;
    int nameEnd = funcEnd, nameStart = nameEnd - 1;
    while(nameStart >= 0 && (isalnum((unsigned char)lineText[nameStart]) || lineText[nameStart] == '_')) nameStart--;
    nameStart++;
    funcName = lineText.substr(nameStart, nameEnd - nameStart);
    if(funcName.empty()) return false;
    objName.clear();
    if(nameStart > 0 && lineText[nameStart - 1] == '.') {
        int objEnd = nameStart - 1, objStart = objEnd - 1;
        while(objStart >= 0 && (isalnum((unsigned char)lineText[objStart]) || lineText[objStart] == '_'
                                || lineText[objStart] == '.')) objStart--;
        objStart++;
        objName = lineText.substr(objStart, objEnd - objStart);
    }
    argIndex = commaCount;
    slotStart = (nearestComma >= 0 ? nearestComma : funcEnd) + 1;
    return true;
}

// Completion case: a call argument whose parameter type is an enum -> that enum's members.
json LspServer::completeEnumArgument(const string& uri, int line, int col, const string& lineText,
                                     const string& docText, bool& handled) {
    handled = false;
    // ── Enum-argument completion ─────────────────────────────────────────
    // At a call argument whose parameter type is an enum, offer that enum's members.
    // Fires in ordinary code — `foo(bar, ▮)` — AND inside an interpolated string —
    // `$"…{img(▮)}…"` — so it MUST run before the in-string suppression below (that
    // guard would otherwise treat the interpolation `{…}` as prose and bail). Detection is
    // specific enough to be safe in prose: it only produces items when the enclosing `(`
    // has a resolvable callee whose active parameter is an enum, so an incidental `(` in
    // narrative text yields nothing and falls through to the normal phases.
    {
        string funcName, objName;
        int argIndex = 0, slotStart = 0;
        if(findCallSlot(lineText, col, funcName, objName, argIndex, slotStart)
           && lineText.substr(slotStart, std::max(0, std::min(col, (int)lineText.size()) - slotStart)).find('.') == string::npos) {   // `f(x.|` completes x's members
            vector<paramDef*> params = resolveCalleeParams(uri, line, col, funcName, objName, docText);
            if(argIndex < (int)params.size()) {
                string ptype = params[argIndex]->type.name;  // lowercased type name
                for(typeDef* t : languageService.objectTypes) {
                    auto* ed = dynamic_cast<enumDef*>(t);
                    if(!ed || ed->name != ptype) continue;
                    json items = json::array();
                    for(enumValueDef* ev : ed->namedValues) {
                        json item = {{"label", ev->name}, {"kind", 13},  // CompletionItemKind.EnumMember
                                     {"detail", ed->displayName.empty() ? ed->name : ed->displayName}};
                        if(!ev->docComment.empty())
                            item["documentation"] = {{"kind", "markdown"}, {"value", ev->docComment}};
                        items.push_back(item);
                    }
                    if(!items.empty()) { handled = true; return items; }
                    break;
                }
            }
        }
    }
    return json();
}

// Append the symbols in scope at `line` — locals declared above it, parameters, members of the enclosing
// class or object, file-scope globals/functions/objects, #using-imported members — that `fits` accepts.
// `typed` completes a typed slot, so values with no type (void, var) are left out as noise.
void LspServer::appendScopeItems(json& items, std::set<string>& seen, const string& uri, int line, int col,
                                 const string& docText, const std::function<bool(const string&)>& fits, bool typed) {
    auto accept = [&](const string& name, const string& type) {
        if(name.empty() || name[0] == '_' || !isalpha((unsigned char)name[0])) return false;
        if(typed && (type.empty() || type == "void" || type == "var")) return false;   // untyped values fit anywhere: noise
        if(seen.count(name)) return false;
        if(!fits(type)) return false;
        seen.insert(name);
        return true;
    };
    auto addVar = [&](variableDeclaration* vd, int kind) {
        if(!vd || vd->isSynthetic || vd->isAlias || vd->isPrePassStub) return;
        if(!accept(vd->name, vd->type.name)) return;
        json item = {{"label", vd->dName()}, {"kind", vd->isConst ? 21 : kind}, {"detail", vd->type.dName()}};
        if(!vd->docComment.empty()) item["documentation"] = {{"kind", "markdown"}, {"value", vd->docComment}};
        items.push_back(item);
    };
    auto addFn = [&](functionDef* fd) {
        if(!fd || fd->isPrePassStub) return;
        if(!accept(fd->name, fd->returnType.name)) return;
        json item = {{"label", fd->dName()}, {"kind", fd->isValueEmitter ? 6 : 3},
                     {"detail", fd->returnType.dName()}};
        if(!fd->isValueEmitter) {
            item["insertText"] = fd->dName() + (fd->params.empty() ? "()" : "($0)");
            item["insertTextFormat"] = 2;  // Snippet
            if(!fd->params.empty()) item["command"] = {{"title", ""}, {"command", "editor.action.triggerParameterHints"}};
        }
        if(!fd->docComment.empty()) item["documentation"] = {{"kind", "markdown"}, {"value", fd->docComment}};
        items.push_back(item);
    };
    auto addMember = [&](typeMember* m) {
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)) addVar(vd, 5);   // Field
        else if(auto* fd = dynamic_cast<functionDef*>(m)) addFn(fd);
    };

    // Locals (declared above the cursor) and parameters of the enclosing routines, lambdas first, then
    // the members of the class/object that owns them.
    int cursorLine1Based = line + 1;
    FunctionContext ctx = functionContext(uri, line, col);
    // Nested-block locals are included once declared above the cursor, even from a sibling
    // block that has closed — blocks carry no end line to scope them more tightly.
    std::function<void(statementBlock*)> walk = [&](statementBlock* blk) {
        if(!blk) return;
        for(statement* st : blk->statements) {
            if(auto* vd = dynamic_cast<variableDeclaration*>(st)) {
                if(vd->src.line == 0 || vd->src.line <= cursorLine1Based) addVar(vd, 6);
            }
            else if(auto* is = dynamic_cast<ifStatement*>(st))      { walk(is->thenBlock); walk(is->elseBlock); }
            else if(auto* ws = dynamic_cast<whileStatement*>(st))   walk(ws->body);
            else if(auto* ds = dynamic_cast<doStatement*>(st))      walk(ds->body);
            else if(auto* fs = dynamic_cast<forStatement*>(st))     walk(fs->body);
            else if(auto* fi = dynamic_cast<forInStatement*>(st))   walk(fi->body);
            else if(auto* ss = dynamic_cast<switchStatement*>(st))  { for(switchCase* c : ss->cases) walk(c->body); }
            else if(auto* tc = dynamic_cast<tryCatchStatement*>(st)){ walk(tc->tryBody); walk(tc->catchBody); }
        }
    };
    for(functionDef* fn : ctx.chain) {
        walk(dynamic_cast<statementBlock*>(fn->body));
        for(paramDef* p : fn->params)
            if(accept(p->name, p->type.name))
                items.push_back({{"label", p->displayName.empty() ? p->name : p->displayName},
                                 {"kind", 6}, {"detail", p->type.dName()}});
    }
    if(ctx.ownerClass) {
        ctx.ownerClass->forEachMember(addMember);
    } else if(ctx.ownerObject) {
        for(typeMember* m : ctx.ownerObject->members) addMember(m);
        if(ctx.ownerObject->objectClass) ctx.ownerObject->objectClass->forEachMember(addMember);
    }

    // File scope: globals, functions and objects, then #using-imported namespace members.
    for(typeDef* g : languageService.globals) {
        if(auto* vd = dynamic_cast<variableDeclaration*>(g)) addVar(vd, 6);
        else if(auto* fd = dynamic_cast<functionDef*>(g)) addFn(fd);
        else if(auto* od = dynamic_cast<objectDef*>(g)) {
            if(od->isPrePassStub) continue;
            string otype = od->objectClass ? od->objectClass->name : od->name;
            if(accept(od->name, otype))
                items.push_back({{"label", od->dName()}, {"kind", 6},
                                 {"detail", od->objectClass ? od->objectClass->dName() : string("_bglObject")}});
        }
    }
    for(objectDef* ns : activeUsingNamespaces(docText, line))
        for(typeMember* m : ns->members) addMember(m);
}

// Completion case: a call argument whose parameter type is known (and not an enum) -> the in-scope
// symbols assignable to it: locals, parameters, enclosing-type members, globals, objects, #using
// members, and functions returning a compatible type. Fires only while the slot holds nothing but an
// identifier prefix, so an expression in progress (`f(a + |`) falls through untouched.
json LspServer::completeTypedArgument(const string& uri, int line, int col, const string& lineText,
                                      const string& docText, bool& handled) {
    handled = false;
    string funcName, objName;
    int argIndex = 0, slotStart = 0;
    if(!findCallSlot(lineText, col, funcName, objName, argIndex, slotStart)) return json();
    int end = std::min(col, (int)lineText.size());
    for(int i = slotStart; i < end; i++) {
        unsigned char c = (unsigned char)lineText[i];
        if(!(isalnum(c) || c == '_' || isspace(c))) return json();
    }
    // The slot's accepted types, across every overload that has this many parameters.
    vector<string> ptypes;
    for(functionDef* fd : resolveCallees(uri, line, col, funcName, objName, docText)) {
        if(argIndex >= (int)fd->params.size()) continue;
        const string& t = fd->params[argIndex]->type.name;
        if(t.empty() || t == "var") return json();   // an untyped overload accepts anything: nothing to narrow
        if(std::find(ptypes.begin(), ptypes.end(), t) == ptypes.end()) ptypes.push_back(t);
    }
    if(ptypes.empty()) return json();
    auto fits = [&](const string& type) {
        for(const string& pt : ptypes) if(parser.isTypeCompatible(type, pt)) return true;
        return false;
    };
    auto wants = [&](const char* t) { return std::find(ptypes.begin(), ptypes.end(), t) != ptypes.end(); };

    json items = json::array();
    // Literals first — the most common thing handed to a typed slot.
    if(wants("string"))
        items.push_back({{"label", "\"\""}, {"kind", 12}, {"detail", "string literal"},
                         {"insertText", "\"$0\""}, {"insertTextFormat", 2}, {"sortText", "0"}});
    if(wants("bool"))
        for(const char* b : {"true", "false"})
            items.push_back({{"label", b}, {"kind", 14}, {"detail", "bool"}, {"sortText", "0"}});

    std::set<string> seen;
    appendScopeItems(items, seen, uri, line, col, docText, fits, /*typed*/true);

    handled = true;
    return json{{"isIncomplete", false}, {"items", items}};
}

// Completion case: `#include <...>` beguiLib library names.
json LspServer::completeAngleInclude(int line, int col, const string& lineText, bool& handled) {
    handled = false;
    // ── Phase 0: `#include <…>` library completion ───────────────────────
    // When the cursor is positioned inside the angle brackets of a `#include <…>` directive,
    // scan beguiLib for .bgl files and offer them. Names are returned without the .bgl
    // extension. Files starting with '_' (system/internal) or '.' are excluded.
    {
        int limit = col;
        if(limit > (int)lineText.size()) limit = (int)lineText.size();
        int i = limit - 1;
        // Skip the partial name being typed (identifier-ish chars)
        while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_' || lineText[i] == '.')) i--;
        // Expect '<' next (after optional whitespace)
        while(i >= 0 && isspace((unsigned char)lineText[i])) i--;
        if(i >= 0 && lineText[i] == '<') {
            int angleCol = i;   // column of the opening '<' (for the edit range)
            i--;
            while(i >= 0 && isspace((unsigned char)lineText[i])) i--;
            // Walk back over the word right before the whitespace — should be "include" (case-insensitive)
            int wordEnd = i + 1;
            while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
            int wordStart = i + 1;
            string word = lineText.substr(wordStart, wordEnd - wordStart);
            for(char& c : word) c = (char)tolower((unsigned char)c);
            // Expect '#' immediately before the word (this is `#include`, not a bare `include`)
            if(word == "include" && i >= 0 && lineText[i] == '#') {
                json items = json::array();
                string query = includeQuery(lineText, col, angleCol);
                try {
                    std::filesystem::path libRoot = settings.libPath;
                    for(const auto& entry : std::filesystem::recursive_directory_iterator(libRoot)) {
                        if(!entry.is_regular_file()) continue;
                        std::filesystem::path p = entry.path();
                        string filename = p.filename().string();
                        // Filter: must end in .bgl (case-insensitive)
                        if(filename.size() < 4) continue;
                        string ext = filename.substr(filename.size() - 4);
                        for(char& c : ext) c = (char)tolower((unsigned char)c);
                        if(ext != ".bgl") continue;
                        // Filter: skip files whose basename starts with '_' or '.' (system/internal/hidden)
                        if(filename[0] == '_' || filename[0] == '.') continue;
                        // Build the include name: relative path from lib root, drop .bgl extension,
                        // normalize path separators to '/' (Beguile's #include syntax uses forward slashes).
                        std::filesystem::path rel = std::filesystem::relative(p, libRoot);
                        string relStr = rel.string();
                        for(char& c : relStr) if(c == '\\') c = '/';
                        if(relStr.size() >= 4)
                            relStr = relStr.substr(0, relStr.size() - 4);  // strip ".bgl"
                        if(!ciContains(relStr, query)) continue;     // server-side substring filter
                        items.push_back(makeIncludeItem(relStr, "Beguile language extension", angleCol, query, line, col));
                    }
                } catch(...) { /* directory unreadable — fall back to empty list */ }
                // isIncomplete: re-query each keystroke so the substring filter re-runs.
                handled = true; return json{{"isIncomplete", true}, {"items", items}};
            }
        }
    }
    return json();
}

// Completion case: class-header inheritance position (`class Foo : |`, `alias class Foo for |`).
json LspServer::completeClassHeader(int col, const string& lineText, bool& handled) {
    handled = false;
    // ── Phase 0.5: class inheritance position ────────────────────────────
    // Detect cursor in:
    //   `class Foo : <here>`                (single inheritance start)
    //   `class Foo : object, <here>`        (multi-inheritance continuation)
    //   `alias class Foo for <here>`        (alias-class single parent)
    // Returns all known classes as completion items so users can discover available
    // base classes (especially mixins like bglAllocated).
    do {
        // Build prefix through cursor
        string prefix = lineText.substr(0, std::min((int)lineText.size(), col));
        // Strip trailing partial identifier (the name being typed)
        while(!prefix.empty() && (isalnum((unsigned char)prefix.back()) || prefix.back() == '_'))
            prefix.pop_back();
        // Strip trailing whitespace
        while(!prefix.empty() && isspace((unsigned char)prefix.back())) prefix.pop_back();

        bool isAfterColon = false, isAfterComma = false, isAfterFor = false;
        if(!prefix.empty()) {
            char last = prefix.back();
            if(last == ':')      isAfterColon = true;
            else if(last == ',') isAfterComma = true;
            else if(prefix.size() >= 3 && prefix.substr(prefix.size()-3) == "for") {
                size_t s = prefix.size() - 3;
                if(s == 0 || isspace((unsigned char)prefix[s-1])) {
                    isAfterFor = true;
                    prefix.resize(s);
                }
            }
        }
        if(!isAfterColon && !isAfterComma && !isAfterFor) break;

        if(isAfterColon || isAfterComma) prefix.pop_back();
        while(!prefix.empty() && isspace((unsigned char)prefix.back())) prefix.pop_back();

        // Comma case: walk back through the existing `id (, id)*` chain to find the originating `:`
        if(isAfterComma) {
            while(!prefix.empty() && (isalnum((unsigned char)prefix.back()) || prefix.back() == '_'))
                prefix.pop_back();
            while(!prefix.empty() && isspace((unsigned char)prefix.back())) prefix.pop_back();
            while(!prefix.empty() && prefix.back() == ',') {
                prefix.pop_back();
                while(!prefix.empty() && isspace((unsigned char)prefix.back())) prefix.pop_back();
                while(!prefix.empty() && (isalnum((unsigned char)prefix.back()) || prefix.back() == '_'))
                    prefix.pop_back();
                while(!prefix.empty() && isspace((unsigned char)prefix.back())) prefix.pop_back();
            }
            if(prefix.empty() || prefix.back() != ':') break;  // comma wasn't part of inheritance
            prefix.pop_back();
            while(!prefix.empty() && isspace((unsigned char)prefix.back())) prefix.pop_back();
        }

        // Verify `class` (and `alias` for the `for` case) appear as words in the remaining prefix.
        // Beguile's `class` is only used in class declarations, so this is a reliable signal.
        auto containsWord = [&prefix](const string& w) -> bool {
            size_t pos = 0;
            while((pos = prefix.find(w, pos)) != string::npos) {
                bool leftOk  = (pos == 0) || (!isalnum((unsigned char)prefix[pos-1]) && prefix[pos-1] != '_');
                bool rightOk = (pos + w.size() >= prefix.size()) ||
                               (!isalnum((unsigned char)prefix[pos + w.size()]) && prefix[pos + w.size()] != '_');
                if(leftOk && rightOk) return true;
                pos += w.size();
            }
            return false;
        };
        if(!containsWord("class")) break;
        if(isAfterFor && !containsWord("alias")) break;

        // We're in inheritance position — emit class completions
        json items = json::array();
        for(typeDef* t : languageService.objectTypes) {
            auto* cd = dynamic_cast<classDef*>(t);
            if(!cd) continue;
            if(!cd->name.empty() && cd->name[0] == '_') continue;  // skip system internals
            if(cd->isValue && !cd->isEmitterClass) continue;        // a struct has no subclasses
            const string& label = cd->displayName.empty() ? cd->name : cd->displayName;
            items.push_back({
                {"label", label},
                {"kind", 7},   // CompletionItemKind.Class
                {"detail", "class"}
            });
        }
        handled = true; return items;
    } while(false);
    return json();
}

// Completion case: dotted access (`foo.|`) - members of foo's type, or an enum type's values.
json LspServer::completeDottedMember(const string& uri, int line, int col, const string& lineText, bool& handled) {
    handled = false;
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

        LspTarget target;
        if(!resolveTarget(uri, line, col, lower, target)) { handled = true; return json::array(); }

        json items = json::array();
        std::set<string> seenLabels;
        auto push = [&](const string& label, json item) {
            string key = label.substr(0, label.find('('));
            transform(key.begin(), key.end(), key.begin(), ::tolower);
            if(!seenLabels.insert(key).second) return;
            item["label"] = label;
            items.push_back(item);
        };
        // The enum type itself lists its values.
        if(target.enumType) {
            string enumLabel = target.enumType->dName();
            for(enumValueDef* ev : target.enumType->namedValues)
                push(ev->dName(), {{"kind", 20}, {"detail", enumLabel}});   // EnumMember
            handled = true; return items;
        }
        // An object's own members: aliases, auto aliases, methods and properties.
        if(target.obj) {
            for(typeMember* m : target.obj->members) {
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                    if(vd->isAlias) push(vd->dName(), {{"kind", 7}, {"detail", "type alias → " + vd->type.dName()}});
                    else push(vd->dName(), {{"kind", 6}, {"detail", vd->type.dName()}});
                } else if(auto* fd = dynamic_cast<functionDef*>(m)) {
                    if(!fd->isPrePassStub) push(fd->dName(), {{"kind", fd->isValueEmitter ? 6 : 2}, {"detail", fd->returnType.dName()}});
                }
            }
        }
        // Then the class hierarchy's members. An object declared directly on the root (`_bglObject bgl { … }`)
        // is a namespace: its own members are the point, not the root's plumbing.
        vector<classDef*> classes;
        if(target.cls && !(target.obj && target.cls->name == "_bglobject")) {
            classes.push_back(target.cls);
            if(classDef* converted = conversionTarget(target.cls).cls) classes.push_back(converted);   // what the value reads as
        }
        for(classDef* listed : classes) {
            listed->forEachMember([&](typeMember* m) {
                if(auto* fd = dynamic_cast<functionDef*>(m)) {
                    if(fd->isPrePassStub) return;
                    // Operators (`==`, `()`, …) are not reached with '.'.
                    if(fd->name.empty() || !(isalpha((unsigned char)fd->name[0]) || fd->name[0] == '_')) return;
                    string label = fd->dName();
                    if(!fd->isValueEmitter) {
                        label += "(";
                        for(size_t i = 0; i < fd->params.size(); i++) {
                            if(i > 0) label += ", ";
                            label += fd->params[i]->type.name;
                        }
                        label += ")";
                    }
                    push(label, {{"kind", fd->isValueEmitter ? 6 : 2}, {"detail", fd->returnType.name}});
                } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                    push(vd->dName(), {{"kind", 6}, {"detail", vd->type.name}});
                }
            });
        }
        handled = true; return items;
    }
    return json();
}

// Completion case: inside a grammar pattern literal -> grammarToken enum members.
json LspServer::completeGrammarPattern(int line, int col, const string& docText, bool& handled) {
    handled = false;
    // ── Phase 1.5: grammar pattern literal context ────────────────────────
    // Detect cursor inside a grammar pattern literal:
    //   verb V { grammar = { {.w, ▮} } }            (canonical)
    //   verb V { grammar = {.w, ▮} }                (single-line shorthand)
    //   extend V { grammar = { {.w, ▮} } }          (append; default in extend)
    //   extend V { replace grammar = { {.w, ▮} } }  (destructive replace)
    //   grammarRule r = {Verb, {.w, ▮}}              (rule literal form)
    // Offers grammarToken enum members (noun, held, creature, reverse, etc.).
    //
    // Detection: find the cursor's immediately-enclosing `{`. If its first
    // non-whitespace content is a `.` (dictionaryWord literal prefix), the
    // brace opens a grammar pattern. This catches all four shapes above while
    // rejecting non-pattern braces (function bodies, class bodies, object
    // bodies, outer `grammar = { ▮ }` between lines, rule-literal verb slot,
    // etc.). Strings/comments not specially handled — grammar patterns don't
    // contain them, and incidental string braces in surrounding scope are
    // matched in their own pairs and don't change the walk.
    {
        // Compute byte offset of (line, col) in docText.
        size_t cursorOffset = 0;
        {
            int curLine = 0;
            size_t i = 0;
            while(i < docText.size() && curLine < line) {
                if(docText[i] == '\n') curLine++;
                i++;
            }
            cursorOffset = i + (size_t)col;
            if(cursorOffset > docText.size()) cursorOffset = docText.size();
        }

        // Walk back to find the immediately-enclosing `{`.
        ptrdiff_t openerPos = -1;
        int depth = 0;
        for(ptrdiff_t i = (ptrdiff_t)cursorOffset - 1; i >= 0; i--) {
            char c = docText[(size_t)i];
            if(c == '}') depth++;
            else if(c == '{') {
                depth--;
                if(depth < 0) { openerPos = i; break; }
            }
        }

        bool inGrammar = false;
        if(openerPos >= 0) {
            size_t j = (size_t)openerPos + 1;
            while(j < docText.size() && isspace((unsigned char)docText[j])) j++;
            inGrammar = (j < docText.size() && docText[j] == '.');
        }

        if(inGrammar) {
            json items = json::array();
            for(typeDef* t : languageService.objectTypes) {
                auto* ed = dynamic_cast<enumDef*>(t);
                if(!ed) continue;
                if(ed->name != "grammartoken") continue;
                for(enumValueDef* ev : ed->namedValues) {
                    json item = {
                        {"label", ev->name},
                        {"kind", 13},  // CompletionItemKind.EnumMember
                        {"detail", "grammarToken"}
                    };
                    if(!ev->docComment.empty())
                        item["documentation"] = {{"kind", "markdown"}, {"value", ev->docComment}};
                    items.push_back(item);
                }
                break;
            }
            handled = true; return items;
        }
    }
    return json();
}

// Completion case: an `attributeList`-typed member initializer -> every `attribute` instance.
json LspServer::completeAttributeListLiteral(int line, int col, const string& docText, bool& handled) {
    handled = false;
    // ── attributeList initializer completion ─────────────────────────────
    // Cursor inside an `attributeList`-typed member initializer literal —
    //   attributes = { ▮ }     // e.g. { light, !scenery }
    // offer every instance of class `attribute` (light, scenery, container, …). Detection:
    // find the immediately-enclosing `{`; read the `IDENT =` immediately before it; if that
    // member name is declared anywhere with type `attributeList`, we're in such a literal.
    // Generalizes to any attributeList member, not just the built-in `attributes`.
    {
        size_t cursorOffset = 0;
        {
            int curLine = 0; size_t i = 0;
            while(i < docText.size() && curLine < line) { if(docText[i] == '\n') curLine++; i++; }
            cursorOffset = i + (size_t)col;
            if(cursorOffset > docText.size()) cursorOffset = docText.size();
        }

        // Immediately-enclosing `{`.
        ptrdiff_t openerPos = -1; int depth = 0;
        for(ptrdiff_t i = (ptrdiff_t)cursorOffset - 1; i >= 0; i--) {
            char c = docText[(size_t)i];
            if(c == '}') depth++;
            else if(c == '{') { depth--; if(depth < 0) { openerPos = i; break; } }
        }

        string memberName;
        if(openerPos >= 0) {
            ptrdiff_t i = openerPos - 1;
            while(i >= 0 && isspace((unsigned char)docText[(size_t)i])) i--;   // skip ws before '{'
            if(i >= 0 && docText[(size_t)i] == '=' &&
               !(i > 0 && (docText[(size_t)i-1] == '=' || docText[(size_t)i-1] == '!' ||
                           docText[(size_t)i-1] == '<' || docText[(size_t)i-1] == '>'))) {  // a plain '=', not ==/!=/<=/>=
                i--;
                while(i >= 0 && isspace((unsigned char)docText[(size_t)i])) i--;   // skip ws before '='
                ptrdiff_t nameEnd = i + 1;
                while(i >= 0 && (isalnum((unsigned char)docText[(size_t)i]) || docText[(size_t)i] == '_')) i--;
                memberName = docText.substr((size_t)(i + 1), (size_t)(nameEnd - (i + 1)));
                transform(memberName.begin(), memberName.end(), memberName.begin(), ::tolower);
            }
        }

        // Is `memberName` declared (on any class/object) with type `attributeList`?
        bool isAttrListMember = false;
        if(!memberName.empty()) {
            auto memberIsAttrList = [&](const vector<typeMember*>& members) {
                for(typeMember* m : members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == memberName && vd->type.name == "attributelist") return true;
                return false;
            };
            for(typeDef* t : languageService.objectTypes) {
                if(auto* cd = dynamic_cast<classDef*>(t)) { if(memberIsAttrList(cd->members)) { isAttrListMember = true; break; } }
            }
            if(!isAttrListMember)
                for(typeDef* t : languageService.objectInstances)
                    if(auto* od = dynamic_cast<objectDef*>(t)) { if(memberIsAttrList(od->members)) { isAttrListMember = true; break; } }
        }

        if(isAttrListMember) {
            json items = json::array();
            auto addAttrInstance = [&](abstractObject* o, const string& typeName) {
                if(typeName != "attribute") return;
                json item = {{"label", o->dName()}, {"kind", 6},  // CompletionItemKind.Variable
                             {"detail", "attribute"}};
                if(!o->docComment.empty())
                    item["documentation"] = {{"kind", "markdown"}, {"value", o->docComment}};
                items.push_back(item);
            };
            for(typeDef* g : languageService.globals)
                if(auto* vd = dynamic_cast<variableDeclaration*>(g)) {
                    if(vd->isConst) continue;  // skip the NO_ATTRIBUTE sentinel (attribute-valued 0, not a set member)
                    addAttrInstance(vd, vd->type.name);
                }
            for(typeDef* t : languageService.objectInstances)
                if(auto* od = dynamic_cast<objectDef*>(t))
                    addAttrInstance(od, od->objectClass ? od->objectClass->name : string());
            if(!items.empty()) { handled = true; return items; }
        }
    }
    return json();
}

// Completion case: `extend <verb|array> { | }` member position -> the members an extend may add.
json LspServer::completeExtendBody(int line, int col, const string& docText, bool& handled) {
    handled = false;
    // ── Phase 1.6: extend-body member completion ─────────────────────────
    // Inside `extend <Verb> { ▮ }` at member position, offer the members you add to a
    // verb via an extend: `grammar` (+=/-=/replace), `synonyms`, `priority`, and the
    // `replace` qualifier. Gated on the extended name resolving to a known verb, so
    // object/class extends are unaffected. The grammar-pattern brace
    // (`grammar = { {.w ▮} }`) is caught by Phase 1.5 above, which runs first and returns.
    {
        // Byte offset of the cursor.
        size_t cursorOffset = 0;
        {
            int curLine = 0; size_t i = 0;
            while(i < docText.size() && curLine < line) { if(docText[i] == '\n') curLine++; i++; }
            cursorOffset = i + (size_t)col;
            if(cursorOffset > docText.size()) cursorOffset = docText.size();
        }
        // Immediately-enclosing `{`.
        ptrdiff_t openerPos = -1; int depth = 0;
        for(ptrdiff_t i = (ptrdiff_t)cursorOffset - 1; i >= 0; i--) {
            char c = docText[(size_t)i];
            if(c == '}') depth++;
            else if(c == '{') { depth--; if(depth < 0) { openerPos = i; break; } }
        }
        if(openerPos >= 0) {
            // Walk back over the opener's head: whitespace, the extended name, whitespace,
            // then require the keyword `extend` immediately before it.
            ptrdiff_t i = openerPos - 1;
            while(i >= 0 && isspace((unsigned char)docText[(size_t)i])) i--;
            ptrdiff_t nameEnd = i + 1;
            while(i >= 0 && (isalnum((unsigned char)docText[(size_t)i]) || docText[(size_t)i] == '_')) i--;
            ptrdiff_t nameStart = i + 1;
            string extName = (nameStart < nameEnd) ? docText.substr((size_t)nameStart, (size_t)(nameEnd - nameStart)) : "";
            while(i >= 0 && isspace((unsigned char)docText[(size_t)i])) i--;
            ptrdiff_t kwEnd = i + 1;
            while(i >= 0 && (isalnum((unsigned char)docText[(size_t)i]) || docText[(size_t)i] == '_')) i--;
            string kw = (i + 1 < kwEnd) ? docText.substr((size_t)(i + 1), (size_t)(kwEnd - (i + 1))) : "";
            string kwl = kw, extl = extName;
            transform(kwl.begin(), kwl.end(), kwl.begin(), ::tolower);
            transform(extl.begin(), extl.end(), extl.begin(), ::tolower);
            if(kwl == "extend" && !extl.empty()) {
                bool isVerb = false;
                for(auto* v : languageService.verbs) if(v->name == extl) { isVerb = true; break; }
                if(isVerb) {
                    json items = json::array();
                    auto add = [&](const char* label, const char* detail, const char* doc) {
                        items.push_back({{"label", label}, {"kind", 14},  // CompletionItemKind.Keyword
                            {"detail", detail},
                            {"documentation", {{"kind", "markdown"}, {"value", doc}}}});
                    };
                    add("grammar",  "verb grammar",  "Add or alter this verb's grammar: `grammar += { … }` (append), `grammar -= { … }` (remove matching lines / evict a word), `replace grammar = { … }` (wipe and replace).");
                    add("synonyms", "verb synonyms", "`synonyms = {.w1, .w2};` — declare true I6 aliases of this verb (emits `Verb 'w1' 'w2' = '<anchor>';`), so later extensions flow to them.");
                    add("priority", "verb priority", "`priority = N;` — block-local grammar priority stamped onto the lines this extend adds (lower number = matched earlier).");
                    add("replace",  "member replace","Qualifier to override an inherited member: `replace grammar = { … }` or `replace void handler(){ … }`.");
                    handled = true; return items;
                }
                // extend <array> { inject/remove/move … } — declarative build-time array editing.
                // Gated on the extended name resolving to a non-extern declared array (extern arrays
                // have no Beguile-owned initializer to edit, matching the compiler's rejection).
                bool isArray = false;
                for(typeDef* g : languageService.globals)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(g))
                        if(ad->name == extl && !ad->isExternal) { isArray = true; break; }
                if(isArray) {
                    json items = json::array();
                    auto add = [&](const char* label, const char* detail, const char* doc) {
                        items.push_back({{"label", label}, {"kind", 14},  // CompletionItemKind.Keyword
                            {"detail", detail},
                            {"documentation", {{"kind", "markdown"}, {"value", doc}}}});
                    };
                    add("inject", "add an element", "`inject <element> [after X | before X | first | last];` — splice an element in at a position (no clause = append). The element is a reference, a value, or an inline object `Type{ … }` / inferred `{ … }`.");
                    add("remove", "remove an element", "`remove <name | [N]>;` — remove an element by reference or index.");
                    add("move",   "reposition an element", "`move <name | [N]> <after Y | before Y | first | last>;` — reposition an existing element (remove-then-inject at the new spot).");
                    handled = true; return items;
                }
            }
        }
    }
    return json();
}

// Completion case: first token of a class/object member declaration -> the valid member modifiers.
json LspServer::completeMemberModifiers(int line, int col, const string& docText, bool insideBsBlock, bool& handled) {
    handled = false;
    // ── Phase 1.7: class/object member-declaration position — offer member modifiers ──
    // At the FIRST token of a member declaration inside a class or object body, offer the
    // modifiers valid there. Context-scoped (never a flat always-on list): gated on the
    // enclosing '{' being a class/object body (NOT a function body, control block, array
    // initializer, or inline-object literal) AND the cursor being at a fresh member start.
    // Returned as isIncomplete so type-name/word-based completions still merge in.
    if(!insideBsBlock) {
        // Cursor byte offset.
        size_t cursorOffset = 0;
        {
            int curLine = 0; size_t i = 0;
            while(i < docText.size() && curLine < line) { if(docText[i] == '\n') curLine++; i++; }
            cursorOffset = i + (size_t)col;
            if(cursorOffset > docText.size()) cursorOffset = docText.size();
        }
        // Immediately-enclosing '{' (depth-matched).
        ptrdiff_t openerPos = -1; int depth = 0;
        for(ptrdiff_t i = (ptrdiff_t)cursorOffset - 1; i >= 0; i--) {
            char c = docText[(size_t)i];
            if(c == '}') depth++;
            else if(c == '{') { depth--; if(depth < 0) { openerPos = i; break; } }
        }
        if(openerPos >= 0) {
            // (a) Fresh member start? Between the nearest statement boundary ('{', ';', '}') and the
            //     cursor there must be at most a single partial identifier (the first token being typed)
            //     — no completed type, '=', or '(' yet.
            ptrdiff_t b = (ptrdiff_t)cursorOffset - 1;
            while(b >= 0 && docText[(size_t)b] != '{' && docText[(size_t)b] != ';' && docText[(size_t)b] != '}') b--;
            string seg = docText.substr((size_t)(b + 1), cursorOffset - (size_t)(b + 1));
            size_t sp = seg.find_first_not_of(" \t\r\n");
            string head0 = (sp == string::npos) ? "" : seg.substr(sp);
            bool firstToken = head0.find_first_of(" \t\r\n=(){}") == string::npos;
            if(firstToken && b >= openerPos) {
                // (b) Classify the enclosing opener: walk back to the statement boundary before '{'.
                ptrdiff_t h = openerPos - 1;
                while(h >= 0 && docText[(size_t)h] != ';' && docText[(size_t)h] != '{' && docText[(size_t)h] != '}') h--;
                string headText = docText.substr((size_t)(h + 1), (size_t)(openerPos - (h + 1)));
                bool hasParen = headText.find('(') != string::npos;
                // An '=' anywhere in the head means this '{' is an initializer / inline-object value
                // (`array<T> a = {`, `T x = T{ … }`), not an object or class body.
                bool isInitializer = headText.find('=') != string::npos;
                // First two words of the head.
                string firstWord, secondWord;
                {
                    size_t s0 = headText.find_first_not_of(" \t\r\n");
                    if(s0 != string::npos) {
                        size_t e0 = headText.find_first_of(" \t\r\n:{<", s0);
                        firstWord = headText.substr(s0, (e0 == string::npos ? headText.size() : e0) - s0);
                        if(e0 != string::npos && headText[e0] != ':') {
                            size_t s1 = headText.find_first_not_of(" \t\r\n", e0);
                            if(s1 != string::npos && headText[s1] != ':' && headText[s1] != '{') {
                                size_t e1 = headText.find_first_of(" \t\r\n:{<(", s1);
                                secondWord = headText.substr(s1, (e1 == string::npos ? headText.size() : e1) - s1);
                            }
                        }
                    }
                }
                string fw = firstWord;
                transform(fw.begin(), fw.end(), fw.begin(), ::tolower);
                static const set<string> controlKw = {"if","while","for","switch","do","else","try","catch","func"};
                // `extend` over a VERB or ARRAY is owned by Phase 1.6 (which returns before this phase),
                // so only an EXPLICIT `extend class`/`extend object` counts as a member body here — a
                // bare `extend <name> {` stays silent rather than guessing modifiers for a verb/array.
                string sw = secondWord; transform(sw.begin(), sw.end(), sw.begin(), ::tolower);
                bool isClassBody  = (fw == "class" || fw == "struct")
                                 || (fw == "extend" && (sw == "class" || sw == "struct" || sw == "object"));
                // `Type name {` (two identifiers, no parens/initializer/control-or-extend kw) → object body.
                bool isObjectBody = !hasParen && !isInitializer && !controlKw.count(fw)
                                    && fw != "class" && fw != "struct" && fw != "enum" && fw != "grammar" && fw != "extend"
                                    && !firstWord.empty() && !secondWord.empty();
                if(isClassBody || isObjectBody) {
                    json items = json::array();
                    auto add = [&](const char* label, const char* detail, const char* doc) {
                        items.push_back({{"label", label}, {"kind", 14},  // CompletionItemKind.Keyword
                            {"detail", detail},
                            {"documentation", {{"kind", "markdown"}, {"value", doc}}}});
                    };
                    add("static", "static member", "`static` — one shared slot at class level, not per-instance.");
                    add("const",  "constant member", "`const` — an immutable member (must be initialized).");
                    add("replace","override inherited", "`replace` — override an inherited member (`replace void handler(){…}` / `replace grammar = {…}`).");
                    add("emitter","emitter member", "`emitter` — an inline-I6 member (expanded at the call site, no runtime call).");
                    add("superposed","superposed member", "`superposed` — a lazily-emitted, zero-footprint member.");
                    if(isClassBody) {
                        add("inline", "positional slot", "`inline` — mark a member so it takes a positional value in inline object construction `Type{ v1, v2 }`.");
                        add("required", "required member", "`required` — every object of the class must give this member a value, itself or through a subclass.");
                        add("default","class default member", "`default` — a member value provided as the class default (valid in class declarations only).");
                    }
                    handled = true; return json{{"isIncomplete", true}, {"items", items}};
                }
            }
        }
    }
    return json();
}

// Completion case: inside `#beguilerSettings { ... }` -> enum RHS values, else the property names.
json LspServer::completeBeguilerSettingsBlock(int col, const string& lineText, bool insideBsBlock, bool& handled) {
    handled = false;
    // ── Phases 2 & 3: #beguilerSettings block ─────────────────────────────
    if(insideBsBlock) {
        classDef* schema = languageService.findClass("beguilersettingstype");
        if(!schema) { handled = true; return json::array(); }  // type not registered (shouldn't happen)

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
            if(propType.empty()) { handled = true; return json::array(); }
            auto* ed = languageService.findEnum(propType);
            if(!ed) { handled = true; return nullptr; }  // primitive (string, int, bool) — let client fall back
            json items = json::array();
            for(enumValueDef* ev : ed->namedValues) {
                items.push_back({
                    {"label", ev->displayName.empty() ? ev->name : ev->displayName},
                    {"kind", 20},
                    {"detail", ed->displayName.empty() ? ed->name : ed->displayName}
                });
            }
            handled = true; return items;
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
        handled = true; return items;
    }
    return json();
}

// Completion case: a bare prefix of `enum` -> the keyword syntax snippet + doc popup.
json LspServer::completeKeywordSnippet(int col, const string& lineText, bool& handled) {
    handled = false;
    // ── Phase 3.9 (EXPERIMENTAL, enum-only): keyword syntax snippet + doc popup ──────────────
    // As you type a prefix of `enum` at a bare-identifier position (dotted/string/member-body/
    // bs-block contexts have all returned above), offer an `enum` completion whose documentation
    // shows the syntax and whose snippet body scaffolds a skeleton. Self-contained POC — delete
    // this whole block to back it out. If it proves useful, generalize to class/object/verb/…
    {
        int i = col - 1;
        if(i >= (int)lineText.size()) i = (int)lineText.size() - 1;
        int wEnd = i + 1;
        while(i >= 0 && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i--;
        int wStart = i + 1;
        bool afterDot = (wStart - 1 >= 0 && lineText[wStart - 1] == '.');   // skip member access `x.en…`
        string partial = (wStart < wEnd) ? lineText.substr(wStart, wEnd - wStart) : "";
        string plow = partial; transform(plow.begin(), plow.end(), plow.begin(), ::tolower);
        // Only where a declaration can start, and once the word can't be another one (`else`, `end…`).
        bool lineStart = lineText.find_first_not_of(" \t") >= (size_t)wStart;
        if(syntaxHintsEnabled && !afterDot && lineStart && plow.size() >= 3 && string("enum").rfind(plow, 0) == 0){
            json item = {
                {"label", "enum"},
                {"kind", 14},                        // CompletionItemKind.Keyword
                {"detail", "enum declaration"},
                {"insertText", "enum ${1:Name} {\n\t${2:first},\n\t$0\n}"},
                {"insertTextFormat", 2},             // 2 = Snippet
                {"documentation", {{"kind", "markdown"}, {"value",
                    "**enum** — a named set of integer constants.\n\n"
                    "```beguile\n"
                    "enum Name {\n"
                    "    first,       // 1\n"
                    "    second,      // 2\n"
                    "    third = 7,   // an explicit value\n"
                    "}\n"
                    "```\n\n"
                    "The first value is `1`; each later member is one more than the one before unless given an "
                    "explicit `= N`. Reference members as `Name.member`."}}}
            };
            handled = true; return json{{"isIncomplete", true}, {"items", json::array({item})}};
        }
    }
    return json();
}

// Completion case: bare-identifier position -> members imported by active `#using` directives.
json LspServer::completeUsingImports(const string& uri, int line, int col, const string& docText, bool& handled) {
    handled = false;
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
                if(auto* od = languageService.findGlobalAs<objectDef>(head)) curObj = od;
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
                            if(auto* od = languageService.findGlobalAs<objectDef>(initName)) next = od;
                        if(!next)
                            if(auto* od = languageService.findGlobalAs<objectDef>(vd->type.name)) next = od;
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
                    if(auto* cls = languageService.findClass(path)){
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
                // A list from here replaces VS Code's word-based suggestions, so it must carry what
                // those would have offered: every name in scope (locals, parameters, members,
                // globals) and the keywords — not only the #using imports.
                std::set<string> seen;
                for(const json& it : items) {
                    string l = it["label"].get<string>();
                    transform(l.begin(), l.end(), l.begin(), ::tolower);
                    seen.insert(l);
                }
                appendScopeItems(items, seen, uri, line, col, docText, [](const string&){ return true; }, /*typed*/false);
                static const char* kKeywords[] = {
                    "if", "else", "for", "in", "while", "do", "switch", "case", "default", "break",
                    "continue", "return", "new", "delete", "try", "catch", "throw", "true", "false",
                    "null", "self", "auto", "var", "const", "static", "int", "bool", "char", "string",
                    "float", "void", "object", "array" };
                for(const char* k : kKeywords)
                    if(!seen.count(k)) items.push_back({{"label", k}, {"kind", 14}});
                handled = true; return json{{"isIncomplete", true}, {"items", items}};
            }
        }
    }
    return json();
}

json LspServer::handleCompletion(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();
    ensureParsedForRequest(uri);

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
    //   1.   Dotted access (foo.|)           — return members of foo's type / enum values if foo is an enum
    //   1.5. Grammar pattern literal         — cursor inside `grammar [+]= { … ▮ … }` → grammarToken values
    //   2.   Enum RHS in bs-block (prop = |) — return enum values for the property's declared type
    //   3.   Bare identifier in bs-block     — return beguilerSettingsType property names
    //   (and a typed call-argument slot `f(|` → in-scope symbols assignable to the parameter type)
    //   4.   Fallthrough                     — return nullptr (let client do word completion)
    // Dotted-access wins over bs-block member completion so `target = eTarget.` correctly
    // offers Glulx/Z3/Z5/Z8 instead of the 24 property names.

    bool insideBsBlock = isInBeguilerSettingsBlock(uri, line, col);

    // Each completeXxx() below owns one case. It sets `handled` and returns that case's result
    // when the cursor belongs to it; otherwise it leaves `handled` false and its return value is
    // ignored. The call order IS the phase order documented above.
    bool handled = false;
    if(json r = completeQuotedInclude(uri, line, col, lineText, handled); handled) return r;
    if(json r = completeI6Include(uri, line, col, lineText, handled); handled) return r;
    if(json r = completeIncludePathsDir(uri, line, col, lineText, insideBsBlock, handled); handled) return r;
    if(json r = completeEnumArgument(uri, line, col, lineText, docText, handled); handled) return r;

    // Suppress completion inside string/char literals and comments — the user is typing prose.
    // Scanned from the top of the file (multi-line strings/comments carry state across lines);
    // a position inside an interpolated string's {expr} slot is real code and isn't suppressed.
    if(isInStringOrComment(uri, line, col)) return nullptr;

    if(json r = completeAngleInclude(line, col, lineText, handled); handled) return r;
    if(json r = completeClassHeader(col, lineText, handled); handled) return r;
    if(json r = completeDottedMember(uri, line, col, lineText, handled); handled) return r;
    if(json r = completeTypedArgument(uri, line, col, lineText, docText, handled); handled) return r;
    if(json r = completeGrammarPattern(line, col, docText, handled); handled) return r;
    if(json r = completeAttributeListLiteral(line, col, docText, handled); handled) return r;
    if(json r = completeExtendBody(line, col, docText, handled); handled) return r;
    if(json r = completeMemberModifiers(line, col, docText, insideBsBlock, handled); handled) return r;
    if(json r = completeBeguilerSettingsBlock(col, lineText, insideBsBlock, handled); handled) return r;
    if(json r = completeKeywordSnippet(col, lineText, handled); handled) return r;
    if(json r = completeUsingImports(uri, line, col, docText, handled); handled) return r;

    return nullptr;
}

//=============================================================================
// Definition (go-to-definition)
//=============================================================================

json LspServer::handleDefinition(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();
    ensureParsedForRequest(uri);

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
    string settleName = lower;

    if((lower == "prefix" || lower == "postfix") && ownerName.empty() && followsOperatorKeyword(lineText, start)) {
        src.file = documentParsePaths.count(uri) ? documentParsePaths[uri] : uriToPath(uri);
        src.line = line + 1;
    } else if(!ownerName.empty()) {
        // Dotted member: resolve the owner chain to its target, then look the member up on it.
        string ownerLower = ownerName;
        transform(ownerLower.begin(), ownerLower.end(), ownerLower.begin(), ::tolower);
        LspTarget target;
        if(resolveTarget(uri, line, col, ownerLower, target)) {
            LspMember m = findMemberOn(target, lower);
            if(m.enumValue) {
                src = enumValueSource(m.enumType, lower);
            } else if(auto* vd = dynamic_cast<variableDeclaration*>(m.member)) {
                src = vd->src;
                // A type alias goes to the type it names; the alias line is the fallback.
                if(vd->isAlias)
                    if(auto* tc = languageService.findClass(vd->type.name))
                        if(!tc->src.file.empty()) { src = tc->src; settleName = tc->name; }
            } else if(auto* fd = dynamic_cast<functionDef*>(m.member)) {
                src = fd->src;
            }
        }
    } else {
        // Non-dotted: use the scoped resolver
        size_t after = lineText.find_first_not_of(" \t", end);
        LspSymbolRef ref = resolveSymbol(uri, line, col, lower, after != string::npos && lineText[after] == '(');
        if(ref.declSrc.line > 0) src = ref.declSrc;
        // Verbs fallback
        if(src.file.empty()) {
            for(auto* v : languageService.verbs)
                if(v->name == lower) { src = v->src; break; }
        }
    }

    if(src.file.empty() || src.line == 0) return nullptr;
    src = settleDeclLine(src, settleName);

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
    if(openDocuments.count(uri)) ensureParsedForRequest(uri);   // the outline is of this document's parse

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
            string className = od->objectClass ? od->objectClass->name : "_bglobject";
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
    ensureParsedForRequest(uri);

    // .inf-mode gating: outside #bgl{} regions, no Beguile signature help.
    if(!requestAllowedAt(uri, line, col)) return nullptr;

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;

    // Get the line text
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    string funcName, objName;
    int commaCount = 0, slotStart = 0;
    if(!findCallSlot(lineText, col, funcName, objName, commaCount, slotStart)) return nullptr;

    vector<functionDef*> candidates;
    for(functionDef* fd : resolveCallees(uri, line, col, funcName, objName, docIt->second))
        if(!fd->isValueEmitter) candidates.push_back(fd);

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
            string paramLabel = paramDisplay(fd->params[i]);
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
        string doc = fd->docComment;
        if(fd->isEmitter) doc = doc.empty() ? "emitter function" : doc + "\n\n*emitter function*";
        if(!doc.empty()) sig["documentation"] = {{"kind", "markdown"}, {"value", doc}};
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

// UTF-16 code units in s[0, byteEnd): LSP columns count these, the text here is UTF-8 bytes.
static int lspByteToU16(const string& s, int byteEnd) {
    int units = 0, k = 0, n = (int)s.size();
    if(byteEnd > n) byteEnd = n;
    while(k < byteEnd) {
        unsigned char ch = (unsigned char)s[k];
        if(ch < 0x80)      { units += 1; k += 1; }
        else if(ch < 0xE0) { units += 1; k += 2; }
        else if(ch < 0xF0) { units += 1; k += 3; }
        else               { units += 2; k += 4; }
    }
    return units;
}
// The byte offset of UTF-16 column `u16` in s.
static int lspU16ToByte(const string& s, int u16) {
    int units = 0, k = 0, n = (int)s.size();
    while(k < n && units < u16) {
        unsigned char ch = (unsigned char)s[k];
        if(ch < 0x80)      { units += 1; k += 1; }
        else if(ch < 0xE0) { units += 1; k += 2; }
        else if(ch < 0xF0) { units += 1; k += 3; }
        else               { units += 2; k += 4; }
    }
    return min(k, n);
}
static bool lspWordChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

// Every whole-word use of the identifier at (line, u16col) — matched ignoring case, as Beguile names are,
// outside strings and comments — across the open documents, or only inside the enclosing function for
// a local or parameter. `word` receives the identifier as written at the cursor.
vector<LspServer::WordHit> LspServer::wordOccurrences(const string& uri, int line, int u16col, string& word) {
    vector<WordHit> hits;
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return hits;
    ensureParsedForRequest(uri);
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);
    int col = lspU16ToByte(lineText, u16col);
    if(col >= (int)lineText.size()) return hits;
    int start = col, end = col;
    while(start > 0 && lspWordChar(lineText[start-1])) start--;
    while(end < (int)lineText.size() && lspWordChar(lineText[end])) end++;
    word = lineText.substr(start, end - start);
    if(word.empty() || isdigit((unsigned char)word[0])) { word.clear(); return hits; }

    string lowered = word;
    transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
    LspSymbolRef ref = resolveSymbol(uri, line, start, lowered);
    bool restrictToFunction = (ref.kind == LspSymbolRef::Local || ref.kind == LspSymbolRef::Parameter);
    int fnStart1 = -1, fnEnd1 = -1;
    string restrictFile;
    if(restrictToFunction) {
        auto rng = functionLines(uri, line, start, ref.enclosingFunc);
        fnStart1 = rng.first;
        fnEnd1   = rng.second + 1;
        restrictFile = ref.enclosingFunc->src.file;
    }
    for(auto& [docUri, docText] : openDocuments) {
        if(restrictToFunction) {
            string docPath = documentParsePaths.count(docUri) ? documentParsePaths[docUri] : uriToPath(docUri);
            if(docPath != restrictFile) continue;
        }
        istringstream docStream(docText);
        string docLine;
        int lineNum = 0;
        bool inBlockComment = false;
        while(getline(docStream, docLine)) {
            int line1Based = lineNum + 1;
            bool inRange = !restrictToFunction || (line1Based >= fnStart1 && line1Based < fnEnd1);
            size_t i = 0;
            char quote = 0;
            while(i < docLine.size()) {
                char c = docLine[i];
                if(inBlockComment){ if(c == '*' && i + 1 < docLine.size() && docLine[i+1] == '/'){ inBlockComment = false; i += 2; } else i++; continue; }
                if(quote){ if(c == '\\') i += 2; else { if(c == quote) quote = 0; i++; } continue; }
                if(c == '/' && i + 1 < docLine.size() && docLine[i+1] == '/') break;
                if(c == '/' && i + 1 < docLine.size() && docLine[i+1] == '*'){ inBlockComment = true; i += 2; continue; }
                if(c == '"' || c == '\''){ quote = c; i++; continue; }
                if(lspWordChar(c)){
                    size_t j = i;
                    while(j < docLine.size() && lspWordChar(docLine[j])) j++;
                    if(inRange && j - i == word.size() && strncasecmp(docLine.c_str() + i, word.c_str(), word.size()) == 0)
                        hits.push_back({docUri, lineNum, lspByteToU16(docLine, (int)i), lspByteToU16(docLine, (int)j)});
                    i = j;
                    continue;
                }
                i++;
            }
            lineNum++;
        }
    }
    return hits;
}

json LspServer::handleReferences(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    // .inf-mode gating: outside #bgl{} regions, no Beguile references.
    if(!requestAllowedAt(uri, line, col)) return json::array();

    string word;
    json locations = json::array();
    for(const WordHit& h : wordOccurrences(uri, line, col, word))
        locations.push_back({{"uri", h.uri}, {"range", {{"start", {{"line", h.line}, {"character", h.start}}},
                                                       {"end", {{"line", h.line}, {"character", h.end}}}}}});
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

    string oldName;
    json changes = json::object();
    for(const WordHit& h : wordOccurrences(uri, line, col, oldName)){
        if(!changes.contains(h.uri)) changes[h.uri] = json::array();
        changes[h.uri].push_back({{"range", {{"start", {{"line", h.line}, {"character", h.start}}},
                                             {"end", {{"line", h.line}, {"character", h.end}}}}},
                                  {"newText", newName}});
    }
    if(oldName.empty()) return nullptr;
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
    "print", "true", "false", "null", "self"
};
// Real declaration / member modifiers only. Class type names (object, int, bool, char,
// string, verb, attribute, attributeList, array, etc.) are intentionally NOT in this set —
// they're registered classes in the language service and should fall through to the
// stClass branch in classifyWord so they color the same as user-defined types. Same for
// non-class registered types like void/var/auto, which are caught by a separate check
// further down. `has` / `hasnt` were removed entirely — they're methods on object, not
// keywords.
static const set<string> bglModifiers = {
    "class", "enum", "extern", "emitter", "static", "const", "inline",
    "replace", "extend", "explicit", "default",
    // The rest of Appendix A's qualifier set, which this list had drifted behind.
    // `asI6`/`asBgl` are stored folded, as every keyword comparison here is.
    "additive", "alias", "asbgl", "asi6", "hide", "primitive", "ref", "required",
    "struct", "superposed", "synonyms", "typesealed"
};
// No `#ifdef` / `#ifndef`, in either hash form: `#if SYMBOL` and `#if !SYMBOL` test definedness
// (appendix A, 14.2.5). Colouring them as directives told the author they existed. `readonly` is
// likewise not a qualifier — the parser answers "Illegal global identifier".
static const set<string> bglDirectives = {
    "#include", "#once", "#define", "#defineI6", "#declare", "#if", "#else", "#endif",
    "#i6", "#startup", "#emitfirst", "#emitlast", "#using", "#exit"
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
    if(typeDef* g = languageService.findGlobal(lower)) {
        if(dynamic_cast<functionDef*>(g)) return stFunction;
        if(dynamic_cast<objectDef*>(g)) return stVariable;
        if(dynamic_cast<verbObjectDef*>(g)) return stVariable;
        if(dynamic_cast<variableDeclaration*>(g)) return stVariable;
    }

    // Check verbs
    for(auto* v : languageService.verbs)
        if(v->name == lower) return stVariable;

    return -1;  // unknown — don't emit a token
}

    // Pre-scan instance/class body ranges so bare identifiers inside them can be resolved as
    // members via class-hierarchy walk. Each entry: (startLine0, endLine0Exclusive, classDef*).
    // For 'room foyer { short_name = ... }', the block covers the interior of foyer's body and
    // maps to the 'room' classDef so 'short_name' (inherited from room) resolves as a property.
    // For 'class Counter { ... }', maps to Counter itself so bare member names inside the class
    // body resolve as members.
void LspServer::buildInstanceBlockIndex(const string& uri, const string& txt, SemanticScope& scope) {
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
        scope.instanceBlocks.push_back({ lineAt(bracePos) + 1, lineAt(closeEnd), cls, obj });
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
const LspServer::InstanceBlockRange* LspServer::blockForLine(const SemanticScope& scope, int lineNum) {
    const InstanceBlockRange* best = nullptr;
    int bestStart = -1;
    for(const auto& b : scope.instanceBlocks) {
        if(lineNum >= b.startLine0 && lineNum < b.endLine0Exclusive)
            if(b.startLine0 > bestStart) { bestStart = b.startLine0; best = &b; }
    }
    return best;
}

    // Walk a block's member scope (objectDef members first, then class hierarchy) looking for
    // 'name'. Returns kind: 1 = variable/property, 2 = function/method, 0 = not found.
int LspServer::findInBlock(const InstanceBlockRange* b, const string& name) {
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
    if(!b->cls) return 0;
    typeMember* hit = b->cls->findMember([&](typeMember* m){
        return m->name == name
               && (dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m));
    });
    if(dynamic_cast<variableDeclaration*>(hit)) return 1;
    if(dynamic_cast<functionDef*>(hit))         return 2;
    return 0;
}

    // `#using`-imported namespace members (bug: a bare `style` from `#using bgl.ui`, and its dotted
    // `.italics`/`.roman`, were never colored — only receivers already declared as globals resolved).
    // Precompute, from every active `#using <path>` in the doc (mirrors the completion-side resolution):
    //   usingMemberTok   — bare member name → token type, for coloring a bare `style`.
    //   usingMemberType  — bare member name → the (classDef, objectDef) it resolves to, so a dotted
    //                      `style.italics` can be resolved against that type. `#using` is file-scoped.
void LspServer::collectUsingNamespaceMembers(const string& docText, SemanticScope& scope) {
    auto objByName = [&](const string& nm) -> objectDef* {
        if(auto* od = languageService.findGlobalAs<objectDef>(nm)) return od;
        for(typeDef* t : languageService.objectInstances)
            if(auto* od = dynamic_cast<objectDef*>(t)) if(od->name == nm) return od;
        return nullptr;
    };
    auto walkObjectPath = [&](const string& path) -> objectDef* {
        size_t dot = path.find('.');
        string head = (dot == string::npos) ? path : path.substr(0, dot);
        string rest = (dot == string::npos) ? "" : path.substr(dot + 1);
        objectDef* curObj = objByName(head);
        while(curObj && !rest.empty()){
            dot = rest.find('.');
            string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
            rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
            objectDef* next = nullptr;
            for(typeMember* m : curObj->members){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(!vd || vd->name != seg) continue;
                string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                if(!initName.empty()) next = objByName(initName);
                if(!next)             next = objByName(vd->type.name);
                break;
            }
            curObj = next;
        }
        return curObj;
    };
    auto addMember = [&](typeMember* m){
        if(auto* fd = dynamic_cast<functionDef*>(m)){
            if(fd->isPrePassStub) return;
            scope.usingMemberTok[fd->name] = fd->isValueEmitter ? stVariable : stMethod;
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            scope.usingMemberTok[vd->name] = vd->isAlias ? stClass : stVariable;
            // Resolve what this member points at, so `member.foo` colors against it.
            string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
            objectDef* target = initName.empty() ? nullptr : objByName(initName);
            if(!target) target = objByName(vd->type.name);
            if(target) scope.usingMemberType[vd->name] = {target->objectClass, target};
            else if(auto* cd = languageService.findClass(vd->type.name))
                scope.usingMemberType[vd->name] = {cd, nullptr};
        }
    };
    istringstream ds(docText);
    string dl;
    while(getline(ds, dl)){
        size_t p = dl.find_first_not_of(" \t");
        if(p == string::npos || dl.compare(p, 7, "#using ") != 0) continue;
        size_t s = p + 7;
        while(s < dl.size() && (dl[s] == ' ' || dl[s] == '\t')) s++;
        size_t e = s;
        while(e < dl.size() && (isalnum((unsigned char)dl[e]) || dl[e] == '_' || dl[e] == '.')) e++;
        if(e <= s) continue;
        string path = dl.substr(s, e - s);
        transform(path.begin(), path.end(), path.begin(), ::tolower);
        if(objectDef* ns = walkObjectPath(path)) {
            for(typeMember* m : ns->members) addMember(m);
        } else if(path.find('.') == string::npos) {
            if(auto* cls = languageService.findClass(path))
                for(typeMember* m : cls->members) addMember(m);
        }
    }
}

    // Resolve a dotted-access receiver name to a transient block-style lookup context.
    // Returns a pair (classDef*, objectDef*) — the caller uses findInBlock via a temp range.
int LspServer::lookupDotted(const SemanticScope& scope, const string& receiverLower, int lineNum,
                            const string& member) {
    // A `#using`-imported member used as a receiver (e.g. `style.italics()`).
    if(auto ut = scope.usingMemberType.find(receiverLower); ut != scope.usingMemberType.end()) {
        InstanceBlockRange tmp{0,0,ut->second.first,ut->second.second};
        if(int r = findInBlock(&tmp, member)) return r;
    }
    if(receiverLower == "self") {
        return findInBlock(blockForLine(scope, lineNum), member);
    }
    // Class or enum lookup by name (e.g. bgl.world.instances)
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
            auto* cd = languageService.findClass(vd->type.name);
            if(cd) { InstanceBlockRange tmp{0,0,cd,nullptr}; return findInBlock(&tmp, member); }
        }
    }
    return 0;
}

// Token type for a bare `#using`-imported member name, or -1 when the name is not one.
int LspServer::usingMemberKind(const SemanticScope& scope, const string& lowerName, int /*lineNum*/) {
    auto it = scope.usingMemberTok.find(lowerName);
    return it == scope.usingMemberTok.end() ? -1 : it->second;
}

    // Scan an interpolated-string body from cursor `i`, with the current literal segment beginning
    // at `segStart`. Emits literal runs as stString and carves each `{expr}` slot as code. Used for
    // BOTH the opening `$"…"` line and any continuation lines of a multi-line interpolated string, so
    // embedded `{expr}` is colored the same on every line. Advances `i`; returns true iff the closing
    // '"' is on this line (false means the string carries to the next line).
bool LspServer::scanInterpString(const SemanticScope& scope,
                                 const std::function<void(int,int,int,int)>& emit,
                                 const string& lineText, size_t& i, int lineNum, size_t segStart) {
    while(i < lineText.size()) {
        char ic = lineText[i];
        if(ic == '\\' && i + 1 < lineText.size()) { i += 2; continue; }
        if(ic == '{') {
            if(i > segStart) emit(lineNum, (int)segStart, (int)(i - segStart), stString);
            i++;                // past {
            int exprDepth = 1;
            while(i < lineText.size() && exprDepth > 0) {
                char ec = lineText[i];
                if(isspace((unsigned char)ec)) { i++; continue; }
                if(ec == '{') { exprDepth++; i++; continue; }
                if(ec == '}') { exprDepth--; i++; continue; }
                if(ec == '"') {
                    size_t ss = i++;
                    while(i < lineText.size() && lineText[i] != '"') {
                        if(lineText[i] == '\\' && i + 1 < lineText.size()) i++;
                        i++;
                    }
                    if(i < lineText.size()) i++;
                    emit(lineNum, (int)ss, (int)(i - ss), stString);
                    continue;
                }
                if(ec == '\'') {
                    size_t ss = i++;
                    while(i < lineText.size() && lineText[i] != '\'') {
                        if(lineText[i] == '\\') i++;
                        i++;
                    }
                    if(i < lineText.size()) i++;
                    emit(lineNum, (int)ss, (int)(i - ss), stString);
                    continue;
                }
                if(isalpha((unsigned char)ec) || ec == '_') {
                    size_t ss = i;
                    while(i < lineText.size() && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i++;
                    string word = lineText.substr(ss, i - ss);
                    string lowerWord = word;
                    transform(lowerWord.begin(), lowerWord.end(), lowerWord.begin(), ::tolower);
                    if(ss > 0 && lineText[ss-1] == '.') {
                        int rEnd = (int)ss - 1;
                        int rStart = rEnd - 1;
                        while(rStart >= 0 && (isalnum((unsigned char)lineText[rStart]) || lineText[rStart] == '_')) rStart--;
                        rStart++;
                        if(rStart < rEnd) {
                            string recvLower = lineText.substr(rStart, rEnd - rStart);
                            transform(recvLower.begin(), recvLower.end(), recvLower.begin(), ::tolower);
                            int kind = lookupDotted(scope, recvLower, lineNum, lowerWord);
                            if(kind == 2) { emit(lineNum, (int)ss, (int)(i - ss), stMethod);   continue; }
                            if(kind == 1) { emit(lineNum, (int)ss, (int)(i - ss), stProperty); continue; }
                        }
                    }
                    if(const InstanceBlockRange* blk = blockForLine(scope, lineNum)) {
                        int kind = findInBlock(blk, lowerWord);
                        if(kind == 2) { emit(lineNum, (int)ss, (int)(i - ss), stMethod);   continue; }
                        if(kind == 1) { emit(lineNum, (int)ss, (int)(i - ss), stProperty); continue; }
                    }
                    int usingKind = usingMemberKind(scope, lowerWord, lineNum);
                    if(usingKind >= 0) { emit(lineNum, (int)ss, (int)(i - ss), usingKind); continue; }
                    int tokenType = classifyWord(word);
                    if(tokenType >= 0) emit(lineNum, (int)ss, (int)(i - ss), tokenType);
                    continue;
                }
                if(isdigit((unsigned char)ec)) {
                    size_t ss = i;
                    if(i + 1 < lineText.size() && lineText[i] == '0' && lineText[i+1] == 'x') {
                        i += 2;
                        while(i < lineText.size() && isxdigit((unsigned char)lineText[i])) i++;
                    } else {
                        while(i < lineText.size() && isdigit((unsigned char)lineText[i])) i++;
                    }
                    if(ss == 0 || !(isalnum((unsigned char)lineText[ss-1]) || lineText[ss-1] == '_'))
                        emit(lineNum, (int)ss, (int)(i - ss), stNumber);
                    continue;
                }
                if(ec == '.' && i + 1 < lineText.size() && isalpha((unsigned char)lineText[i+1])
                   && (i == 0 || !(isalnum((unsigned char)lineText[i-1]) || lineText[i-1] == '_'))) {
                    size_t ss = i++;
                    if(i < lineText.size() && lineText[i] == '.') i++;
                    while(i < lineText.size() && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i++;
                    emit(lineNum, (int)ss, (int)(i - ss), stString);
                    continue;
                }
                i++;            // unrecognized inside expression slot
            }
            segStart = i;       // resume literal segment after closing }
            continue;
        }
        if(ic == '"') {
            emit(lineNum, (int)segStart, (int)(i - segStart + 1), stString);
            i++;
            return true;
        }
        i++;
    }
    if(i > segStart) emit(lineNum, (int)segStart, (int)(i - segStart), stString);
    return false;
}

bool LspServer::isInStringOrComment(const string& uri, int line, int col) {
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return false;
    const string& docText = docIt->second;

    // Same carryover state as handleSemanticTokensFull's tokenizer, but instead of emitting
    // full token classification we only record string/char/comment spans on the target line
    // and check whether `col` falls inside one. Reuses scanInterpString (with an empty scope —
    // {expr} identifier resolution doesn't matter for this query, only span boundaries do) so
    // interpolated-string literal segments and comment/string carveouts stay in lockstep with
    // the real tokenizer.
    struct Span { int start, end; };
    vector<Span> literalSpans;
    SemanticScope emptyScope;
    std::function<void(int,int,int,int)> record = [&](int ln, int start, int len, int tokenType) {
        if(ln != line || (tokenType != stString && tokenType != stComment)) return;
        literalSpans.push_back({start, start + len});
    };

    istringstream stream(docText);
    string lineText;
    int lineNum = 0;
    bool inBlockComment = false;
    bool inString = false;
    bool inInterpolatedString = false;

    while(getline(stream, lineText)) {
        bool isTargetLine = (lineNum == line);
        size_t i = 0;

        if(inBlockComment) {
            size_t close = lineText.find("*/");
            int len = (close == string::npos) ? (int)lineText.size() : (int)close + 2;
            if(len > 0) record(lineNum, 0, len, stComment);
            if(close == string::npos) {
                if(isTargetLine) break;
                lineNum++; continue;
            }
            inBlockComment = false;
            i = close + 2;
        } else if(inInterpolatedString) {
            size_t j = 0;
            bool closed = scanInterpString(emptyScope, record, lineText, j, lineNum, 0);
            if(!closed) {
                if(isTargetLine) break;
                lineNum++; continue;
            }
            inInterpolatedString = false;
            i = j;
        } else if(inString) {
            size_t j = 0;
            while(j < lineText.size() && lineText[j] != '"') {
                if(lineText[j] == '\\' && j + 1 < lineText.size()) j++;
                j++;
            }
            int len = (j < lineText.size()) ? (int)j + 1 : (int)lineText.size();
            if(len > 0) record(lineNum, 0, len, stString);
            if(j >= lineText.size()) {
                if(isTargetLine) break;
                lineNum++; continue;
            }
            inString = false;
            i = j + 1;
        }

        while(i < lineText.size()) {
            char c = lineText[i];
            if(isspace((unsigned char)c)) { i++; continue; }

            if(c == '/' && i + 1 < lineText.size() && lineText[i+1] == '/') {
                record(lineNum, (int)i, (int)(lineText.size() - i), stComment);
                break;
            }
            if(c == '/' && i + 1 < lineText.size() && lineText[i+1] == '*') {
                size_t start = i;
                size_t close = lineText.find("*/", i + 2);
                if(close == string::npos) {
                    record(lineNum, (int)start, (int)(lineText.size() - start), stComment);
                    inBlockComment = true;
                    break;
                }
                record(lineNum, (int)start, (int)(close + 2 - start), stComment);
                i = close + 2;
                continue;
            }
            if(c == '$' && i + 1 < lineText.size() && lineText[i+1] == '"') {
                size_t segStart = i;
                i += 2;
                bool closed = scanInterpString(emptyScope, record, lineText, i, lineNum, segStart);
                if(!closed) { inInterpolatedString = true; break; }
                continue;
            }
            if(c == '"') {
                size_t start = i;
                i++;
                while(i < lineText.size() && lineText[i] != '"') {
                    if(lineText[i] == '\\' && i + 1 < lineText.size()) i++;
                    i++;
                }
                if(i < lineText.size()) i++;
                else inString = true;
                record(lineNum, (int)start, (int)(i - start), stString);
                continue;
            }
            if(c == '\'') {
                size_t start = i;
                i++;
                while(i < lineText.size() && lineText[i] != '\'') {
                    if(lineText[i] == '\\' && i + 1 < lineText.size()) i++;
                    i++;
                }
                if(i < lineText.size()) i++;
                record(lineNum, (int)start, (int)(i - start), stString);
                continue;
            }
            if(isalnum((unsigned char)c) || c == '_') {
                i++;
                while(i < lineText.size() && (isalnum((unsigned char)lineText[i]) || lineText[i] == '_')) i++;
                continue;
            }
            i++;
        }
        lineNum++;
        if(isTargetLine) break;
    }

    // A completion position is a CURSOR between characters, not a character index. A span
    // that runs to end-of-line (continuation line of a multi-line "..."/$"...", an unclosed
    // /*, or a // comment) must suppress the cursor sitting AT that end-of-line too — the
    // author has just typed "...lamp." and the caret is past the last character, still inside
    // the literal. A span that CLOSES mid-line (a complete "...", 'c', or a /*...*/ that ends
    // on this line) must NOT suppress the cursor just past its closing character — that's real
    // code (e.g. `print("done")‸;`). `lineText` still holds the target line's content here
    // (the scan loop only breaks within the target line's own iteration), so a span's end
    // coinciding with the line's length is exactly the "runs to end-of-line" case.
    int lineLen = (int)lineText.size();
    for(auto& sp : literalSpans) {
        bool touchesEOL = (sp.end == lineLen);
        if(touchesEOL) { if(col >= sp.start && col <= sp.end) return true; }
        else           { if(col >= sp.start && col <  sp.end) return true; }
    }
    return false;
}

json LspServer::handleSemanticTokensFull(const json& params) {
    string uri = params["textDocument"]["uri"];
    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return {{"data", json::array()}};
    ensureParsedForRequest(uri);

    // .inf-mode: don't classify the I6 host code. Beguile-aware highlighting inside #bgl{}
    // regions is Tier-2 work (TextMate injection); for now we hand off to the I6 highlighter.
    auto modeIt = documentModes.find(uri);
    if(modeIt != documentModes.end() && modeIt->second == DocMode::Inf)
        return {{"data", json::array()}};

    // Semantic tokens are encoded as delta-encoded quintuplets:
    // [deltaLine, deltaStartChar, length, tokenType, tokenModifiers]
    vector<int> data;
    int prevLine = 0, prevChar = 0;

    // The current line being tokenized. Declared here (ahead of `emit`) so `emit` can convert its
    // byte offsets to UTF-16 columns against it — LSP positions are UTF-16 code units, but the scanner
    // works in bytes, so without this tokens misalign on lines containing non-ASCII text (e.g. the
    // curly “smart quotes” that fill prose strings). The main tokenizer loop reads into this.
    string lineText;
    // Count UTF-16 code units in s[0, byteEnd) — 1 per BMP char, 2 per astral char (surrogate pair).
    auto byteToU16 = [](const string& s, int byteEnd) -> int {
        int units = 0, k = 0, n = (int)s.size();
        if(byteEnd > n) byteEnd = n;
        while(k < byteEnd) {
            unsigned char ch = (unsigned char)s[k];
            if(ch < 0x80)      { units += 1; k += 1; }
            else if(ch < 0xE0) { units += 1; k += 2; }
            else if(ch < 0xF0) { units += 1; k += 3; }
            else               { units += 2; k += 4; }
        }
        return units;
    };
    // Helper: emit a token at (lineNum, byte start) with given byte length and type.
    std::function<void(int,int,int,int)> emit = [&](int lineNum, int start, int len, int tokenType) {
        int u16start = byteToU16(lineText, start);
        int u16len   = byteToU16(lineText, start + len) - u16start;
        int deltaLine = lineNum - prevLine;
        int deltaChar = (deltaLine == 0) ? u16start - prevChar : u16start;
        data.insert(data.end(), {deltaLine, deltaChar, u16len, tokenType, 0});
        prevLine = lineNum; prevChar = u16start;
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
    if(auto* schema = languageService.findClass("beguilersettingstype"))
        for(typeMember* m : schema->members) bsMemberNames.insert(m->name);

    // Document-derived lookup state, built once here and read by the classifying phases below.
    SemanticScope scope;
    buildInstanceBlockIndex(uri, docIt->second, scope);
    collectUsingNamespaceMembers(docIt->second, scope);

    istringstream stream(docIt->second);
    lineText.clear();              // (declared earlier, ahead of `emit`, for UTF-16 column conversion)
    int lineNum = 0;
    bool inBlockComment = false;
    bool inString       = false;   // a "..." opened on a prior line; consume until closing '"'
    bool inInterpolatedString = false;   // a `$"…"` opened on a prior line; continuation lines carve {expr}
    bool inI6Block      = false;   // inside a multi-line #i6 { ... } raw Inform 6 region
    string i6BlockIndent;          // opener's leading whitespace; region ends at ^<indent>}

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

        // If a string literal opened on a prior line, consume until closing '"' (or EOL,
        // which keeps inString set for the next iteration). Without this carryover, the
        // line-by-line scanner re-enters code-mode on subsequent lines of a multi-line
        // string and emits semantic tokens (keyword/identifier colors) that overlay on
        // top of the TextMate string scope.
        if(inInterpolatedString) {
            // Continuation of a multi-line `$"…"`: carve `{expr}` slots as code, same as the opening
            // line, instead of flat whole-line stString (which swallowed embedded interpolation code).
            size_t j = 0;
            bool closed = scanInterpString(scope, emit, lineText, j, lineNum, 0);
            if(!closed) { lineNum++; continue; }   // string still open — whole line consumed
            inInterpolatedString = false;
            i = j;                                 // resume code scanning after the closing '"'
        } else if(inString) {
            size_t j = 0;
            while(j < lineText.size() && lineText[j] != '"') {
                if(lineText[j] == '\\' && j + 1 < lineText.size()) j++;
                j++;
            }
            int len = (j < lineText.size()) ? (int)j + 1 : (int)lineText.size();
            if(len > 0) emit(lineNum, 0, len, stString);
            if(j >= lineText.size()) { lineNum++; continue; }
            inString = false;
            i = j + 1;
        }

        // ── Raw Inform 6 regions (#i6) ───────────────────────────────────────
        // #i6 directives are raw Inform 6: a single-line `#i6 <code>` runs to end of
        // line, and a block `#i6 { ... }` runs to the closing } at the opener's
        // indentation. Inform 6 uses `!` for comments, not `//`, so the Beguile
        // tokenizer must not emit stComment/stString/keyword tokens inside these
        // regions — that green comment overlay (from a stray `//` in raw I6) was the
        // reported bug. Mirrors the TextMate i6-line / i6-block rules.
        if(inI6Block) {
            size_t nb = lineText.find_first_not_of(" \t");
            if(nb != string::npos && nb == i6BlockIndent.size() && lineText[nb] == '}')
                inI6Block = false;     // closing brace at the opener's indent — region ends
            lineNum++; continue;       // raw I6 line: emit nothing; TextMate/I6 colors it
        }
        if(i == 0) {                   // opener must be the first token on the line
            size_t ws = lineText.find_first_not_of(" \t");
            if(ws != string::npos && lineText.size() - ws >= 3 &&
               strncasecmp(lineText.c_str() + ws, "#i6", 3) == 0 &&
               (ws + 3 == lineText.size() || isspace((unsigned char)lineText[ws + 3]) ||
                lineText[ws + 3] == '{')) {   // reject #includeI6 / #i6foo
                size_t p = lineText.find_first_not_of(" \t", ws + 3);
                if(p != string::npos && lineText[p] == '{') {
                    // Block form: find the matching close brace on this line.
                    int depth = 0; size_t closePos = string::npos;
                    for(size_t q = p; q < lineText.size(); q++) {
                        if(lineText[q] == '{') depth++;
                        else if(lineText[q] == '}') { if(--depth == 0) { closePos = q; break; } }
                    }
                    if(closePos == string::npos) {
                        // Multi-line block: raw region until the indent-matched '}'.
                        inI6Block = true;
                        i6BlockIndent = lineText.substr(0, ws);
                        lineNum++; continue;
                    }
                    // Single-line block: raw inside braces, resume Beguile after '}'
                    // (a trailing `// comment` there is a genuine Beguile comment).
                    i = closePos + 1;
                } else {
                    // Single-line directive `#i6 <code>` — raw Inform 6 to end of line.
                    lineNum++; continue;
                }
            }
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

            // Interpolated string literal: $"text {expr} text". The literal segments
            // emit as stString, and the expression slots inside {...} are scanned as
            // code (including nested "..." literals which fall through to the regular
            // string handler). Without this branch the scanner would treat the '$' as
            // an unrecognized char, then the following '"' would open a regular string
            // that closes at the FIRST inner '"' — leaving the scanner walking through
            // inner-string contents as code and tokenizing English words like "to" as
            // keywords. Mirrors the TS-side stringRanges in beguilex/semanticTokens.ts.
            if(c == '$' && i + 1 < lineText.size() && lineText[i+1] == '"') {
                size_t segStart = i;       // start of current stString segment ($" included)
                i += 2;                     // past $"
                bool closed = scanInterpString(scope, emit, lineText, i, lineNum, segStart);
                if(!closed) inInterpolatedString = true;   // carries to next line (continuation carves {expr})
                continue;
            }

            // String literals — may span multiple lines. When the closing '"' isn't on
            // this line, set inString so the next iteration's carryover block (above)
            // consumes string content from column 0 until the closing quote.
            if(c == '"') {
                size_t start = i;
                i++;  // skip opening quote
                while(i < lineText.size() && lineText[i] != '"') {
                    if(lineText[i] == '\\' && i + 1 < lineText.size()) i++;  // skip escape
                    i++;
                }
                if(i < lineText.size()) {
                    i++;  // skip closing quote
                } else {
                    inString = true;  // unterminated on this line — continue on next
                }
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
                size_t dirStart = i + 1;
                i++;
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                // For #include / #includeI6, the remainder of the line is a library path
                // (<sub/folder/name>) or quoted string, NOT code. Don't tokenize identifiers
                // in it — otherwise a path segment that happens to name a known symbol (e.g.
                // `orPrint` in `#include <orLibrary/orPrint>`) gets an stFunction token that
                // beats TextMate's uniform string color, splitting the path's coloring.
                // Mirrors the TS side skipping include lines entirely (semanticTokens.ts INCLUDE_RE).
                string directive = lineText.substr(dirStart, i - dirStart);
                transform(directive.begin(), directive.end(), directive.begin(), ::tolower);
                if(directive == "include" || directive == "includei6") break;
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
                        int kind = lookupDotted(scope, recvLower, lineNum, lowerWord);
                        if(kind == 2) { emit(lineNum, (int)start, (int)(i - start), stMethod);   continue; }
                        if(kind == 1) { emit(lineNum, (int)start, (int)(i - start), stProperty); continue; }
                    }
                }

                // Bare identifier inside an instance/class body: resolve against the enclosing
                // block's member scope (objectDef own members first, then class hierarchy).
                // Covers Gap 1 (property initializers like `short_name = ...`) and Gap 2
                // (method declarations like `bool before() { }` that live on the objectDef).
                if(const InstanceBlockRange* blk = blockForLine(scope, lineNum)) {
                    int kind = findInBlock(blk, lowerWord);
                    if(kind == 2) { emit(lineNum, (int)start, (int)(i - start), stMethod);   continue; }
                    if(kind == 1) { emit(lineNum, (int)start, (int)(i - start), stProperty); continue; }
                }

                // A bare identifier that names a `#using`-imported namespace member (e.g. `style`
                // from `#using bgl.ui`) — color it as that member rather than leaving it default.
                int usingKind = usingMemberKind(scope, lowerWord, lineNum);
                if(usingKind >= 0) { emit(lineNum, (int)start, (int)(i - start), usingKind); continue; }

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
            string container = od->objectClass ? od->objectClass->name : "_bglobject";
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
