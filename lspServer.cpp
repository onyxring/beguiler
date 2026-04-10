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
            {"triggerCharacters", {"."}}
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
    parseDocument(uri);
    publishDiagnostics(uri);
}

void LspServer::handleDidChange(const json& params) {
    string uri = params["textDocument"]["uri"];
    // Full sync mode: contentChanges[0].text is the full document
    if(params.contains("contentChanges") && !params["contentChanges"].empty()) {
        openDocuments[uri] = params["contentChanges"][0]["text"];
    }
    resetAndReparse(uri);
}

void LspServer::handleDidClose(const json& params) {
    string uri = params["textDocument"]["uri"];
    openDocuments.erase(uri);
    // Clear diagnostics on close
    sendNotification("textDocument/publishDiagnostics", {
        {"uri", uri},
        {"diagnostics", json::array()}
    });
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

//=============================================================================
// Hover
//=============================================================================

json LspServer::handleHover(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

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

    // Check for dotted path: is there a '.' before the word?
    string ownerName;
    if(start > 0 && lineText[start - 1] == '.') {
        int oEnd = start - 1;
        int oStart = oEnd - 1;
        while(oStart >= 0 && (isalnum(lineText[oStart]) || lineText[oStart] == '_')) oStart--;
        oStart++;
        ownerName = lineText.substr(oStart, oEnd - oStart);
    }

    // Resolve the type
    string lower = word;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    string typeInfo;

    // If we have a dotted path (owner.member), resolve the member
    if(!ownerName.empty()) {
        string ownerLower = ownerName;
        transform(ownerLower.begin(), ownerLower.end(), ownerLower.begin(), ::tolower);
        classDef* cls = nullptr;

        // Resolve owner to a class
        typeDef& td = languageService.getType(ownerLower);
        if(auto* cd = dynamic_cast<classDef*>(&td)) cls = cd;
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
                            typeInfo = fd->returnType.name + " " + (fd->displayName.empty() ? fd->name : fd->displayName);
                            if(!fd->isValueEmitter) {
                                typeInfo += "(";
                                for(size_t i = 0; i < fd->params.size(); i++) {
                                    if(i > 0) typeInfo += ", ";
                                    typeInfo += fd->params[i]->type.name + " " + fd->params[i]->name;
                                }
                                typeInfo += ")";
                            }
                            if(fd->isEmitter) typeInfo = "emitter " + typeInfo;
                        } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                            typeInfo = vd->type.name + " " + (vd->displayName.empty() ? vd->name : vd->displayName);
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

        // Also check object instance members
        if(typeInfo.empty()) {
            for(typeDef* g : languageService.globals) {
                if(auto* od = dynamic_cast<objectDef*>(g)) {
                    if(od->name == ownerLower) {
                        for(typeMember* m : od->members) {
                            if(m->name == lower) {
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    typeInfo = fd->returnType.name + " " + fd->name + "(...)";
                                else if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                    typeInfo = vd->type.name + " " + vd->name;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    // Non-dotted: resolve as top-level identifier
    if(typeInfo.empty() && ownerName.empty()) {
        // Check classes
        typeDef& td = languageService.getType(lower);
        if(auto* cd = dynamic_cast<classDef*>(&td)) {
            typeInfo = "class " + (cd->displayName.empty() ? cd->name : cd->displayName);
            if(cd->isEmitterClass) typeInfo = "emitter " + typeInfo;
            if(cd->isExternal) typeInfo = "extern " + typeInfo;
        }
        // Check globals
        if(typeInfo.empty()) {
            for(typeDef* g : languageService.globals) {
                if(g->name == lower) {
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        typeInfo = vd->type.name + " " + (vd->displayName.empty() ? vd->name : vd->displayName);
                    else if(auto* fd = dynamic_cast<functionDef*>(g)) {
                        typeInfo = fd->returnType.name + " " + (fd->displayName.empty() ? fd->name : fd->displayName) + "(";
                        for(size_t i = 0; i < fd->params.size(); i++) {
                            if(i > 0) typeInfo += ", ";
                            typeInfo += fd->params[i]->type.name + " " + fd->params[i]->name;
                        }
                        typeInfo += ")";
                        if(fd->isEmitter) typeInfo = "emitter " + typeInfo;
                    }
                    else if(auto* od = dynamic_cast<objectDef*>(g))
                        typeInfo = (od->objectClass ? od->objectClass->name : "object") + " " +
                                   (od->displayName.empty() ? od->name : od->displayName);
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
// Completion (Phase 1: member completion after '.')
//=============================================================================

json LspServer::handleCompletion(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return json::array();

    // Get the text before the cursor on this line
    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    // Find the identifier before the '.'
    int dotPos = col - 1;
    if(dotPos < 0 || dotPos >= (int)lineText.size() || lineText[dotPos] != '.')
        return json::array();

    int start = dotPos - 1;
    while(start >= 0 && (isalnum(lineText[start]) || lineText[start] == '_')) start--;
    start++;
    string prefix = lineText.substr(start, dotPos - start);
    if(prefix.empty()) return json::array();

    string lower = prefix;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Resolve the type of the prefix
    classDef* cls = nullptr;

    // Check if it's a class/emitter namespace directly
    typeDef& td = languageService.getType(lower);
    if(auto* cd = dynamic_cast<classDef*>(&td)) cls = cd;

    // Check if it's an object — use its class
    if(!cls) {
        for(typeDef* g : languageService.globals)
            if(auto* od = dynamic_cast<objectDef*>(g))
                if(od->name == lower) { cls = od->objectClass; break; }
    }

    // Check if it's a variable — resolve its type
    if(!cls) {
        for(typeDef* g : languageService.globals)
            if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                if(vd->name == lower) {
                    cls = dynamic_cast<classDef*>(&languageService.getType(vd->type.name));
                    break;
                }
    }

    if(!cls) return json::array();

    // Collect all members (walk hierarchy)
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
                items.push_back({
                    {"label", label},
                    {"kind", kind},
                    {"detail", detail}
                });
            } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                items.push_back({
                    {"label", vd->displayName.empty() ? vd->name : vd->displayName},
                    {"kind", 6},  // Variable
                    {"detail", vd->type.name}
                });
            }
        }
        for(classDef* base : c->baseClasses) collectMembers(base);
    };
    collectMembers(cls);

    // Also collect instance members if prefix is an object
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            if(od->name == lower) {
                for(typeMember* m : od->members) {
                    if(auto* fd = dynamic_cast<functionDef*>(m)) {
                        string label = fd->displayName.empty() ? fd->name : fd->displayName;
                        if(!fd->isValueEmitter) label += "(...)";
                        items.push_back({{"label", label}, {"kind", 2}, {"detail", fd->returnType.name}});
                    } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)) {
                        items.push_back({{"label", vd->name}, {"kind", 6}, {"detail", vd->type.name}});
                    }
                }
                break;
            }

    return items;
}

//=============================================================================
// Definition (go-to-definition)
//=============================================================================

json LspServer::handleDefinition(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

    auto docIt = openDocuments.find(uri);
    if(docIt == openDocuments.end()) return nullptr;

    istringstream stream(docIt->second);
    string lineText;
    for(int i = 0; i <= line; i++) getline(stream, lineText);

    int start = col, end = col;
    while(start > 0 && (isalnum(lineText[start-1]) || lineText[start-1] == '_')) start--;
    while(end < (int)lineText.size() && (isalnum(lineText[end]) || lineText[end] == '_')) end++;
    string word = lineText.substr(start, end - start);
    if(word.empty()) return nullptr;

    string lower = word;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Search for the declaration source location
    sourceLocation src;

    // Classes
    typeDef& td = languageService.getType(lower);
    if(auto* cd = dynamic_cast<classDef*>(&td)) src = cd->src;

    // Globals
    if(src.file.empty()) {
        for(typeDef* g : languageService.globals) {
            if(g->name == lower) {
                if(auto* fd = dynamic_cast<functionDef*>(g)) src = fd->src;
                else if(auto* od = dynamic_cast<objectDef*>(g)) src = od->src;
                else if(auto* vd = dynamic_cast<variableDeclaration*>(g)) src = vd->src;
                break;
            }
        }
    }
    // Verbs
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
        // Member function — resolve the object/class type
        string objLower = objName;
        transform(objLower.begin(), objLower.end(), objLower.begin(), ::tolower);
        classDef* cls = nullptr;

        typeDef& td = languageService.getType(objLower);
        if(auto* cd = dynamic_cast<classDef*>(&td)) cls = cd;

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
        string label = fd->returnType.name + " " + (fd->displayName.empty() ? fd->name : fd->displayName) + "(";
        json parameters = json::array();
        int labelStart = (int)label.size();
        for(size_t i = 0; i < fd->params.size(); i++) {
            if(i > 0) { label += ", "; labelStart = (int)label.size(); }
            else labelStart = (int)label.size();
            string paramLabel = fd->params[i]->type.name + " " + fd->params[i]->name;
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
}

//=============================================================================
// References (find all references to a symbol)
//=============================================================================

json LspServer::handleReferences(const json& params) {
    string uri = params["textDocument"]["uri"];
    int line = params["position"]["line"].get<int>();
    int col = params["position"]["character"].get<int>();

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

    // Search all open documents for whole-word occurrences
    json locations = json::array();
    for(auto& [docUri, docText] : openDocuments) {
        istringstream docStream(docText);
        string docLine;
        int lineNum = 0;
        while(getline(docStream, docLine)) {
            size_t pos = 0;
            while(pos < docLine.size()) {
                size_t found = docLine.find(word, pos);
                if(found == string::npos) break;
                // Check whole-word boundaries (case-insensitive match)
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

    // Build workspace edit — replace all whole-word occurrences in all open documents
    json changes = json::object();
    for(auto& [docUri, docText] : openDocuments) {
        json edits = json::array();
        istringstream docStream(docText);
        string docLine;
        int lineNum = 0;
        while(getline(docStream, docLine)) {
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
static const set<string> bglModifiers = {
    "class", "object", "enum", "extern", "emitter", "static", "const",
    "replace", "extend", "verb", "attribute", "has", "hasnt", "void",
    "int", "bool", "char", "string", "var", "auto", "array"
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

    // Semantic tokens are encoded as delta-encoded quintuplets:
    // [deltaLine, deltaStartChar, length, tokenType, tokenModifiers]
    vector<int> data;
    int prevLine = 0, prevChar = 0;

    istringstream stream(docIt->second);
    string lineText;
    int lineNum = 0;

    while(getline(stream, lineText)) {
        size_t i = 0;
        while(i < lineText.size()) {
            char c = lineText[i];

            // Skip whitespace
            if(isspace(c)) { i++; continue; }

            // Comments: // to end of line
            if(c == '/' && i + 1 < lineText.size() && lineText[i+1] == '/') {
                int len = (int)lineText.size() - (int)i;
                int deltaLine = lineNum - prevLine;
                int deltaChar = (deltaLine == 0) ? (int)i - prevChar : (int)i;
                data.insert(data.end(), {deltaLine, deltaChar, len, stComment, 0});
                prevLine = lineNum; prevChar = (int)i;
                break;  // rest of line is comment
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
                int len = (int)(i - start);
                int deltaLine = lineNum - prevLine;
                int deltaChar = (deltaLine == 0) ? (int)start - prevChar : (int)start;
                data.insert(data.end(), {deltaLine, deltaChar, len, stString, 0});
                prevLine = lineNum; prevChar = (int)start;
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
                int len = (int)(i - start);
                int deltaLine = lineNum - prevLine;
                int deltaChar = (deltaLine == 0) ? (int)start - prevChar : (int)start;
                data.insert(data.end(), {deltaLine, deltaChar, len, stString, 0});
                prevLine = lineNum; prevChar = (int)start;
                continue;
            }

            // Directives: # followed by identifier
            if(c == '#') {
                size_t start = i;
                i++;
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                string dir = lineText.substr(start, i - start);
                if(bglDirectives.count(dir)) {
                    int len = (int)(i - start);
                    int deltaLine = lineNum - prevLine;
                    int deltaChar = (deltaLine == 0) ? (int)start - prevChar : (int)start;
                    data.insert(data.end(), {deltaLine, deltaChar, len, stMacro, 0});
                    prevLine = lineNum; prevChar = (int)start;
                }
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
                if(start == 0 || !(isalnum(lineText[start-1]) || lineText[start-1] == '_')) {
                    int len = (int)(i - start);
                    int deltaLine = lineNum - prevLine;
                    int deltaChar = (deltaLine == 0) ? (int)start - prevChar : (int)start;
                    data.insert(data.end(), {deltaLine, deltaChar, len, stNumber, 0});
                    prevLine = lineNum; prevChar = (int)start;
                }
                continue;
            }

            // Identifiers and keywords
            if(isalpha(c) || c == '_') {
                size_t start = i;
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                string word = lineText.substr(start, i - start);
                int tokenType = classifyWord(word);
                if(tokenType >= 0) {
                    int len = (int)(i - start);
                    int deltaLine = lineNum - prevLine;
                    int deltaChar = (deltaLine == 0) ? (int)start - prevChar : (int)start;
                    data.insert(data.end(), {deltaLine, deltaChar, len, tokenType, 0});
                    prevLine = lineNum; prevChar = (int)start;
                }
                continue;
            }

            // Dictionary words: .word or ..word (but NOT member access like ball.count)
            if(c == '.' && i + 1 < lineText.size() && isalpha(lineText[i+1])
               && (i == 0 || !(isalnum(lineText[i-1]) || lineText[i-1] == '_'))) {
                size_t start = i;
                i++;
                if(i < lineText.size() && lineText[i] == '.') i++;  // ..word
                while(i < lineText.size() && (isalnum(lineText[i]) || lineText[i] == '_')) i++;
                int len = (int)(i - start);
                int deltaLine = lineNum - prevLine;
                int deltaChar = (deltaLine == 0) ? (int)start - prevChar : (int)start;
                data.insert(data.end(), {deltaLine, deltaChar, len, stString, 0});
                prevLine = lineNum; prevChar = (int)start;
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
