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
#include <sstream>
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
        {"definitionProvider", true}
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
    parseDocument(uri);
    publishDiagnostics(uri);
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

    // Reset the language service state for a fresh parse
    // Note: this is a full re-parse. The compiler's global state is rebuilt.
    // TODO: investigate incremental parsing for performance

    // Save cout/cerr and redirect to suppress compiler output
    streambuf* oldCout = cout.rdbuf();
    streambuf* oldCerr = cerr.rdbuf();
    ostringstream nullStream;
    // Don't redirect — we need cerr for LSP logging
    cout.rdbuf(nullStream.rdbuf());  // suppress "Beguiling file..." output

    vector<string> errors;
    try {
        // Re-initialize language service for a clean parse
        // TODO: this is heavy — optimize later
        parser.parseFile(path);
    } catch(exception& e) {
        errors.push_back(e.what());
    } catch(...) {
        errors.push_back("Unknown compilation error");
    }

    cout.rdbuf(oldCout);  // restore cout

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

    // Resolve the type
    string lower = word;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    string typeInfo;

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
