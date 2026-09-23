#pragma once
// lspServer.h — Language Server Protocol implementation for Beguile
// Standalone module: references bglLanguageService and bglParser but does not modify them.
// To remove: delete lspServer.h/cpp, json.hpp, and the --lsp check in beguiler.cpp.

#include <string>
#include <map>
#include <vector>
#include <utility>
#include <functional>
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
    std::string docComment;                 // user-authored doc-comment, if any (rendered in hover)
    sourceLocation declSrc;                 // declaration location (if known)
};

class LspServer {
public:
    LspServer();
    void run();  // main loop — reads from stdin, writes to stdout

    // Include search paths from the CLI (-includepaths=, passed by the extension from the
    // beguiler.includePaths setting). LSP mode skips parseArgs(), and parseDocument() resets
    // beguilerSettings before every parse, so these are captured once (in the --lsp dispatch)
    // and re-seeded into beguilerSettings.includePaths on each parse — needed for `#include "…"`
    // resolution, diagnostics, and quoted-include completion. Set before run().
    std::vector<std::string> cliIncludePaths;

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
    // Custom `beguile/setEntryPoint` notification from the extension: designates the project's
    // entry-point .bgl (the file with `#beguilerSettings`, the one F5/Debug compiles). When set,
    // an opened *included* file is parsed in the entry point's whole-program context (so `#if`
    // gating, symbols, hover, and completion resolve as they do in a real build) instead of being
    // parsed standalone. Empty path clears the designation.
    void handleSetEntryPoint(const json& params);
    // Custom `beguile/setConfig` notification from the extension: pushes editor settings that the
    // server honors (currently just `syntaxHints`, toggling the keyword syntax-snippet completions).
    void handleSetConfig(const json& params);
    void handleDidOpen(const json& params);
    void handleDidChange(const json& params);
    void handleDidClose(const json& params);
    json handleHover(const json& params);
    // handleHover's cases, called in this order. hoverVerbExtendKeyword follows the completion
    // convention (sets `handled` and returns the hover, else its return value is ignored); the
    // three below return the signature line they resolved, or empty when they don't own the cursor.
    // Hover case: a verb-extend member keyword (`synonyms` / `priority` / `grammar`).
    json hoverVerbExtendKeyword(const std::string& word, const std::string& docText, int line, int col,
                                bool& handled);
    // Hover case: a #beguilerSettings property name or enum value.
    std::string hoverBeguilerSettingsMember(const std::string& uri, int line, int col,
                                            const std::string& lower, const std::string& ownerName);
    // Hover case: `owner.member` — resolve the owner chain, then the member on it.
    std::string hoverDottedMember(const std::string& uri, int line, const std::string& lower,
                                  const std::string& ownerName);
    // Hover case: a bare identifier — locals, params, class members, globals, verbs, enum values.
    std::string hoverIdentifier(const std::string& uri, int line, const std::string& word,
                                const std::string& lower, const std::string& ownerName,
                                std::string& docComment);
    json handleCompletion(const json& params);
    // handleCompletion's cases, one method each, called in phase order. Convention: each sets
    // `handled` true and returns that case's completion result when the cursor belongs to it;
    // otherwise it leaves `handled` false and its return value is ignored by the caller.
    // Completion case: `#include "..."` project-file paths.
    json completeQuotedInclude(const std::string& uri, int line, int col, const std::string& lineText, bool& handled);
    // Completion case: `#includeI6 "..."` Inform 6 library files.
    json completeI6Include(const std::string& uri, int line, int col, const std::string& lineText, bool& handled);
    // Completion case: `includePaths = "..."` directory segments inside #beguilerSettings.
    json completeIncludePathsDir(const std::string& uri, int line, int col, const std::string& lineText,
                                 bool insideBsBlock, bool& handled);
    // Completion case: a call argument whose parameter type is an enum -> that enum's members.
    json completeEnumArgument(const std::string& uri, int line, int col, const std::string& lineText,
                              const std::string& docText, bool& handled);
    // Completion case: `#include <...>` beguiLib library names.
    json completeAngleInclude(int line, int col, const std::string& lineText, bool& handled);
    // Completion case: class-header inheritance position (`class Foo : |`, `alias class Foo for |`).
    json completeClassHeader(int col, const std::string& lineText, bool& handled);
    // Completion case: dotted access (`foo.|`) - members of foo's type, or an enum type's values.
    json completeDottedMember(const std::string& uri, int line, int col, const std::string& lineText, bool& handled);
    // Completion case: inside a grammar pattern literal -> grammarToken enum members.
    json completeGrammarPattern(int line, int col, const std::string& docText, bool& handled);
    // Completion case: an `attributeList`-typed member initializer -> every `attribute` instance.
    json completeAttributeListLiteral(int line, int col, const std::string& docText, bool& handled);
    // Completion case: `extend <verb|array> { | }` member position -> the members an extend may add.
    json completeExtendBody(int line, int col, const std::string& docText, bool& handled);
    // Completion case: first token of a class/object member declaration -> the valid member modifiers.
    json completeMemberModifiers(int line, int col, const std::string& docText, bool insideBsBlock, bool& handled);
    // Completion case: inside `#beguilerSettings { ... }` -> enum RHS values, else the property names.
    json completeBeguilerSettingsBlock(int col, const std::string& lineText, bool insideBsBlock, bool& handled);
    // Completion case: a bare prefix of `enum` -> the keyword syntax snippet + doc popup.
    json completeKeywordSnippet(int col, const std::string& lineText, bool& handled);
    // Completion case: bare-identifier position -> members imported by active `#using` directives.
    json completeUsingImports(int line, const std::string& docText, bool& handled);
    json handleDefinition(const json& params);
    json handleDocumentSymbol(const json& params);
    json handleSignatureHelp(const json& params);
    json handleReferences(const json& params);
    json handleRename(const json& params);
    json handleSemanticTokensFull(const json& params);
    // handleSemanticTokensFull's phases. InstanceBlockRange indexes one class/instance body;
    // SemanticScope is the document-derived lookup state the classifying phases read.
    struct InstanceBlockRange { int startLine0; int endLine0Exclusive; classDef* cls; objectDef* obj; };
    struct SemanticScope {
        std::vector<InstanceBlockRange> instanceBlocks;
        std::map<std::string, int> usingMemberTok;
        std::map<std::string, std::pair<classDef*, objectDef*>> usingMemberType;
    };
    // Index every class/instance body declared in `txt` into scope.instanceBlocks.
    void buildInstanceBlockIndex(const std::string& uri, const std::string& txt, SemanticScope& scope);
    // The innermost indexed block containing lineNum, or nullptr.
    const InstanceBlockRange* blockForLine(const SemanticScope& scope, int lineNum);
    // Kind of `name` in a block's member scope: 1 = variable/property, 2 = function/method, 0 = absent.
    int findInBlock(const InstanceBlockRange* b, const std::string& name);
    // Fill scope's `#using`-imported member maps from the directives in `docText`.
    void collectUsingNamespaceMembers(const std::string& docText, SemanticScope& scope);
    // Kind of `member` on the dotted receiver `receiverLower`, in findInBlock's encoding.
    int lookupDotted(const SemanticScope& scope, const std::string& receiverLower, int lineNum,
                     const std::string& member);
    // Token type for a bare `#using`-imported member name, or -1 when the name is not one.
    int usingMemberKind(const SemanticScope& scope, const std::string& lowerName, int lineNum);
    // Scan an interpolated string from `i`, emitting literal runs and carving `{expr}` slots as
    // code through `emit`. Advances `i`; returns true iff the closing '"' is on this line.
    bool scanInterpString(const SemanticScope& scope,
                          const std::function<void(int, int, int, int)>& emit,
                          const std::string& lineText, size_t& i, int lineNum, size_t segStart);
    // True when (line, col) falls inside a string literal, char literal, or comment — scanned
    // from the top of the document so multi-line "..."/$"..."/ /* */ carry over correctly.
    // A position inside an interpolated string's {expr} slot is real code and returns false.
    // Used to gate completion providers that would otherwise fire while typing prose.
    bool isInStringOrComment(const std::string& uri, int line, int col);
    json handleWorkspaceSymbol(const json& params);

