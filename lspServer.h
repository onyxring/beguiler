#pragma once
// lspServer.h — Language Server Protocol implementation for Beguile
// Standalone module: references bglLanguageService and bglParser but does not modify them.
// To remove: delete lspServer.h/cpp, json.hpp, and the --lsp check in beguiler.cpp.

#include <string>
#include <map>
#include <vector>
#include "json.hpp"
#undef assert  // json.hpp includes <cassert>; undef to avoid conflict with token::assert()
#include "typeDef.h"  // for functionDef, classDef, variableDeclaration, paramDef, sourceLocation

using json = nlohmann::json;

class bglParser;
class bglLanguageService;

// Scope-restricted symbol resolution result from findSymbolAtPosition()
struct LspSymbolRef {
    enum Kind { None, Local, Parameter, ClassMember, Global };
    Kind kind = None;
    functionDef* enclosingFunc = nullptr;  // set for Local/Parameter
    classDef*    enclosingClass = nullptr; // set for ClassMember (if applicable)
    std::string typeName;                   // resolved type name of the symbol
    std::string displayName;                // original-case name
    sourceLocation declSrc;                 // declaration location (if known)
};

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
    json handleDocumentSymbol(const json& params);
    json handleSignatureHelp(const json& params);
    json handleReferences(const json& params);
    json handleRename(const json& params);
    json handleSemanticTokensFull(const json& params);
    json handleWorkspaceSymbol(const json& params);

    // Semantic token classification
    int classifyWord(const std::string& word) const;

    // Position-to-symbol resolution helpers (shared across hover, definition, completion,
    // signatureHelp, references, rename). These walk enclosing scopes: locals → parameters →
    // class members → globals.
    functionDef* findEnclosingFunction(const std::string& uri, int cursorLine);
    LspSymbolRef resolveSymbol(const std::string& uri, int cursorLine, const std::string& loweredName);

    // Returns true if the cursor at (line, col) is inside an open '#beguilerSettings { ... }'
    // block. Used by completion, hover, and definition so identifiers in the block can be
    // resolved against the beguilerSettingsType schema even though the block isn't a real
    // object-body AST node.
    bool isInBeguilerSettingsBlock(const std::string& uri, int line, int col);

    // Compilation
    void parseDocument(const std::string& uri);
    void publishDiagnostics(const std::string& uri);
    // Send the beguile/inactiveRegions custom notification for a parsed document. Pulls ranges
    // from parser.inactiveRegions keyed by the canonical parse path, and always sends (empty
    // array is a valid "clear overlay" signal for the client).
    void publishInactiveRegions(const std::string& uri);
    void resetAndReparse(const std::string& uri);

    // Document store
    std::map<std::string, std::string> openDocuments;  // uri → text content
    std::map<std::string, std::vector<std::string>> documentDiagnostics;  // uri → error messages
    std::map<std::string, std::string> documentParsePaths;  // uri → canonical path used during last parse

    // Helpers
    std::string uriToPath(const std::string& uri);
    std::string pathToUri(const std::string& path);
};
