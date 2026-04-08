#pragma once
// lspServer.h — Language Server Protocol implementation for Beguile
// Standalone module: references bglLanguageService and bglParser but does not modify them.
// To remove: delete lspServer.h/cpp, json.hpp, and the --lsp check in beguiler.cpp.

#include <string>
#include <map>
#include <vector>
#include "json.hpp"
#undef assert  // json.hpp includes <cassert>; undef to avoid conflict with token::assert()

using json = nlohmann::json;

class bglParser;
class bglLanguageService;

class LspServer {
public:
    LspServer();
    void run();  // main loop — reads from stdin, writes to stdout

private:
    bool initialized = false;
    bool shutdownRequested = false;

    // Message I/O
    json readMessage();
    void sendMessage(const json& msg);
    void sendResponse(const json& id, const json& result);
    void sendError(const json& id, int code, const std::string& message);
    void sendNotification(const std::string& method, const json& params);

    // Dispatch
    void handleMessage(const json& msg);

    // LSP methods
    json handleInitialize(const json& params);
    void handleShutdown(const json& id);
    void handleDidOpen(const json& params);
    void handleDidChange(const json& params);
    void handleDidClose(const json& params);
    json handleHover(const json& params);
    json handleCompletion(const json& params);
    json handleDefinition(const json& params);

    // Compilation
    void parseDocument(const std::string& uri);
    void publishDiagnostics(const std::string& uri);

    // Document store
    std::map<std::string, std::string> openDocuments;  // uri → text content
    std::map<std::string, std::vector<std::string>> documentDiagnostics;  // uri → error messages

    // Helpers
    std::string uriToPath(const std::string& uri);
    std::string pathToUri(const std::string& path);
};