    // Semantic token classification
    int classifyWord(const std::string& word) const;

    // Position-to-symbol resolution helpers (shared across hover, definition, completion,
    // signatureHelp, references, rename). These walk enclosing scopes: locals → parameters →
    // class members → globals.
    functionDef* findEnclosingFunction(const std::string& uri, int cursorLine);
    LspSymbolRef resolveSymbol(const std::string& uri, int cursorLine, const std::string& loweredName);
    // resolveSymbol's tiers, tried in this order. Each fills `out` and returns true when it
    // resolves the name; a false return leaves `out` for the next tier to try.
    // Tier: a class member whose declaration sits on the cursor line.
    bool resolveTierSourceLineClassMember(const std::string& curFile, int cursorLine1Based,
                                          const std::string& loweredName, LspSymbolRef& out);
    // Tier: an object-instance member whose declaration sits on the cursor line.
    bool resolveTierSourceLineInstanceMember(const std::string& curFile, int cursorLine1Based,
                                             const std::string& loweredName, LspSymbolRef& out);
    // Tier: the enclosing function's locals, parameters and containing class members. Sets
    // out.enclosingFunc even when it resolves nothing.
    bool resolveTierFunctionScope(const std::string& uri, int cursorLine,
                                  const std::string& loweredName, LspSymbolRef& out);
    // Tier: file-scope names — classes, enums, globals.
    bool resolveTierGlobal(const std::string& loweredName, LspSymbolRef& out);

    // Resolve every active `#using <path>` directive above `line` to the namespace object
    // it names (if any). Used by completion to surface #using-imported members (e.g. the
    // print-rule `img` brought in by `#using bgl.printRules`). Empty when none resolve.
    std::vector<objectDef*> activeUsingNamespaces(const std::string& docText, int line);

    // Resolve a call site to the parameter list of its (first) matching callee. `funcName` is
    // the called identifier; `objName` is the receiver for a `recv.method(` member call (empty
    // for a bare call). Checks, in order: member methods (when objName is set), global functions,
    // then #using-imported namespace members. Returns the first candidate's params, or empty when
    // nothing resolves. Backs enum-argument completion (offer an enum param's members).
    std::vector<paramDef*> resolveCalleeParams(const std::string& uri, int line,
                                               const std::string& funcName,
                                               const std::string& objName,
                                               const std::string& docText);

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

    // Canonical filesystem path of the designated entry-point .bgl (empty = none). Set via the
    // `beguile/setEntryPoint` notification. Drives whole-program context parsing for included files.
    std::string entryPointPath;

    // Editor-pushed settings (via `beguile/setConfig`). `syntaxHints` toggles the keyword
    // syntax-snippet completions (e.g. the `enum` declaration popup). Default on.
    bool syntaxHintsEnabled = true;

    // Parse `uri` in the entry point's context: root the parse at the entry point (which #includes
    // `uri`), overlaying every open buffer so unsaved edits in any file are honored, and attribute
    // diagnostics/inactive-regions/symbols to `uri`. Falls back to a standalone parse when no entry
    // point is set, when `uri` IS the entry point, or when the entry point doesn't include `uri`.
    // Returns true if the context parse covered `uri` (so the caller skips the standalone parse).
    bool parseDocumentInEntryContext(const std::string& uri);

    // Document store
    std::map<std::string, std::string> openDocuments;  // uri → text content
    std::map<std::string, std::vector<std::string>> documentDiagnostics;  // uri → error messages
    std::map<std::string, std::string> documentParsePaths;  // uri → canonical path used during last parse

    // URI of the document whose parse currently populates the shared parser/languageService state.
    // Feature requests (completion/hover/signatureHelp) resolve against that shared state, so a
    // request for a *different* document must reparse its own URI first — otherwise editing an
    // included library file (e.g. __beguileCore.bgl, which double-registers symbols when parsed
    // standalone) corrupts the state that completion in the dependent file reads. See
    // ensureParsedForRequest().
    std::string lastParsedUri;

    // If `uri` is open but not the last-parsed document, reparse it so the shared symbol table
    // reflects the file the request targets. No-op when it already matches.
    void ensureParsedForRequest(const std::string& uri);

    // .inf-mode polyglot support: when an opened file ends in `.inf`, the document is treated as
    // I6 source with `#bgl{...}` islands. The LSP only responds to feature requests inside those
    // islands; outside them, requests return null/empty so other providers (or VS Code's built-in
    // word-based fallback) can handle them.
    enum class DocMode { Bgl, Inf };
    struct BglRegion { int startOffset; int endOffset; };  // half-open: [start, end)
    std::map<std::string, DocMode> documentModes;
    std::map<std::string, std::vector<BglRegion>> documentBglRegions;
    // Scan the document text for #bgl{...} (multi-line) and #bgl ...; (single-line) regions.
    // Brace-balanced like the lexer's getRawTextUntilCloseOrBgl, but operates on a string.
    std::vector<BglRegion> findBglRegions(const std::string& docText);
    // True when the cursor is inside a #bgl region in an .inf-mode document.
    // Returns true unconditionally for .bgl-mode documents (no gating needed).
    bool requestAllowedAt(const std::string& uri, int line, int col);
    // Convert (line, col) to an absolute character offset in the document text.
    int positionToOffset(const std::string& docText, int line, int col);

    // Helpers
    std::string uriToPath(const std::string& uri);
    std::string pathToUri(const std::string& path);
};
