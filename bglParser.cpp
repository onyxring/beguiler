#include <regex>
#include "platform.h"
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
// Shared helpers:
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

// Resolve `rel` under `base`, matching EVERY path component (intermediate directories
// and the final filename) case-insensitively. Each component prefers an exact match
// when one exists, then falls back to the first directory entry whose lowercased name
// matches. Returns the existing resolved path, or empty if any component has no match.
static filesystem::path resolveCaseInsensitive(const filesystem::path& base, const filesystem::path& rel){
    std::error_code ec;
    filesystem::path cur = base;
    for(const auto& comp : rel){
        string cs = comp.string();
        if(cs.empty() || cs == ".") continue;
        filesystem::path exact = cur / comp;
        if(filesystem::exists(exact, ec)){ cur = exact; continue; }
        // No exact match — scan `cur` for a case-insensitive component match.
        string want = cs;
        transform(want.begin(), want.end(), want.begin(), ::tolower);
        bool found = false;
        if(filesystem::exists(cur, ec)){
            for(const auto& e : filesystem::directory_iterator(cur, ec)){
                string nm = e.path().filename().string(), nml = nm;
                transform(nml.begin(), nml.end(), nml.begin(), ::tolower);
                if(nml == want){ cur = e.path(); found = true; break; }
            }
        }
        if(!found) return {};
    }
    return cur;
}

// Resolve an include file path by searching source directory then a list of search paths.
// Tries each root + filename, optionally with an added extension. Path separators are
// normalized first; every path component is then matched case-insensitively (see
// resolveCaseInsensitive). Returns the resolved absolute path, or empty if not found.
string resolveIncludePath(const string& filename, const string& extension,
                                  const filesystem::path& sourceDir,
                                  const vector<string>& searchPaths){
    string normalized = filename;
    for(char& c : normalized)
        if(c == '/' || c == '\\') c = filesystem::path::preferred_separator;
    vector<filesystem::path> roots;
    roots.push_back(sourceDir);
    for(const string& sp : searchPaths) roots.push_back(filesystem::path(sp));
    // Virtual overlay takes precedence over disk (LSP mode only; the map is empty during
    // normal compiles). A live-scanned `_blorbAssets.bgl` thus resolves even when no file
    // exists on disk, and wins over any stale on-disk copy. The returned path need not
    // exist — parseFile/preScanFile fall back to the raw string when canonical() fails,
    // and fileLexer::open serves the overlay content for it.
    if(!g_virtualBglFiles.empty()){
        string dummy;
        for(const auto& root : roots){
            filesystem::path cand = root / filesystem::path(normalized + extension);
            if(lookupVirtualFile(cand.string(), dummy))
                return cand.lexically_normal().string();
            if(!extension.empty()){
                cand = root / filesystem::path(normalized);
                if(lookupVirtualFile(cand.string(), dummy))
                    return cand.lexically_normal().string();
            }
        }
    }
    for(const auto& root : roots){
        filesystem::path r = resolveCaseInsensitive(root, filesystem::path(normalized + extension));
        if(!r.empty()) return filesystem::canonical(r).string();
        if(!extension.empty()){
            r = resolveCaseInsensitive(root, filesystem::path(normalized));
            if(!r.empty()) return filesystem::canonical(r).string();
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

void bglParser::unknownTypeError(const token& typeTok){
    string shown = typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue;
    string lower = typeTok.value;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    std::regex decl("^\\s*(?:(?:extern|emitter|value|primitive|superposed|abstract|typesealed)\\s+)*"
                    "(?:class|enum|bnum|union)\\s+" + lower + "\\b", std::regex::icase);
    vector<filesystem::path> files;
    std::error_code ec;
    for(auto it = filesystem::recursive_directory_iterator(settings.libPath, ec);
        !ec && it != filesystem::recursive_directory_iterator(); it.increment(ec))
        if(it->is_regular_file(ec) && it->path().extension() == ".bgl") files.push_back(it->path());
    sort(files.begin(), files.end());
    for(const auto& f : files){
        ifstream in(f);
        // A target-only core file opens with `#if TARGET_X #else #error` (X only) or `#if TARGET_X #error` (the other).
        string target, guard;
        for(string line; getline(in, line); ){
            string t = line.substr(min(line.size(), line.find_first_not_of(" \t")));
            if(t.rfind("#if TARGET_", 0) == 0) guard = t.find("GLULX") != string::npos ? "glulx" : "z";
            else if(!guard.empty() && !t.empty()){
                bool glulx = (guard == "glulx") == (t.rfind("#else", 0) == 0);
                if(t.rfind("#else", 0) == 0 || t.rfind("#error", 0) == 0) target = glulx ? "a Glulx" : "a Z-machine";
                guard.clear();
            }
            if(std::regex_search(line, decl)){
                if(!target.empty() && f.parent_path().filename() == "core")
                    parsingError(format("Unknown type '{0}': it exists only on {1} target. Set the target in "
                                        "#beguilerSettings.", shown, target));
                parsingError(format("Unknown type '{0}'. It is declared by the library extension <{1}>: add "
                                    "`#include <{1}>`.", shown, f.stem().string()));
                return;
            }
        }
    }
    parsingError(format("Unknown type '{0}': no class, enum or union of that name is declared.", shown));
}

// Recursive helper for findLibIncludeRecursive. Scans `dir` in pre-order — this folder's files
// first, then subfolders in alphabetical (case-insensitive) order, depth-first. `specLower` is the
// lowercased include spec split on '/', with the last element being the target file name
// ("name.bgl") and the preceding elements the required trailing parent-folder chain. `relComps` are
// the lowercased path components of `dir` relative to the lib root, for the parent-folder suffix match.
static filesystem::path findLibIncludeWalk(const filesystem::path& dir,
                                           const vector<string>& specLower, vector<string>& relComps){
    std::error_code ec;
    if(!filesystem::exists(dir, ec)) return {};
    vector<filesystem::path> files, subdirs;
    for(const auto& e : filesystem::directory_iterator(dir, ec)){
        if(e.is_regular_file(ec))   files.push_back(e.path());
        else if(e.is_directory(ec)) subdirs.push_back(e.path());
    }
    auto ciLess = [](const filesystem::path& a, const filesystem::path& b){
        string an = a.filename().string(), bn = b.filename().string();
        transform(an.begin(), an.end(), an.begin(), ::tolower);
        transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
        return an < bn;
    };
    sort(files.begin(), files.end(), ciLess);
    sort(subdirs.begin(), subdirs.end(), ciLess);
    // 1) files in THIS folder — match name + required trailing parent-folder chain
    const string& wantFile = specLower.back();
    size_t needParents = specLower.size() - 1;
    for(const auto& f : files){
        string fn = f.filename().string();
        transform(fn.begin(), fn.end(), fn.begin(), ::tolower);
        if(fn != wantFile) continue;
        if(needParents > relComps.size()) continue;
        bool ok = true;
        for(size_t i = 0; i < needParents; i++)
            if(relComps[relComps.size() - needParents + i] != specLower[i]){ ok = false; break; }
        if(ok) return f;
    }
    // 2) recurse subfolders alphabetically
    for(const auto& d : subdirs){
        string dn = d.filename().string();
        transform(dn.begin(), dn.end(), dn.begin(), ::tolower);
        relComps.push_back(dn);
        filesystem::path r = findLibIncludeWalk(d, specLower, relComps);
        relComps.pop_back();
        if(!r.empty()) return r;
    }
    return {};
}

// Resolve an angle-bracket library include `<name>` by recursively searching the lib root (beguiLib):
// its files first, then subfolders in alphabetical order, depth-first. The include name is treated as
// a PATH SUFFIX — `<file>` matches a `file.bgl` anywhere in the tree; `<sub/file>` matches a `file.bgl`
// whose immediate parent folder is `sub` (at any depth); `<a/b/file>` requires the path to end with
// `a/b/file.bgl`. Returns the first match in traversal order, or an empty path if none is found.
filesystem::path findLibIncludeRecursive(const filesystem::path& root, const string& includeName){
    vector<string> specLower;
    string cur;
    for(char c : includeName){
        if(c == '/' || c == '\\'){ specLower.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    specLower.push_back(cur + ".bgl");           // last component carries the .bgl extension
    for(auto& s : specLower) transform(s.begin(), s.end(), s.begin(), ::tolower);
    if(specLower.back() == ".bgl") return {};     // empty file name (e.g. trailing '/')
    vector<string> relComps;
    return findLibIncludeWalk(root, specLower, relComps);
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
    includedFilePaths.clear();
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
    i6DefinedSymbols.clear();
    definedSymbols["beguiler"] = to_string(BEGUILER_VERSION);
    definedSymbols["beguilermajor"]   = to_string(BEGUILER_VERSION / 1000);
    definedSymbols["beguilerminor"]   = to_string((BEGUILER_VERSION % 1000) / 10);
    definedSymbols["beguilerpatch"]   = to_string(BEGUILER_VERSION % 10);
    definedSymbolsPreScanSeed.clear();
    hasPreScanSeed = false;
    declaredSymbols.clear();   // #declare set — repopulated by the pre-scan across the whole include graph
    lspErrors.clear();
    // Mid-statement state: a parse that stopped at an error (LSP recovery) can leave any of these set,
    // and the next parse of the same text then resolves names in the wrong scope.
    activeBlockStack.clear();
    anonObjectCounter = 0;
    emitterExpansionChain.clear();
    accessorOuter = nullptr;
    lambdaOuterFunc = nullptr;
    lambdaOuterBody = nullptr;
    lambdaOuterFuncStack.clear();
    currentExpectedType.clear();
    allowVoidReturnExpr = false;
    looseIdentifierMode = false;
    stashedToken = nullopt;
    currentStatementSrc = sourceLocation();
    // Drain any files left open by a prior parse that bailed under LSP error recovery. Otherwise
    // getNumberOfOpenFiles() stays > 0 and the next entry parse skips the BLR auto-load (see
    // parseFile), losing every base type and derailing the parse.
    file.reset();
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
// `_<paramTypeName>` for each parameter. Template arguments are kept (`func<int, int>` →
// `func_int_int`), since overloads may differ only in them. Used by mangleOverloadSet below
// and (via the same helper) by call-site emission paths.
string mangleObjectMethodName(functionDef* fd){
    string safe = fd->name + "_" + to_string(fd->params.size());
    for(paramDef* p : fd->params){
        string t;
        for(char c : p->type.name)
            if(isalnum((unsigned char)c) || c == '_') t += c;
            else if((c == '<' || c == ',') && !t.empty() && t.back() != '_') t += '_';
        while(!t.empty() && t.back() == '_') t.pop_back();
        safe += "_";
        safe += t.empty() ? "var" : t;
    }
    return safe;
}

// Helper: mangle the i6name of every non-emitter member of an overload set on `members`.
// A single member keeps its plain name; a set of two or more needs each entry distinguished,
// or the class emits the same I6 property twice ("Property given twice in the same
// declaration") and will not assemble.
static void mangleOverloadSet(vector<typeMember*>& members, const string& methodName){
    // Pre-scan stubs count: a call parsed before the class itself names the overload by its stub,
    // which mangles exactly as the definition replacing it will. A stub and its definition are one
    // signature, so a set is two distinct signatures.
    vector<functionDef*> group;
    for(typeMember* m : members)
        if(auto* fd = dynamic_cast<functionDef*>(m))
            if(fd->name == methodName && !fd->isEmitter)
                group.push_back(fd);
    set<string> signatures;
    for(functionDef* fd : group) signatures.insert(mangleObjectMethodName(fd));
    if(signatures.size() < 2) return;
    for(functionDef* fd : group){
        // Conversion operators take no parameters; they are told apart by result type, which their
        // declaration already put in the name (`_opconv_int`).
        if(fd->name == "operator()") continue;
        // Operators are mangled at parse time (`==` → `_opeqeq`), which is what makes a
        // declared-but-never-called operator emit a legal I6 identifier. That name is shared
        // by every overload, so an overload SET needs the parameter types appended too.
        // Recomputed from the operator name rather than appended to the current i6name, so
        // running this pass more than once (the call-site path mangles eagerly, and
        // assignObjectMethodOverloadMangling sweeps afterwards) is idempotent.
        bool isOperator = !fd->name.empty() && !isalpha((unsigned char)fd->name[0])
                          && fd->name[0] != '_';
        if(isOperator || fd->name == "operator()"){
            string disc;
            for(paramDef* p : fd->params){
                string t = p->type.name;
                size_t lt = t.find('<');
                if(lt != string::npos) t = t.substr(0, lt);
                disc += "_" + (t.empty() ? string("var") : t);
            }
            fd->i6name = mangleOperatorName(fd->name) + disc;
        }
        // `print()` keeps its name: the runtime prints a value through `x.print()` (`provides print`).
        else if(fd->name == "print" && fd->params.empty()) continue;
        // A method renamed for a global of its name (`_m_show`) is told apart from the global by its arity too.
        else if(fd->i6name.empty() || fd->i6name.rfind("_m_", 0) == 0)
            fd->i6name = mangleObjectMethodName(fd);
    }
}

// A class-typed OBJECT MEMBER whose type has an `init` emitter needs that init run, exactly as
// a local or a file-scope variable does. Locals inject it at the declaration and file-scope vars
// record it in globalInits; object members had no path at all, so `object j { stringObj name =
// "fish"; }` left the raw literal in the property and every method call dispatched against `j`.
//
// Recorded here rather than at the member's parse site because a member's declared value is
// attached after the member itself, and because inherited members have to be swept too.
// Property declarations are TYPE-LESS: the type lives at each use site (each contributing member's
// own declaration), not on the property. This pass enforces the two rules that keep an ADDITIVE
// property coherent — its contributions accumulate into one shared run of words, so a mismatch
// buries one layout inside another:
//   (1) every member bound to an additive property must be a raw array (a tracked array's trailing
//       length slot would otherwise land inside the accumulated data); and
//   (2) all contributions within ONE class hierarchy (the class's ancestor chain plus every
//       instance of it) must use the SAME element type — fixed by the highest ancestor that
//       declares it. Unrelated hierarchies may each fix their own element type.
void bglParser::renameI6KeywordNames(){
    for(typeDef* g : languageService.globals){
        if(!g->i6name.empty() || !bglLanguageService::isI6StatementKeyword(g->name)) continue;
        if(auto* od = dynamic_cast<objectDef*>(g)){ if(od->isExternal) continue; }
        else if(auto* fd = dynamic_cast<functionDef*>(g)){ if(fd->isExternal || fd->isEmitter) continue; }
        else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){ if(vd->isExternal) continue; }
        else continue;
        g->i6name = "_" + g->dName();
        g->i6nameAvoidsKeyword = true;
    }
}

void bglParser::validateRoutinePropertyClashes(){
    auto lower = [](string s){ transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    auto at = [](const sourceLocation& src){ return src.file.empty() ? string("an included file") : format("{0}:{1}", src.file, src.line); };
    // Every property a non-superposed class or object emits, by I6 name.
    struct PropertyUse { string owner; typeMember* member; sourceLocation src; };
    map<string, PropertyUse> properties;
    auto addMembers = [&](const vector<typeMember*>& members, const string& owner){
        for(typeMember* m : members){
            if(m->isPrePassStub) continue;
            sourceLocation src;
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->isStatic || vd->isNamespaceAlias()) continue;
                src = vd->src;
            } else if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->isEmitter || fd->isStatic) continue;
                src = fd->src;
            } else continue;
            properties.emplace(lower(m->i6name.empty() ? m->name : m->i6name), PropertyUse{owner, m, src});
        }
    };
    for(typeDef* g : languageService.globals){
        if(auto* cd = dynamic_cast<classDef*>(g)){
            if(!cd->isEmitterClass && !cd->isSuperposed && !cd->isAlias) addMembers(cd->members, cd->dName());
        } else if(auto* od = dynamic_cast<objectDef*>(g)){
            if(!od->isSuperposed) addMembers(od->members, od->dName());
        }
    }
    // Free-standing `property` declarations, the bindings' and the core's (`name`), are properties too.
    for(typeDef* g : languageService.globals)
        if(auto* vd = dynamic_cast<variableDeclaration*>(g); vd != nullptr && vd->type.name == "property")
            properties.emplace(lower(vd->i6name.empty() ? vd->name : vd->i6name), PropertyUse{"", vd, vd->src});
    for(typeDef* g : languageService.globals){
        auto* fd = dynamic_cast<functionDef*>(g);
        if(fd == nullptr || fd->isEmitter || fd->isExternal || fd->isSuperposed || fd->isPrePassStub || fd->isReplacedDead)
            continue;
        string routine = fd->i6name.empty() ? fd->dName() : fd->i6name;
        auto it = properties.find(lower(routine));
        if(it != properties.end() && it->second.owner.empty()){
            parsingError(format("{0}:{1}:1: routine '{2}' has the same name as the property '{3}'{4}; Inform 6 keeps "
                "routines and properties in one namespace, ignoring case, so rename the routine.",
                fd->src.file, fd->src.line, routine, it->second.member->dName(),
                it->second.src.file.empty() ? string() : format(" ({0})", at(it->second.src))));
            continue;
        }
        if(it == properties.end()) continue;
        parsingError(format("{0}:{1}:1: routine '{2}' has the same name as member '{3}' of '{4}' ({5}); "
            "Inform 6 keeps routines and properties in one namespace, ignoring case, so rename one of them.",
            fd->src.file, fd->src.line, routine, it->second.member->dName(), it->second.owner, at(it->second.src)));
    }
    // A data member that meets a global is renamed `_m_<name>`; a method is not, so an object or
    // global variable named like one is the same clash.
    for(typeDef* g : languageService.globals){
        sourceLocation src; string kind;
        if(auto* od = dynamic_cast<objectDef*>(g)){
            if(od->isExternal || od->isSuperposed || od->isPrePassStub) continue;
            src = od->src; kind = "object";
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
            if(vd->isExternal || vd->isConst || vd->isNamespaceAlias() || vd->isPrePassStub) continue;
            if(vd->type.name == "property") continue;   // declares the very property the methods give
            src = vd->src; kind = "global";
        } else continue;
        string name = g->i6name.empty() ? g->dName() : g->i6name;
        auto it = properties.find(lower(name));
        if(it == properties.end() || dynamic_cast<functionDef*>(it->second.member) == nullptr) continue;
        parsingError(format("{0}:{1}:1: {2} '{3}' has the same name as method '{4}' of '{5}' ({6}); "
            "Inform 6 keeps objects, variables and properties in one namespace, ignoring case, so rename one "
            "of them or give one an `asI6` name.",
            src.file, src.line, kind, name, it->second.member->dName(), it->second.owner, at(it->second.src)));
    }
}

// Two globals that emit the same Inform 6 symbol: I6 keeps one namespace, ignoring case, for
// routines, objects, variables, arrays, constants and classes. A function that emits nothing (an
// emitter) can share its name with a variable or object (§3.10); two emitted symbols can't.
void bglParser::validateGlobalNameClashes(){
    auto lower = [](string s){ transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    auto at = [](const sourceLocation& src){ return src.file.empty() ? string("an included file") : format("{0}:{1}", src.file, src.line); };
    struct Symbol { typeDef* def; string kind; sourceLocation src; };
    map<string, Symbol> seen;
    for(typeDef* g : languageService.globals){
        string kind; sourceLocation src;
        if(auto* fd = dynamic_cast<functionDef*>(g)){
            if(fd->isEmitter || fd->isExternal || fd->isSuperposed || fd->isPrePassStub || fd->isReplacedDead) continue;
            kind = "function"; src = fd->src;
        } else if(auto* od = dynamic_cast<objectDef*>(g)){
            if(od->isExternal || od->isSuperposed || od->isPrePassStub) continue;
            kind = "object"; src = od->src;
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
            if(vd->isExternal || vd->isNamespaceAlias() || vd->isPrePassStub || vd->isInstanceBacking || vd->isSynthetic) continue;
            kind = dynamic_cast<arrayDeclaration*>(vd) ? "array" : "variable"; src = vd->src;
        } else continue;
        if(src.file.empty()) continue;   // compiler-made
        string name = lower(g->i6name.empty() ? g->dName() : g->i6name);
        auto [it, fresh] = seen.emplace(name, Symbol{g, kind, src});
        if(fresh || it->second.def == g) continue;
        parsingError(format("{0}:{1}:1: {2} '{3}' has the same name as {4} '{5}' ({6}); Inform 6 keeps these "
            "in one namespace, ignoring case, so rename one of them or give one an `asI6` name.",
            src.file, src.line, kind, g->dName(), it->second.kind, it->second.def->dName(), at(it->second.src)));
    }
    // A name the library binding declares (a global, an attribute, an object) is taken in Inform 6 too.
    // A library routine is left out: a program defines the library's entry points under their names.
    map<string, typeDef*> library;
    for(typeDef* g : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(g); vd && vd->isExternal)
            library.emplace(lower(vd->i6name.empty() ? vd->dName() : vd->i6name), g);
        else if(auto* od = dynamic_cast<objectDef*>(g); od && od->isExternal && !dynamic_cast<verbObjectDef*>(od))
            library.emplace(lower(od->i6name.empty() ? od->dName() : od->i6name), g);
    }
    // A library routine is taken too, by anything but a routine of the program's own.
    map<string, functionDef*> libraryRoutines;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->isExternal && !fd->isEmitter
           && fd->src.file.find("bindings") != string::npos)
            libraryRoutines.emplace(lower(fd->i6name.empty() ? fd->dName() : fd->i6name), fd);
    for(auto& [name, sym] : seen){
        if(sym.kind == "function" || sym.src.file.find("beguiLib") != string::npos) continue;
        if(auto r = libraryRoutines.find(name); r != libraryRoutines.end())
            parsingError(format("{0}:{1}:1: {2} '{3}' has the same name as the library routine '{4}'; Inform 6 keeps "
                "these in one namespace, ignoring case, so rename the {2} or give it an `asI6` name.",
                sym.src.file, sym.src.line, sym.kind, sym.def->dName(), r->second->dName()));
    }
    for(auto& [name, sym] : seen){
        auto lib = library.find(name);
        if(lib == library.end() || sym.src.file.find("beguiLib") != string::npos) continue;
        auto* lv = dynamic_cast<variableDeclaration*>(lib->second);
        bool libProperty = lv != nullptr && lv->type.name == "property";
        if(libProperty)   // the program's own `property` of the name is that property
            if(auto* sv = dynamic_cast<variableDeclaration*>(sym.def); sv != nullptr && sv->type.name == "property") continue;
        string what = libProperty ? "property" : lv != nullptr && lv->type.name == "attribute" ? "attribute" : lv != nullptr ? "global" : "object";
        parsingError(format("{0}:{1}:1: {2} '{3}' has the same name as the library's {4} '{5}'; Inform 6 keeps these "
            "in one namespace, ignoring case, so rename the {2} or give it an `asI6` name.",
            sym.src.file, sym.src.line, sym.kind, sym.def->dName(), what, lib->second->dName()));
    }
}

// A program's verb is a global name (§3.1): no other global may share it, ignoring case, and it may
// not redeclare a verb the binding already declares — that one is extended, not declared again. Its
// I6 names are prefixed, so the I6-level check above never sees these clashes.
// `early`: run on the pre-scan's stubs, before a clash can derail the parse (an `extend` or a use of
// the name then resolves to the wrong one and fails with an unrelated error).
void bglParser::validateVerbNameClashes(bool early){
    auto lower = [](string s){ transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    auto at = [](const sourceLocation& src){ return src.file.empty() ? string("an included file") : format("{0}:{1}", src.file, src.line); };
    for(verbObjectDef* v : languageService.verbs){
        // A library verb is a global name too: the program's own globals mustn't take it.
        if(v->isExternal){
            string name = lower(v->dName());
            for(typeDef* g : languageService.globals){
                if(g == v || lower(g->dName()) != name) continue;
                sourceLocation gsrc; string kind;
                if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                    if(vd->isExternal || vd->isNamespaceAlias() || vd->isInstanceBacking || vd->isSynthetic) continue;
                    kind = dynamic_cast<arrayDeclaration*>(vd) || vd->type.name.rfind("array", 0) == 0 ? "array" : "variable";
                    gsrc = vd->src;
                } else if(auto* od = dynamic_cast<objectDef*>(g)){
                    if(od->isExternal || dynamic_cast<verbObjectDef*>(od)) continue;
                    kind = "object"; gsrc = od->src;
                } else continue;   // a function is called, never named bare, so it can't be taken for the verb
                if(gsrc.file.empty() || gsrc.file.find("beguiLib") != string::npos) continue;
                parsingError(format("{0}:{1}:1: {2} '{3}' has the same name as the library verb '{4}'; a verb is a "
                    "global name, so rename the {2}.", gsrc.file, gsrc.line, kind, g->dName(), v->dName()));
            }
            continue;
        }
        if(v->src.file.empty()) continue;
        string name = lower(v->dName());
        for(verbObjectDef* o : languageService.verbs)
            if(o != v && o->isExternal && lower(o->dName()) == name)
                parsingError(format("{0}:{1}:1: verb '{2}' has the same name as the library verb '{3}' ({4}); "
                    "rename it, or add to the library verb with `extend {3} {{ … }}`.",
                    v->src.file, v->src.line, v->dName(), o->dName(), at(o->src)));
        // The verb's action routine is <name>Sub: a library routine of that name already holds it.
        if(auto* fd = languageService.findGlobalAs<functionDef>(name + "sub"); fd && !fd->isEmitter && !fd->isPrePassStub)
            parsingError(format("{0}:{1}:1: a verb's action routine is its name followed by 'Sub', and '{2}' is already "
                "a routine ({3}); rename the verb.", v->src.file, v->src.line, fd->dName(), at(fd->src)));
        for(typeDef* g : languageService.globals){
            if(g == v || lower(g->dName()) != name) continue;
            string kind; sourceLocation gsrc;
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                // A program's own emitter is a global name too (`rule;` would reach the verb); the library's
                // emitters are left to the library-name checks.
                bool libraryEmitter = fd->isEmitter && (fd->src.file.empty()
                    || fd->src.file.find(settings.libPath) == 0);
                if(libraryEmitter || (fd->isPrePassStub && !early) || fd->isReplacedDead) continue;
                kind = fd->isEmitter ? "emitter" : "function"; gsrc = fd->src;
            } else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                if(vd->isNamespaceAlias() || (vd->isPrePassStub && !early) || vd->isInstanceBacking || vd->isSynthetic) continue;
                kind = dynamic_cast<arrayDeclaration*>(vd) || vd->type.name.rfind("array", 0) == 0 || vd->type.name.rfind("rawarray", 0) == 0
                     ? "array" : vd->type.name == "property" ? "property" : "variable";
                gsrc = vd->src;
            } else if(auto* od = dynamic_cast<objectDef*>(g)){
                if(dynamic_cast<verbObjectDef*>(od) || (od->isPrePassStub && !early)) continue;
                kind = "object"; gsrc = od->src;
            } else continue;
            parsingError(format("{0}:{1}:1: verb '{2}' has the same name as {3} '{4}' ({5}); a verb is a "
                "global name, so rename one of them.", v->src.file, v->src.line, v->dName(), kind, g->dName(),
                at(gsrc)));
        }
    }
}

// A file-scope array's initial elements are fixed when the program is compiled: a global variable named
// there (declared above or below) is a value-class instance's copy, or else an error.
void bglParser::resolveArrayInitialGlobals(){
    for(typeDef* g : languageService.globals){
        auto* ad = dynamic_cast<arrayDeclaration*>(g);
        auto* list = ad != nullptr ? dynamic_cast<initializerList*>(ad->declaredExpressionValue) : nullptr;
        if(list == nullptr || ad->src.file.find("beguiLib") != string::npos) continue;
        for(expression* e : list->elements){
            if(e == nullptr || dynamic_cast<initializerList*>(e) != nullptr || e->tokens.size() != 1) continue;
            string word = e->tokens[0];
            transform(word.begin(), word.end(), word.begin(), ::tolower);
            auto* gv = languageService.findGlobalAs<variableDeclaration>(word);
            if(gv == nullptr || gv->isConst || dynamic_cast<arrayDeclaration*>(gv) != nullptr || gv->isExternal) continue;
            const string& t = gv->type.name;
            if(t == "attribute" || t == "property" || t == "verb" || t == "grammartoken") continue;
            classDef* vc = getDispatchClass(t);
            if(vc != nullptr && isValueClass(vc) && !vc->isEmitterClass && gv->declaredExpressionValue != nullptr
               && gv->declaredExpressionValue->tokens.size() == 1 && gv->declaredExpressionValue->tokens[0].rfind("_bglanon", 0) == 0)
                if(auto* made = languageService.findGlobalAs<objectDef>(gv->declaredExpressionValue->tokens[0])){
                    string copyName = format("_bglanon{0}", anonObjectCounter++);
                    objectDef& copy = languageService.registerObject(copyName, false, "");
                    copy.objectClass = made->objectClass;
                    copy.members = made->members;
                    if(std::find(languageService.globals.begin(), languageService.globals.end(), &copy) == languageService.globals.end())
                        languageService.globals.push_back(&copy);
                    e->tokens[0] = copyName;
                    continue;
                }
            // An uninitialized value-class global names the instance it owns.
            if(vc != nullptr && isValueClass(vc) && gv->declaredExpressionValue == nullptr) continue;
            string where = ad->src.line > 0 ? format("{0}:{1}:1: ", ad->src.file, ad->src.line) : string();
            parsingError(where + format("'{0}' is a variable, and the initial elements of '{1}' are fixed when the program "
                "is compiled; assign the element when the program runs, or make '{0}' a const.", gv->dName(), ad->dName()));
        }
    }
}

// A class that declares `==` without `!=` keeps the inherited `!=`, which compares identity, so
// `a == b` and `a != b` can both be true.
void bglParser::validateEqualityPairs(){
    for(typeDef* g : languageService.globals){
        auto* cls = dynamic_cast<classDef*>(g);
        if(cls == nullptr || cls->isExternal || cls->isEmitterClass || cls->isAlias || cls->isPrePassStub) continue;
        functionDef* eq = nullptr; bool ne = false;
        for(typeMember* m : cls->members)
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name == "==") eq = fd;
                if(fd->name == "!=") ne = true;
            }
        // A `!=` for the same operand inherited from a base (not the roots') compares by value too.
        if(eq != nullptr && !ne && !eq->params.empty())
            for(classDef* b = cls->baseClasses.empty() ? nullptr : cls->baseClasses[0]; b != nullptr && !ne;
                b = b->baseClasses.empty() ? nullptr : b->baseClasses[0]){
                if(b->name == "_bglobject" || b->name == "_bglprimitive") break;
                for(typeMember* m : b->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == "!=" && fd->params.size() == eq->params.size()
                       && fd->params.back()->type.name == eq->params.back()->type.name) ne = true;
            }
        if(eq == nullptr || ne || eq->src.file.empty() || eq->src.file.find("beguiLib") != string::npos) continue;
        // After the parse no file is open, so the location comes from the declaration.
        cerr << format("{0}:{1}:{2}: warning: class '{3}' declares operator == but not operator !=, so != "
            "still compares identity; declare operator != too.", eq->src.file, eq->src.line, max(1, eq->src.col),
            cls->dName()) << endl;
    }
}

// A `required` class member must get a value from each object of the class: from the object itself or
// from a class between it and the declaring class. A class that requires a member an ancestor already
// gives a value has no effect (a warning).
void bglParser::validateRequiredMembers(){
    // Does this class body itself give `name` a value?
    // A required routine declared with `;` gets an empty placeholder body, so its body doesn't count;
    // nor does a `default` body on a required routine, which is the fallback for extern objects.
    auto givesValue = [](typeMember* m){
        if(m->isPrePassStub) return false;
        if(auto* fd = dynamic_cast<functionDef*>(m)){
            if(fd->isRequired && fd->isDefault) return false;
            auto* blk = dynamic_cast<statementBlock*>(fd->body);
            return fd->body != nullptr && !(fd->isRequired && blk != nullptr && blk->statements.empty());
        }
        if(auto* ad = dynamic_cast<arrayDeclaration*>(m)) return ad->arraySize > 0 || ad->declaredExpressionValue != nullptr;
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)) return vd->declaredExpressionValue != nullptr;
        return false;
    };
    auto classSupplies = [&](classDef* c, const string& name){
        for(typeMember* m : c->members)
            if(m->name == name && !m->isRequired && givesValue(m)) return true;
        return false;
    };
    auto derives = [](classDef* c, classDef* from){
        function<bool(classDef*)> walk = [&](classDef* k){
            if(k == from) return true;
            for(classDef* b : k->baseClasses) if(walk(b)) return true;
            return false;
        };
        return walk(c);
    };
    // Is the requirement met on some path from `c` up to (not including) `owner`?
    function<bool(classDef*, classDef*, const string&)> metBelow = [&](classDef* c, classDef* owner, const string& name){
        if(c == owner) return false;
        if(classSupplies(c, name)) return true;
        for(classDef* b : c->baseClasses)
            if(derives(b, owner) && metBelow(b, owner, name)) return true;
        return false;
    };
    auto where = [](const sourceLocation& s){ return format("{0}:{1}:{2}: ", s.file, s.line, max(1, s.col)); };

    vector<pair<classDef*, typeMember*>> required;
    for(typeDef* g : languageService.globals){
        auto* cls = dynamic_cast<classDef*>(g);
        if(cls == nullptr || cls->isPrePassStub) continue;
        for(typeMember* m : cls->members){
            if(!m->isRequired || givesValue(m)) continue;   // a value of its own already meets it (warned at parse)
            string shown = m->displayName.empty() ? m->name : m->displayName;
            classDef* giver = nullptr;
            for(classDef* b : cls->baseClasses){
                function<classDef*(classDef*)> find = [&](classDef* k) -> classDef* {
                    if(classSupplies(k, m->name)) return k;
                    for(classDef* bb : k->baseClasses) if(classDef* f = find(bb)) return f;
                    return nullptr;
                };
                if((giver = find(b)) != nullptr) break;
            }
            sourceLocation src;
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)) src = vd->src;
            else if(auto* fd = dynamic_cast<functionDef*>(m)) src = fd->src;
            if(giver != nullptr){
                cerr << where(src) << format("warning: '{0}' already gets a value from '{1}', so 'required' has no effect here.",
                                             shown, giver->dName()) << endl;
                continue;
            }
            required.push_back({cls, m});
        }
    }
    if(required.empty()) return;
    vector<objectDef*> objects;
    for(typeDef* g : languageService.globals)
        if(auto* obj = dynamic_cast<objectDef*>(g)) objects.push_back(obj);
    for(verbObjectDef* v : languageService.verbs)
        if(find(objects.begin(), objects.end(), (objectDef*)v) == objects.end()) objects.push_back(v);
    for(objectDef* obj : objects){
        if(obj->isPrePassStub || obj->isExternal) continue;   // Inform 6 supplies an extern object
        vector<classDef*> classes;
        if(obj->objectClass) classes.push_back(obj->objectClass);
        for(typeDef* b : obj->baseClasses)
            if(auto* bc = dynamic_cast<classDef*>(b); bc && find(classes.begin(), classes.end(), bc) == classes.end())
                classes.push_back(bc);
        for(auto& [owner, m] : required){
            bool isA = false, met = false;
            for(classDef* c : classes)
                if(derives(c, owner)){ isA = true; if(metBelow(c, owner, m->name)) met = true; }
            if(!isA || met) continue;
            bool own = false;
            for(typeMember* om : obj->members) if(om->name == m->name && !om->isPrePassStub) own = true;
            if(own) continue;
            string shown = m->displayName.empty() ? m->name : m->displayName;
            parsingError(where(obj->src) + format("'{0}' must give '{1}', which '{2}' requires.",
                                                  obj->dName(), shown, owner->dName()));
        }
    }
}

// Post-parse validation of `hide` directives. A hide that doesn't resolve to an INHERITED member
// (or, for an operator hide, whose member type doesn't expose that operator) is a WARNING, not an
// error (Jim's call — typo/refactor tolerance); the entry simply has no effect. Runs after the full
// parse so base classes are resolved regardless of declaration order.
void bglParser::validateHiddenMembers(){
    for(typeDef* g : languageService.globals){
        auto* cls = dynamic_cast<classDef*>(g);
        if(cls == nullptr) continue;
        for(const hiddenMember& hm : cls->hiddenMembers){
            // The hidden member must exist in the INHERITED surface (a base class) — you hide what
            // you inherited; hiding a self-declared member is "just don't declare it".
            typeMember* mem = nullptr;
            for(classDef* b : cls->baseClasses){
                mem = b->findMember([&](typeMember* m){ return m->name == hm.memberName; });
                if(mem) break;
            }
            string where = hm.src.line > 0 ? format("{0}:{1}:1: ", hm.src.file, hm.src.line) : string();
            if(mem == nullptr){
                parsingWarning(where + format("hide '{0}' on '{1}': no inherited member named '{0}' — "
                    "the hide has no effect (you can only hide members inherited from a base class).",
                    hm.memberName, cls->dName()));
                continue;
            }
            if(!hm.operatorName.empty()){
                auto* vd = dynamic_cast<variableDeclaration*>(mem);
                classDef* memType = vd ? getDispatchClass(vd->type.name) : nullptr;
                bool opFound = memType != nullptr && findMemberInHierarchy(memType, [&](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == hm.operatorName;
                }) != nullptr;
                if(!opFound)
                    parsingWarning(where + format("hide '{0}.operator {1}' on '{2}': {3} has no "
                        "'operator {1}' — the hide has no effect.",
                        hm.memberName, hm.operatorName, cls->dName(),
                        vd ? format("type '{0}'", typeDisplayName(vd->type.name)) : format("member '{0}'", hm.memberName)));
            }
        }
    }
}

void bglParser::checkTypedPropertyMemberTypes(){
    // The property name a member binds to (its i6name override, else its declared name).
    auto propOf = [](variableDeclaration* vd){ return vd->i6name.empty() ? vd->name : vd->i6name; };

    // (1) every member bound to an additive property must be a `rawArray<T>`, a routine or a string. The
    // contributions accumulate into one shared run of words: a tracked `array<T>` keeps its length
    // in a trailing slot that would land inside that data, and a scalar has no run at all. A routine
    // is exempt — I6's `before`/`after`/`life`/… are routine-valued additive properties. Rejected
    // here rather than silently demoted, so the source says what the layout is.
    auto checkRaw = [&](vector<typeMember*>& members, const string& ownerLabel){
        for(typeMember* m : members){
            if(dynamic_cast<functionDef*>(m)) continue;        // routine contribution — legal
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd == nullptr) continue;
            string propName = propOf(vd);
            if(!languageService.isAdditiveProperty(propName)) continue;
            auto* ad = dynamic_cast<arrayDeclaration*>(m);
            if(ad != nullptr && ad->isRaw) continue;           // rawArray<T> — the one legal array form
            // An inference-typed override (`name = {…}` with no type of its own) is a bare
            // variableDeclaration whose type is inherited from a base member — which is itself
            // checked here. Its own type is empty at this point, so it is not a scalar to reject.
            if(ad == nullptr && vd->type.name.empty()) continue;
            // A string is one word that the library prints (`each_turn "Water drips.";`) — unless the
            // property is declared as an array of words (`name`), which a string can't join.
            auto declaredAsArray = [&](const string& prop){
                for(typeDef* g : languageService.objectTypes)
                    if(auto* cd = dynamic_cast<classDef*>(g))
                        for(typeMember* cm : cd->members)
                            if(auto* cad = dynamic_cast<arrayDeclaration*>(cm); cad && cad != ad && propOf(cad) == prop) return true;
                return false;
            };
            bool isText = vd->type.name == "string" || vd->type.name == "stringorroutine";
            if(ad == nullptr && isText && !declaredAsArray(propName)) continue;
            if(ad == nullptr && isText){
                string where = vd->src.line > 0 ? format("{0}:{1}:1: ", vd->src.file, vd->src.line) : string();
                parsingError(where + format("'{0}.{1}' is declared '{2}', but '{3}' is the ADDITIVE property that "
                    "holds an object's words, so it takes a `rawArray<T>`. Give the member another name.",
                    ownerLabel, vd->dName(), typeDisplayName(vd->type.name), propName));
            }
            string where = vd->src.line > 0 ? format("{0}:{1}:1: ", vd->src.file, vd->src.line) : string();
            if(ad != nullptr)
                parsingError(where + format("'{0}.{1}' is declared 'array<{2}>', but '{3}' is an ADDITIVE "
                    "property: its contributions accumulate into a single run of words, and a tracked "
                    "array's trailing length slot would land inside that data. Declare it `rawArray<{2}>`.",
                    ownerLabel, vd->dName(), typeDisplayName(ad->elementType), propName));
            else
                parsingError(where + format("'{0}.{1}' is declared '{2}', but '{3}' is an ADDITIVE "
                    "property: it holds a list of every class's and object's contributions, so the member is "
                    "a `rawArray<T>`, a routine or a string. A single value is a one-element list "
                    "(`rawArray<int> {1} = 3;`).",
                    ownerLabel, vd->dName(), typeDisplayName(vd->type.name), propName));
        }
    };

    // Element type an additive property `prop` is given on a member set (first non-empty found).
    auto elemFor = [&](vector<typeMember*>& members, const string& prop) -> string {
        for(typeMember* m : members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m)){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(propOf(vd) == prop && !ad->elementType.empty()) return ad->elementType;
            }
        return "";
    };
    // Canonical element type for an additive property in a class chain, walked ROOT-FIRST so the
    // highest ancestor that declares it wins. "" if no member in the chain gives it an element type.
    std::function<string(classDef*, const string&)> canonical =
        [&](classDef* cd, const string& prop) -> string {
            if(cd == nullptr) return "";
            for(classDef* base : cd->baseClasses){          // bases before self ⇒ root-first
                string t = canonical(base, prop);
                if(!t.empty()) return t;
            }
            return elemFor(cd->members, prop);
        };

    // (2) reconcile each additive-property contribution against the element type its hierarchy fixes.
    // `chainRoot` is the class whose ancestor chain sets the canonical type: the class itself (its
    // own members are part of the chain) or, for an object, its class (own members are leaves that
    // must match what the class hierarchy already declares).
    auto reconcile = [&](vector<typeMember*>& members, const string& ownerLabel,
                         classDef* chainRoot, bool ownerIsInstance){
        for(typeMember* m : members){
            auto* ad = dynamic_cast<arrayDeclaration*>(m);
            if(ad == nullptr || ad->elementType.empty()) continue;
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            string prop = propOf(vd);
            if(!languageService.isAdditiveProperty(prop)) continue;
            string want = canonical(chainRoot, prop);
            if(want.empty() || want == ad->elementType) continue;
            string where = vd->src.line > 0 ? format("{0}:{1}:1: ", vd->src.file, vd->src.line) : string();
            parsingError(where + (ownerIsInstance
                ? format("'{0}.{1}' contributes 'rawArray<{2}>' to the ADDITIVE property '{3}', but its "
                    "class hierarchy fixes '{3}' as 'rawArray<{4}>'. An instance must match the element "
                    "type its class declares. Declare it `rawArray<{4}>`.",
                    ownerLabel, vd->dName(), typeDisplayName(ad->elementType), prop, typeDisplayName(want))
                : format("'{0}.{1}' contributes 'rawArray<{2}>' to the ADDITIVE property '{3}', but the "
                    "highest ancestor that declares '{3}' fixes its element type as '{4}'. Every "
                    "contribution in one hierarchy must use the same element type. Declare it "
                    "`rawArray<{4}>` (or change the ancestor).",
                    ownerLabel, vd->dName(), typeDisplayName(ad->elementType), prop, typeDisplayName(want))));
        }
    };

    for(typeDef* g : languageService.objectTypes)
        if(auto* cd = dynamic_cast<classDef*>(g)){ checkRaw(cd->members, cd->dName());
            reconcile(cd->members, cd->dName(), cd, false); }
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            if(!od->isExternal){ checkRaw(od->members, od->dName());
                reconcile(od->members, od->dName(), od->objectClass, true); }
}

void bglParser::recordObjectMemberInits(){
    auto trim = [](string v){
        size_t a = v.find_first_not_of(" \t\n\r"); if(a == string::npos) return string();
        size_t b = v.find_last_not_of(" \t\n\r");
        return v.substr(a, b - a + 1);
    };
    objectDef* curObj = nullptr;
    // A class-typed member may itself hold class-typed members needing init — an object
    // whose field is a class with a stringObj in it. Walk down as the backing synthesis
    // does, tracking the path, and guarding against a type that contains its own type.
    // Descends as far as backing synthesis does. `onPath` stops a type that contains its
    // own type from recursing forever — the same guard synthesizeFieldBackings uses, and
    // the reason both stop at the same place, so an init is never emitted for a path whose
    // intermediate instance was not created.
    std::function<void(vector<variableDeclaration*>&, const string&, set<classDef*>&)> sweepMembers =
    [&](vector<variableDeclaration*>& members, const string& basePath, set<classDef*>& onPath){
    for(variableDeclaration* vd : members){
        if(vd == nullptr || vd->isStatic || vd->isConst || vd->isExternal) continue;
        if(vd->isRefLocal) continue;                  // a ref member owns nothing to initialise
        auto* cls = languageService.findClass(vd->type.name);
        if(cls == nullptr) continue;
        string path = basePath + "." + (vd->i6name.empty() ? vd->dName() : vd->i6name);
        functionDef* initFn = nullptr;
        if(typeMember* im = findMemberInHierarchy(cls, [&](typeMember* mm){
                auto* f = dynamic_cast<functionDef*>(mm);
                return f && f->name == "init" && f->isEmitter && f->params.empty()
                       && dynamic_cast<i6Block*>(f->body) != nullptr;
           })) initFn = dynamic_cast<functionDef*>(im);
        // No init of its own — but an instance it owns may CONTAIN something that needs one. A
        // reference member refers to an instance owned elsewhere and is empty here.
        if(initFn == nullptr){
            if(isValueClass(cls) && onPath.insert(cls).second){
                vector<variableDeclaration*> nested;
                set<string> nseen;
                cls->forEachMember([&](typeMember* nm){
                    if(auto* nvd = dynamic_cast<variableDeclaration*>(nm))
                        if(nseen.insert(nvd->name).second) nested.push_back(nvd);
                });
                sweepMembers(nested, path, onPath);
                onPath.erase(cls);
            }
            continue;
        }

        emitterBindings ib; ib.self = path; ib.val = path;
        string body = initFn == nullptr ? string()
                    : trim(expandEmitterBody(dynamic_cast<i6Block*>(initFn->body), ib));
        languageService.globalInits.push_back({path, body});

        // The declared value goes through the type's operator= after init, for the same
        // reason it does at file scope: the slot holds what init put there.
        if(vd->declaredExpressionValue != nullptr){
            string rhsText = vd->declaredExpressionValue->text();
            string rhsType = vd->declaredExpressionValue->resolvedType;
            // The class's own emitter operator (an inherited pass-through store changes nothing
            // the static property doesn't already hold).
            functionDef* opFn = findAssignOperator(cls, rhsType, [](functionDef* f){
                return f->isEmitter && dynamic_cast<i6Block*>(f->body) != nullptr; }, /*allowInherited*/false);
            if(opFn != nullptr){
                emitterBindings ob; ob.self = path; ob.val = path;
                ob.fn = opFn; ob.args.push_back(rhsText);
                string ab = trim(expandEmitterBody(dynamic_cast<i6Block*>(opFn->body), ob));
                languageService.globalInits.push_back({path, ab});
                // Only clear the value when it is declared ON THIS OBJECT. An inherited
                // member's value belongs to the class and is shared by every instance —
                // clearing it would strip the default from all of them. Leaving it emits
                // a literal default in the Class directive that bglInit then overwrites
                // per instance, which is harmless.
                bool ownMember = false;
                if(curObj != nullptr) for(typeMember* om : curObj->members) if(om == vd){ ownMember = true; break; }
                if(ownMember) vd->declaredExpressionValue = nullptr;
            }
        }
    }
    };
    for(typeDef* g : languageService.globals){
        auto* obj = dynamic_cast<objectDef*>(g);
        if(obj == nullptr || obj->isExternal) continue;

        // Members declared on the object itself, then those inherited from its class chain.
        // The class case is the common one — `class Room { stringObj desc; }` with many room
        // instances — and sweeping only obj->members missed it entirely: the class-level
        // default baked the literal into the I6 Class, so reads looked right and only writes
        // failed. An own member of the same name wins, matching how the emitter bakes
        // backings. Each INSTANCE gets its own init; the members are shared declarations.
        vector<variableDeclaration*> candidates;
        set<string> seen;
        for(typeMember* m : obj->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(seen.insert(vd->name).second) candidates.push_back(vd);
        if(obj->objectClass != nullptr)
            obj->objectClass->forEachMember([&](typeMember* m){
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(seen.insert(vd->name).second) candidates.push_back(vd);
            });

        set<classDef*> onPath;
        curObj = obj;
        sweepMembers(candidates, objectI6Name(obj->name), onPath);   // an object renamed for I6 (`box`) by that name
        curObj = nullptr;
    }
    // A value-class variable's instance (a global's, a local's or a parameter's backing): its members
    // need the same once-only init (a local's per-call reset leaves a stringObj member's buffer alone).
    set<string> backingsDone;
    auto sweepBacking = [&](variableDeclaration* vd){
        if(vd == nullptr || vd->isExternal || vd->isRefLocal || dynamic_cast<arrayDeclaration*>(vd) != nullptr) return;
        classDef* cls = languageService.findClass(vd->type.name);
        if(cls == nullptr || !isValueClass(cls) || cls->isEmitterClass || cls->isAlias || cls->isExternal) return;
        // A file-scope variable of a value class owns `_bglGlobal_<name>`, which the emitter makes.
        string inst = vd->isInstanceBacking ? (vd->i6name.empty() ? vd->dName() : vd->i6name)
                                            : "_bglGlobal_" + (vd->i6name.empty() ? vd->dName() : vd->i6name);
        if(!backingsDone.insert(inst).second) return;
        vector<variableDeclaration*> members;
        set<string> seen;
        cls->forEachMember([&](typeMember* m){
            if(auto* mv = dynamic_cast<variableDeclaration*>(m)) if(seen.insert(mv->name).second) members.push_back(mv);
        });
        set<classDef*> onPath{cls};
        sweepMembers(members, inst, onPath);
    };
    for(typeDef* g : languageService.globals) sweepBacking(dynamic_cast<variableDeclaration*>(g));
    for(typeDef* g : languageService.objectInstances) sweepBacking(dynamic_cast<variableDeclaration*>(g));
}

// Global functions overload the same way methods do — resolveGlobalCall already picks the
// overload from the argument types. I6 has no overloading, so each member of a set needs its own
// routine name; call sites emit the resolved overload's i6name. A set of one keeps its plain name,
// so ordinary functions are untouched (and stay callable from raw I6 by the name the author wrote).
// `extern` is excluded: it names a routine that already exists in I6, and renaming it would emit a
// call to something that isn't there.
void bglParser::mangleGlobalOverloadSet(const string& name){
    vector<functionDef*> group;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g))
            if(fd->name == name && !fd->isEmitter && !fd->isExternal && !fd->isReplaceStub)
                group.push_back(fd);
    // Count distinct SIGNATURES, not declarations: a pre-scan stub and the real definition it
    // stands for are one function seen twice, and they mangle identically. Only a genuine second
    // signature makes this an overload set.
    set<string> signatures;
    for(functionDef* fd : group) signatures.insert(mangleObjectMethodName(fd));
    if(signatures.size() < 2) return;
    // A name renamed only to keep clear of a library name (`_compare`) takes its overload's name instead.
    for(functionDef* fd : group)
        if(fd->i6name.empty() || fd->i6name == "_" + fd->dName()) fd->i6name = mangleObjectMethodName(fd);
}

void bglParser::assignGlobalFunctionOverloadMangling(){
    set<string> seen;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g))
            if(!fd->isEmitter && !fd->isExternal && seen.insert(fd->name).second)
                mangleGlobalOverloadSet(fd->name);
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
        else if(auto* cd = dynamic_cast<classDef*>(g)){
            manglePerType(cd->members);
            set<string> names;
            for(typeMember* m : cd->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && !fd->isEmitter && !fd->isPrePassStub && names.insert(fd->name).second)
                    mangleClassFamilyOverloadSet(cd, fd->name);
        }
    }
    syncOverrideNames();
}


// A class that manages its own storage (an `init`/`deinit` emitter pair) copies a parameter the way
// it initializes a local: at entry the parameter gets its own instance from `init` and the argument
// is copied in through `operator =`; `deinit` runs on every exit. The argument is held across the
// two steps in a scratch the emitter names (kParamArgMarker). False when the class has no lifecycle.
// The class's own `init` or `deinit` emitter (the forms §8.5 allows), or null.
static functionDef* lifecycleEmitter(classDef* cls, const string& name){
    for(typeMember* m : cls->members)
        if(auto* fn = dynamic_cast<functionDef*>(m))
            if(fn->isEmitter && fn->name == name && fn->params.empty() && dynamic_cast<i6Block*>(fn->body))
                return fn;
    return nullptr;
}

// The class's copying `init`: one parameter of the class's own type.
static functionDef* copyInitEmitter(classDef* cls){
    for(typeMember* m : cls->members)
        if(auto* fn = dynamic_cast<functionDef*>(m))
            if(fn->isEmitter && fn->name == "init" && fn->params.size() == 1
               && fn->params[0]->type.name == cls->name && dynamic_cast<i6Block*>(fn->body))
                return fn;
    return nullptr;
}

// An emitter class's value is the variable's own word, which I6 can't pass by address: a `ref`
// parameter of one is copied back to the argument after the call.
bool bglParser::refCopiesBack(const paramDef* p){
    if(p == nullptr || !p->isRef || p->type.name.find('<') != string::npos
       || isWordArrayType(p->type.name) || p->type.name == "bytearray") return false;
    classDef* cls = languageService.findClass(p->type.name);
    if(cls == nullptr || !cls->isEmitterClass || cls->isPrimitive || cls->isAlias) return false;
    return findMemberInHierarchy(cls, [](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        return fd && fd->isEmitter && (fd->name == "init" || fd->name == "deinit"); }) != nullptr;
}

string bglParser::refBackGlobal(size_t index){
    string name = format("_bglRefBack{0}", index);
    if(refBackGlobals.insert((int)index).second){
        auto* g = new variableDeclaration();
        g->name = g->displayName = name;
        g->type.name = "var";
        g->needsEarlyGlobalDecl = true;
        languageService.globals.insert(languageService.globals.begin(), g);
    }
    return name;
}

// The call's value, with its copied-back arguments set right after it. `&&` is the I6 operator with a
// fixed order: `_bglKeepTake(_bglKeepPut(call) && ((arg = _bglRefBackN) | 1))`. A call in the arguments
// finishes inside them, so one held value serves nested calls too.
string bglParser::wrapRefBacks(const string& callText){
    if(lastRefBacks.empty()) return callText;
    string assigns;
    for(i6RawNode* back : lastRefBacks){
        string t = back->text;
        if(!t.empty() && t.back() == ';') t.pop_back();
        assigns += " && ((" + t + ") | 1)";
        postInjections.erase(remove(postInjections.begin(), postInjections.end(), back), postInjections.end());
    }
    lastRefBacks.clear();
    if(!refKeepEmitted){
        i6RawNode* keep = new i6RawNode();
        keep->text = "Global _bglKeepV;\n[ _bglKeepPut v; _bglKeepV = v; rtrue; ];\n[ _bglKeepTake x; x = _bglKeepV; return x; ];\n";
        languageService.globals.insert(languageService.globals.begin(), keep);
        refKeepEmitted = true;
    }
    return "_bglKeepTake(_bglKeepPut(" + callText + ")" + assigns + ")";
}

bool bglParser::synthesizeLifecycleParamCopy(functionDef& funcDef, paramDef& p, classDef* cls){
    functionDef* initFn = lifecycleEmitter(cls, "init");
    functionDef* deinitFn = lifecycleEmitter(cls, "deinit");
    if(initFn == nullptr || deinitFn == nullptr) return false;
    // A copying `init` takes the argument itself, so it can keep it safe while the parameter's
    // instance is acquired.
    if(functionDef* copyInit = copyInitEmitter(cls)){
        emitterBindings b; b.self = p.name; b.val = p.name; b.selfType = p.type.name; b.trim = emitterTrim::wsSemi;
        b.fn = copyInit; b.args.push_back(kParamArgMarker);
        p.isClassParamWithBacking = true;
        p.copyInText = format("{0} = {1}; {2};", kParamArgMarker, p.name,
                              expandEmitterBody(dynamic_cast<i6Block*>(copyInit->body), b));
        b = emitterBindings(); b.self = p.name; b.val = p.name; b.selfType = p.type.name; b.trim = emitterTrim::wsSemi;
        funcDef.cleanups.push_back({p.name, expandEmitterBody(dynamic_cast<i6Block*>(deinitFn->body), b) + ";"});
        return true;
    }
    functionDef* op = findAssignOperator(cls, p.type.name, [](functionDef* f){
        return !f->isEmitter || dynamic_cast<i6Block*>(f->body) != nullptr; }, /*allowInherited*/false);
    if(op == nullptr){
        parsingError(format("Parameter '{0}' of '{1}' can't be copied: the class has no copy operator "
                            "('operator =' taking '{1}'). Declare one, or make the parameter 'ref'.",
                            p.name, typeDisplayName(p.type.name)));
        return true;
    }
    auto expand = [&](functionDef* fn, const string& arg){
        emitterBindings b; b.self = p.name; b.val = p.name; b.selfType = p.type.name; b.trim = emitterTrim::wsSemi;
        if(!arg.empty()){ b.fn = fn; b.args.push_back(arg); }
        string text = expandEmitterBody(dynamic_cast<i6Block*>(fn->body), b);
        for(size_t at = text.find("$target"); at != string::npos; at = text.find("$target", at + p.name.size()))
            text.replace(at, 7, p.name);   // the parameter is the assignment's target
        return text + ";";
    };
    if(!op->isEmitter && op->i6name.empty())
        op->i6name = mangleOperatorName(op->name);
    const string arg = kParamArgMarker;
    string copy = op->isEmitter ? expand(op, arg) : format("{0}.{1}({2});", p.name, op->i6name, arg);
    p.isClassParamWithBacking = true;
    p.copyInOperator = op;
    p.copyInText = format("{0} = {1}; {2} {3}", arg, p.name, expand(initFn, ""), copy);
    funcDef.cleanups.push_back({p.name, expand(deinitFn, "")});
    return true;
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

    // A class-typed parameter owns an instance unless it is `ref`, like any class-typed variable, and
    // the argument is assigned to it through the class's `operator =`. The root assignment makes the
    // parameter refer to the argument, which it already does, so only a class with a copying
    // `operator =` needs an instance: the argument is copied into it at entry.
    for(size_t pi = 0; pi < funcDef.params.size(); pi++){
        paramDef* p = funcDef.params[pi];
        // A `ref` to a value held in the word itself: the routine hands the word back on every exit.
        if(refCopiesBack(p)) funcDef.cleanups.push_back({p->name, refBackGlobal(pi) + " = " + p->name + ";"});
        if(p->isRef) continue;
        classDef* cls = languageService.findClass(p->type.name);
        // Only an emitter class manages its own storage: a reference class's parameter shares, and a value
        // class's gets a backing instance (below), so `init` there is no allocation to copy into.
        if(cls && cls->isEmitterClass && synthesizeLifecycleParamCopy(funcDef, *p, cls)) continue;
        if(!cls || cls->isEmitterClass || cls->isAlias || cls->isExternal || !isValueClass(cls)) continue;   // a reference: shares
        functionDef* assignOp = findAssignOperator(cls, p->type.name, [](functionDef*){ return true; }, /*allowInherited*/true);
        if(!assignOp){
            parsingError(format("Parameter '{0}' of value class '{1}' can't be copied: the class has no copy operator "
                                "('operator =' taking '{1}'). Declare one, or make the parameter 'ref'.",
                                p->name, typeDisplayName(p->type.name)));
            continue;
        }
        // Overloads of `=` are named by their parameter type; name the set before taking this one's name.
        mangleOverloadSetForReceiver(cls->name, assignOp->name);
        if(!assignOp->isEmitter && assignOp->i6name.empty())
            assignOp->i6name = mangleOperatorName(assignOp->name);
        // Synthesize a global I6 object backing. Name includes the class context (if any)
        // so same-method-name across different classes doesn't collide:
        //   top-level fn `f` with param `p` → `_bglParam_f_p`
        //   class `Foo` method `f` with param `p` → `_bglParam_Foo_f_p`
        // An operator's name is its symbol, so it goes by its mangled routine name (`==` → `_opeqeq`);
        // overloads share a name, so a repeat gets a numeric suffix.
        bool isOperator = funcDef.name == "operator()"
            || (!funcDef.name.empty() && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_');
        string fnPart = isOperator ? mangleOperatorName(funcDef.name) : funcDef.name;
        string backingName = classContext.empty()
            ? "_bglParam_" + fnPart + "_" + p->name
            : "_bglParam_" + classContext + "_" + fnPart + "_" + p->name;
        if(!paramBackingNames.insert(backingName).second){
            int n = 2;
            while(!paramBackingNames.insert(backingName + "_" + to_string(n)).second) n++;
            backingName += "_" + to_string(n);
        }
        variableDeclaration* backing = new variableDeclaration();
        backing->name = backingName;
        backing->displayName = backingName;
        backing->type = p->type;
        backing->src = funcDef.src;
        backing->isInstanceBacking = true;
        languageService.registerInstance(*backing);
        p->backingName = backingName;
        p->isClassParamWithBacking = true;
        funcDef.ownBackings.push_back({backingName, p->type.name});
        p->copyInOperator = assignOp;
        // The copy-in, run at entry: the argument (still in the parameter) is copied into the
        // parameter's own instance, then the parameter is pointed at it.
        if(assignOp->isEmitter){
            emitterBindings cb; cb.self = backingName; cb.val = backingName;
            cb.fn = assignOp; cb.args.push_back(p->name); cb.trim = emitterTrim::wsSemi;
            string body = expandEmitterBody(dynamic_cast<i6Block*>(assignOp->body), cb);
            for(size_t at = body.find("$target"); at != string::npos; at = body.find("$target", at + backingName.size()))
                body.replace(at, 7, backingName);
            p->copyInText = body + ";";
        } else {
            p->copyInText = format("{0}.{1}({2});", backingName, assignOp->i6name, p->name);
        }
        // The instance's lifecycle, as for a local of the class: `init` before the copy-in, `deinit`
        // on every exit.
        auto hook = [&](functionDef* fn, const string& self){
            emitterBindings b; b.self = self; b.val = self; b.selfType = p->type.name; b.trim = emitterTrim::wsSemi;
            return expandEmitterBody(dynamic_cast<i6Block*>(fn->body), b) + ";";
        };
        if(functionDef* initFn = lifecycleEmitter(cls, "init"))
            p->copyInText = hook(initFn, backingName) + " " + p->copyInText;
        if(functionDef* deinitFn = lifecycleEmitter(cls, "deinit"))
            funcDef.cleanups.push_back({p->name, hook(deinitFn, p->name)});
    }
}

// True when the two take the same parameter types: one overrides the other.
bool sameParamTypes(functionDef* a, functionDef* b){
    if(a->params.size() != b->params.size()) return false;
    for(size_t i = 0; i < a->params.size(); i++)
        if(a->params[i]->type.name != b->params[i]->type.name) return false;
    return true;
}

// An override is the same I6 property as the method it overrides, so it keeps that method's name
// whichever of the two an overload set mangled.
void bglParser::syncOverrideNames(){
    function<functionDef*(classDef*, functionDef*)> overridden = [&](classDef* c, functionDef* fd) -> functionDef* {
        for(classDef* b : c->baseClasses){
            for(typeMember* m : b->members)
                if(auto* bf = dynamic_cast<functionDef*>(m); bf && bf->name == fd->name && !bf->isEmitter && !bf->isStatic
                   && !bf->isPrePassStub && sameParamTypes(bf, fd))
                    return bf;
            if(functionDef* f = overridden(b, fd)) return f;
        }
        return nullptr;
    };
    for(typeDef* g : languageService.globals)
        if(auto* cd = dynamic_cast<classDef*>(g); cd && !cd->baseClasses.empty())
            for(typeMember* m : cd->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && !fd->isEmitter && !fd->isStatic && !fd->isPrePassStub
                   && fd->name.rfind("operator", 0) != 0 && isalpha((unsigned char)(fd->name.empty() ? '_' : fd->name[0])))
                    if(functionDef* up = overridden(cd, fd)) fd->i6name = up->i6name;
}

void bglParser::mangleOverloadSetForReceiver(const string& receiverTypeName, const string& methodName){
    if(receiverTypeName.empty() || methodName.empty()) return;
    // Strip a generic suffix on the receiver type name (`array<int>` → `array`) since
    // overload disambiguation runs on the outer type. Mirrors what bindMethodCall does
    // for templated receivers.
    string base = receiverTypeName;
    auto lt = base.find('<');
    if(lt != string::npos) base = base.substr(0, lt);
    // Settle the set wherever the method is declared: an object's own members, its class and the class's
    // ancestors, then its class's line
    // (an object declared as its own type inherits its methods from there).
    typeDef& td = languageService.getType(base);
    if(auto* od = dynamic_cast<objectDef*>(&td)) mangleOverloadSet(od->members, methodName);
    set<classDef*> seen;
    function<void(classDef*)> walk = [&](classDef* c){
        if(c == nullptr || !seen.insert(c).second) return;
        mangleOverloadSet(c->members, methodName);
        for(classDef* b : c->baseClasses) walk(b);
    };
    walk(languageService.classOf(base));
    mangleClassFamilyOverloadSet(languageService.classOf(base), methodName);
    syncOverrideNames();
}

// A method shares its I6 property with the same name along its class's line (not its siblings, unless a
// class derives from both), so an overload added anywhere along that line is told apart from the rest. The
// library's classes are shared by every program class, so the line does not spread down through them.
// `create`, `destroy` and `recreate` are Inform 6's own class messages and keep their names.
void bglParser::mangleClassFamilyOverloadSet(classDef* cls, const string& methodName){
    if(cls == nullptr || methodName.empty()) return;
    if(methodName == "create" || methodName == "destroy" || methodName == "recreate") return;
    // An operator is told apart by its operand types; copying (`=`), conversions and `?` keep their names,
    // which the runtime and the library send directly.
    bool isOperator = !isalpha((unsigned char)methodName[0]) && methodName[0] != '_';
    if(isOperator && (methodName == "=" || methodName == "?" || methodName == "operator()" || methodName == "()")) return;
    auto isLibrary = [](classDef* c){ return c->baseClasses.empty() || c->src.file.find("beguiLib") != string::npos; };
    auto ancestors = [&](classDef* c){
        set<classDef*> out; vector<classDef*> work(c->baseClasses.begin(), c->baseClasses.end());
        while(!work.empty()){ classDef* x = work.back(); work.pop_back();
            if(x && out.insert(x).second) for(classDef* b : x->baseClasses) work.push_back(b); }
        return out;
    };
    auto descendants = [&](classDef* c){
        set<classDef*> out;
        if(isLibrary(c)) return out;
        vector<classDef*> work{c};
        while(!work.empty()){ classDef* x = work.back(); work.pop_back();
            for(typeDef* t : languageService.objectTypes)
                if(auto* d = dynamic_cast<classDef*>(t))
                    if(find(d->baseClasses.begin(), d->baseClasses.end(), x) != d->baseClasses.end() && out.insert(d).second)
                        work.push_back(d); }
        return out;
    };
    auto methodsIn = [&](classDef* c){
        vector<functionDef*> out;
        for(typeMember* m : c->members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == methodName && !fd->isEmitter && !fd->isStatic) out.push_back(fd);
        return out;
    };
    // A class's line: its ancestors, its descendants, and their other parents (one property per name
    // in a class with two parents).
    auto lineOf = [&](classDef* c){
        set<classDef*> line = ancestors(c), below = descendants(c);
        for(classDef* d : below){ set<classDef*> up = ancestors(d); line.insert(up.begin(), up.end()); }
        line.insert(below.begin(), below.end());
        line.erase(c);
        return line;
    };
    set<classDef*> family = lineOf(cls);
    family.insert(cls);
    for(classDef* x : family){
        vector<functionDef*> mine = methodsIn(x);
        if(mine.empty()) continue;
        set<classDef*> line = lineOf(x);
        for(functionDef* fd : mine){
            if(fd->params.empty() && methodName == "print") continue;
            bool differs = false;
            for(classDef* y : line) for(functionDef* other : methodsIn(y))
                if(mangleObjectMethodName(other) != mangleObjectMethodName(fd)) differs = true;
            if(!differs) continue;
            if(isOperator){
                string disc;
                for(paramDef* p : fd->params){
                    string t = p->type.name;
                    if(size_t lt = t.find('<'); lt != string::npos) t = t.substr(0, lt);
                    disc += "_" + (t.empty() ? string("var") : t);
                }
                fd->i6name = mangleOperatorName(fd->name) + disc;
            }
            else if(fd->i6name.empty() || fd->i6name.rfind("_m_", 0) == 0) fd->i6name = mangleObjectMethodName(fd);
        }
    }
}

void bglParser::detectInfModeI6Collisions(){
    if(languageService.infHeader.empty() && languageService.infTrailer.empty()) return;  // not .inf-mode

    // Collect all Beguile-declared global names (lowercased) for cross-check.
    std::set<std::string> beguileNames;
    for(typeDef* g : languageService.globals){
        if(dynamic_cast<i6RawNode*>(g)) continue;  // raw I6 nodes aren't named decls
        // An `extern` names an I6 symbol on purpose (the library binds NO_ATTRIBUTE this way).
        if(auto* vd = dynamic_cast<variableDeclaration*>(g); vd && vd->isExternal) continue;
        if(auto* fd = dynamic_cast<functionDef*>(g); fd && (fd->isExternal || fd->isEmitter)) continue;
        if(auto* od = dynamic_cast<objectDef*>(g); od && od->isExternal) continue;
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
    includedFilePaths.insert(absPath);  // record coverage for the LSP entry-point context check
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

        // Restore the define table to its pre-scan seed (version + target symbols only), dropping
        // every source `#define` the pre-scan accumulated. The main pass re-derives those defines
        // linearly as it re-reads the file(s), so `#if` is evaluated with position-dependent
        // semantics identical to the pre-scan — a `#define` (or `#undef`) affects only the code
        // that follows it, matching standard preprocessor behavior. Without this, the main pass
        // would inherit the pre-scan's end-of-file state and a late `#define` could retroactively
        // enable an earlier `#if`. Done before the BLR load so the BLR is parsed with the same
        // clean seed the pre-scan used.
        if(hasPreScanSeed) definedSymbols = definedSymbolsPreScanSeed;
        // Hoist #declare'd symbols on top of the clean seed. Unlike #define (linear), these are
        // order-independent: the pre-scan collected every #declare across the whole include graph,
        // so they are visible to `#if` from the very first line of the main pass — even ones declared
        // in files included later. See declaredSymbols.
        for(auto& kv : declaredSymbols) definedSymbols[kv.first] = kv.second;

        // Always load the Beguile Language Runtime first so built-in types (eBool, eTarget, etc.)
        // and emitters are available to user code — including #bgl{} blocks inside .inf files.
        // Skip when the entry file IS the BLR core (e.g. the LSP parsing __beguileCore.bgl while
        // the user edits it): re-loading it here would parse the same file twice, double-register
        // every core type ('object'/'bgl'/… "already defined"), and derail. The entry parse below
        // registers them once.
        // Mark the entry file as already-included BEFORE loading core. If the entry file is itself
        // a core library file that core pulls back in via #include (e.g. the LSP parsing the editor
        // buffer of core/_ui.bgl while the user edits it — __beguileCore does `#include <core/_ui>`),
        // the disk copy is then skipped by #once, leaving the entry buffer as the single authoritative
        // definition. Without this every symbol in an edited core file reports a spurious "already
        // defined" against itself. The `systemAbs != absPath` guard below handles the narrower case of
        // editing __beguileCore.bgl directly (skip re-loading core wholesale).
        onceFiles.insert(absPath);
        filesystem::path systemPath = filesystem::path(settings.libPath) / "core" / "__beguileCore.bgl";
        string systemAbs;
        try { systemAbs = filesystem::canonical(systemPath).string(); } catch(...) { systemAbs = systemPath.string(); }
        if(systemAbs != absPath)
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
    size_t conditionalBase = openConditionals.size();
    try{
        while(processNextStatement()==false){
            //cout<<"Block processed."<<endl;
        }
    } catch(lspRecoverySignal&){
        // LSP error recovery: unwind back to top-level scope, then continue parsing the next
        // top-level declaration. Uses file.braceDepth (maintained by the lexer) to know exactly
        // how deeply nested the parser was when the error fired, so we consume the right number
        // of closing braces to reach global scope — not just the nearest '}'. This matters for
        // errors inside nested function bodies (e.g. `class Foo { void m(){ bad_stmt; } }`):
        // without the full unwind, recovery stays stuck at mid-file depth and silently drops
        // every subsequent top-level declaration.
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
        openConditionals.resize(min(openConditionals.size(), conditionalBase));
    }
    if(openConditionals.size() > conditionalBase){
        int line = openConditionals[conditionalBase].line;
        openConditionals.resize(conditionalBase);
        parsingError(format("The '#if' at line {0} has no '#endif'.", line));
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
// Grammar-driven pattern matching (parser dispatcher)
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
    { return processClassDeclaration(t[0], q.isExtern || q.isPrimitive, q.isExtend, q.isEmitter || q.isPrimitive, q.isAlias, t[1],
                                     /*allowNested*/false, q.isSuperposed, q.isValue, q.isPrimitive); }
bool bglParser::processGrammar(vector<token>& t, Qualifiers&, abstractObject&)
    { return processGrammarDeclaration(t[1]); }
bool bglParser::processArray(vector<token>& t, Qualifiers& q, abstractObject& c)
    { return processArrayDeclarationFromGeneric(t[0], q, c); }
bool bglParser::processRoutine(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processRoutineDeclaration(t[0], t[1], c, q.isExtern, q.isEmitter, q.isReplace, q.isDefault, q.isSuperposed); }
bool bglParser::processObject(vector<token>& t, Qualifiers& q, abstractObject&)
    { t[0] = consumeTypeToken(t[0]); return processObjectDeclaration(t[0], t[1], q.isExtern, "", "", true, q.isEmitter, q.isSuperposed); }
// Standalone inline object declaration statement — `Type{ … };` with no name and no assignment.
// The matcher has already consumed the type and the '{'. Bake an anonymous object (§6.2.1); the
// reference is discarded, so the object is only reachable if it self-links (e.g. a positional
// `parent`) — otherwise it is an intentional, unreferenced baked object.
bool bglParser::processInlineObjectStatement(vector<token>& t, Qualifiers&, abstractObject& ctx)
    {
        t[0] = consumeTypeToken(t[0]);
        classDef* cls = getDispatchClass(t[0].value);
        if(cls == nullptr)
            parsingError(format("'{0}{{ … }}': '{0}' is not a class type", t[0].value));
        functionDef* func = dynamic_cast<functionDef*>(&ctx);
        statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;
        string anonName = format("_bglanon{0}", anonObjectCounter++);
        bakeInlineObjectAggregate(cls, t[0].value, anonName, func, body);   // '{' already consumed by the matcher
        file.getToken(token::endStatement);                                 // consume trailing ';'
        return false;
    }
bool bglParser::processVariable(vector<token>& t, Qualifiers& q, abstractObject& c)
    {
        t[0] = consumeTypeToken(t[0]);
        // `[additive] property name;` — a property declaration is TYPE-LESS; see
        // processTypedProperty, which rejects the typed forms and states the rule.
        return processVariableDeclaration(t[0], t[1], t[2], c, q.isExtern, q.isConst, "", q.isRef, q.isSuperposed, q.isAdditive);
    }
// Grammar handler for the typed property forms, which are rejected. Entered after
// "property" <base> "<" have been consumed by the matcher.
bool bglParser::processTypedProperty(vector<token>& t, Qualifiers& q, abstractObject& c)
    {
        // A property declaration is TYPE-LESS: the type lives at the use sites (each contributing
        // member's own declaration), not on the property. For an ADDITIVE property, every
        // contributing member must be a `rawArray<T>` with a consistent element type across the
        // hierarchy (enforced in checkTypedPropertyMemberTypes); the element type is fixed by the
        // highest ancestor that declares it, and inferred from the contribution's elements
        // otherwise. Two shapes reach here, both illegal:
        //   `property rawArray<T> name;`  (matcher consumed through the '<')
        //   `property T name;`            (matcher consumed the name)
        // Consume the rest of the statement so error recovery (LSP) resyncs cleanly, then reject.
        while(!file.peekToken().is(token::endStatement) && !file.peekToken().is(eTokenType::eof))
            file.getToken();
        if(file.peekToken().is(token::endStatement)) file.getToken();
        parsingError("a property declaration no longer specifies a type. Write "
            "`[additive] property <name>;` — the type is set by each use site, not the property. "
            "For an additive property, every contributing member must be a `rawArray<T>` with a "
            "consistent element type across the hierarchy.");
        return false;
    }
bool bglParser::processTypedObject(vector<token>& t, Qualifiers& q, abstractObject& c)
    { t[0] = consumeTypeToken(t[0]); return processTypedObjectDeclaration(t[0], t[1], t[3], q, c); }
bool bglParser::processAliased(vector<token>& t, Qualifiers& q, abstractObject& c){
    token nameTok = t[1];
    string i6name = applyI6NameClause(nameTok, t[3], t[2].is("asbgl"), q.isExtern);
    token i6Tok = t[3]; i6Tok.value = i6name; i6Tok.originalValue = i6name;
    return processAliasedDeclaration(t[0], nameTok, i6Tok, q, c);
}

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
        if(tt.is(eTokenType::identifier)) tt = consumeTypeToken(tt);   // an alias or dotted type path
        string typeName = tt.value;
        if(typeName == "func") typeName = parseFuncType();  // nested func<>
        else if(typeName == "array" || typeName == "rawarray") typeName = parseArrayTypeTail(typeName);
        typeName = maybeParseUnionTail(typeName);   // func<A | B, …>, as array<A | B>
        if(!first) result += ",";
        result += typeName;
        first = false;
        token sep = file.getToken();
        if(sep.is(">")) break;
        if(sep.value == ">>") parsingError("Unbalanced '>>' closing a func<...> type");
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
    string i6alias = parseI6NameClause(name, q.isExtern);
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, name, ctx, q.isExtern, q.isEmitter, q.isReplace, q.isDefault, q.isSuperposed);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, name, q.isExtern, objectClassName, i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, name, symbol, ctx, q.isExtern, q.isConst, i6alias, q.isRef, q.isSuperposed);
}

// See bglParser.h. `asI6` adds an I6 spelling to a Beguile declaration; `alias` names an existing
// I6 symbol for Beguile. Tying each to one side of `extern` keeps a single reading: the DECLARED
// name is always the name in the language that defines the thing, and the clause names it in the
// other one.
string bglParser::parseI6NameClause(token& nameTok, bool isExtern){
    bool isAsI6  = file.peekToken().is("asi6");
    bool isAsBgl = file.peekToken().is("asbgl");
    if(!isAsI6 && !isAsBgl) return "";
    file.getToken();                                    // the clause keyword
    token other = file.getToken({eTokenType::identifier, eTokenType::dataType});
    return applyI6NameClause(nameTok, other, isAsBgl, isExtern);
}

// See bglParser.h. `asI6` CREATES the name Inform 6 will get, so it belongs on a declaration
// Beguile defines; `asBgl` NAMES a symbol Inform 6 already defines, so it belongs on an `extern`.
// Tying each to one side of `extern` keeps a single reading: the DECLARED name is always the name
// in the language that defines the thing, and the clause names it in the other one.
string bglParser::applyI6NameClause(token& nameTok, const token& otherTok, bool isAsBgl, bool isExtern){
    string otherName = otherTok.originalValue.empty() ? otherTok.value : otherTok.originalValue;
    string declared  = nameTok.originalValue.empty()  ? nameTok.value  : nameTok.originalValue;
    if(!isAsBgl){
        if(isExtern){
            parsingError(format("'asI6' names the Inform 6 symbol to emit, so it cannot apply to an "
                                "'extern' declaration — that adopts a symbol Inform 6 already "
                                "defines. Write `extern … {0} asBgl {1};` to give '{0}' the Beguile "
                                "name '{1}'.", declared, otherName));
            return "";
        }
        return otherName;
    }
    if(!isExtern){
        parsingError(format("'asBgl' names a symbol Inform 6 already defines, so it needs 'extern'. "
                            "Write `{0} asI6 {1};` to choose the name '{0}' is emitted under.",
                            declared, otherName));
        return "";
    }
    // `extern <type> <i6name> asBgl <beguileName>` — the declared token IS the I6 symbol; the clause
    // supplies the name Beguile source uses. Register under the Beguile name, emit under the I6 one.
    nameTok.value = otherName;
    transform(nameTok.value.begin(), nameTok.value.end(), nameTok.value.begin(), ::tolower);
    nameTok.originalValue = otherName;
    return declared;
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
    size_t postBefore = postInjections.size();
    bool r = processStatementDispatchInner(tok, contextObject);
    // What a statement leaves to run after it (a `?.` guard's closing brace) follows it, not the next one.
    if(postInjections.size() > postBefore)
        if(auto* f = dynamic_cast<functionDef*>(&contextObject))
            if(auto* b = dynamic_cast<statementBlock*>(f->body)){
                for(size_t i = postBefore; i < postInjections.size(); i++) b->statements.push_back(postInjections[i]);
                postInjections.resize(postBefore);
            }
    return r;
}

bool bglParser::processStatementDispatchInner(token tok, abstractObject& contextObject) {
    if(!grammarInitialized) initGrammarTable();
    sourceLocation stmtLoc = file.currentLocation();
    struct stmtExprScope {
        int& base; bool& open; int savedBase; bool savedOpen;
        stmtExprScope(int& b, bool& o, int depth) : base(b), open(o), savedBase(b), savedOpen(o) { base = depth; open = false; }
        ~stmtExprScope(){ base = savedBase; open = savedOpen; }
    } exprScope(stmtExprBase, optStringMarkOpen, exprParseDepth);
    // A routine's top-level statement starts its chain temporaries afresh; nested ones keep counting.
    if(currentFunc != nullptr && !activeBlockStack.empty() && activeBlockStack.back() == currentFunc->body)
        currentFunc->nextChainTemp = 0;
    // Every statement, keyword-led ones included, places its errors from its own first token.
    if(tok.src.line > 0){
        currentStatementSrc = tok.src;
        currentStatementSrc.col = max(1, tok.src.col - (int)tok.originalValue.size());
    }

    // Early exits
    if(tok.is(token::braceClose) && getCurrentCompileContext() == eCompileContext::global && !looseIdentifierMode)
        parsingError("Unexpected '}' at file scope: it closes nothing.");
    if(tok.is(token::braceClose)) return true;
    if(tok.is(token::endStatement)) return false;   // an empty statement (`default: ;`, `if (c) ;`)
    // `::name[i] …`: the global array, written as the escape the emitter resolves past a same-named local.
    if(tok.value.size() > 2 && tok.value.rfind("::", 0) == 0 && tok.value.find('.') == string::npos
       && file.peekToken().is(token::bracketOpen)){
        if(functionDef* f = currentFunc) f->globalEscapes.insert(tok.value.substr(2));
        tok.value = kGlobalEscapeMarker + tok.value.substr(2);
    }
    if(tok.is("self") && getCurrentCompileContext() == eCompileContext::codeBlock)
        rejectSelfWithoutObject(dynamic_cast<functionDef*>(&contextObject));
    if(getCurrentCompileContext() == eCompileContext::codeBlock && !looseIdentifierMode){
        token nx = file.peekToken();
        if((tok.is("if") || tok.is("while") || tok.is("switch") || tok.is("for")) && !nx.is(token::parenOpen))
            parsingError(format("'{0}' takes its condition in parentheses: `{0} (…)`.", tok.value));
        string shown = tok.originalValue.empty() ? tok.value : tok.originalValue;
        // `Box::hello()`: the lexer glues `::` onto the name that follows.
        if(tok.is(eTokenType::dataType) && nx.value.rfind("::", 0) == 0 && nx.value.size() > 2)
            parsingError(format("'{0}::{1}' — a class's member is reached with '.': `{0}.{1}`.", shown, nx.value.substr(2)));
    }
    if(tok.is("outer") && file.peekToken().is(token::period) && getCurrentCompileContext() == eCompileContext::codeBlock)
        if(auto* f = dynamic_cast<functionDef*>(&contextObject))
            rejectStrayOuter(f, dynamic_cast<statementBlock*>(f->body));
    // A local or parameter named like a class (a library's `int win` beside a program's `class win`)
    // lexes as a type; at the start of a statement, followed by an assignment, a member access, a
    // subscript, a call or ++/--, it is the variable.
    if(tok.is(eTokenType::dataType) && getCurrentCompileContext() == eCompileContext::codeBlock)
        if(auto* f = dynamic_cast<functionDef*>(&contextObject)){
            auto* fb = dynamic_cast<statementBlock*>(f->body);
            token nx = file.peekToken();
            bool usesVariable = nx.is(token::assignment) || nx.is(token::period) || nx.is(token::bracketOpen)
                || nx.is(token::parenOpen) || nx.is("++") || nx.is("--")
                || (nx.is(eTokenType::oper) && nx.value.size() >= 2 && nx.value.back() == '=' && nx.value != "==" && nx.value != "!=");
            if(usesVariable && (qualifyFromParams(tok.value, f) || qualifyFromBodyLocals(tok.value, fb)
                                || qualifyFromAncestorBlocks(tok.value, fb)))
                tok.tokenType = eTokenType::identifier;
        }
    // A bare block (§5.2): a scope of its own, emitted as an always-taken `if` so its locals are
    // released at its closing brace as any other block's are.
    if(tok.is(token::braceOpen) && getCurrentCompileContext() == eCompileContext::codeBlock){
        if(auto* func = dynamic_cast<functionDef*>(&contextObject)){
            auto* blockIf = new ifStatement();
            blockIf->src = stmtLoc;
            blockIf->condition = new expression();
            blockIf->condition->tokens.push_back("true");
            blockIf->condition->resolvedType = "bool";
            blockIf->thenBlock = new statementBlock();
            functionDef blockCtx;
            blockCtx.returnType = func->returnType; blockCtx.params = func->params;
            blockCtx.body = blockIf->thenBlock;
            openCompileContext(eCompileContext::codeBlock, blockIf->thenBlock);
            while(processNextStatement(blockCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
            if(auto* body = dynamic_cast<statementBlock*>(func->body)) body->statements.push_back(blockIf);
            return false;
        }
    }
    if(tok.is(eTokenType::eof)) {
        if(getCurrentCompileContext() == eCompileContext::codeBlock)
            parsingError(file.braceLines.empty() ? string("Unexpected end of file — missing closing '}'")
                : format("The '{{' at line {0} is never closed: the file ends before its '}}'.", file.braceLines.back()));
        return true;
    }
    // Directives flow through the grammar table (each #xxx is a rule).

    // Parse qualifiers
    Qualifiers q = parseQualifiers(tok);
    if(q.isExplicit && getCurrentCompileContext() == eCompileContext::global)
        parsingError("'explicit' is only valid inside a class or object body");
    if(q.isStatic && getCurrentCompileContext() == eCompileContext::global)
        parsingError("'static' is only valid inside a class body");
    if(getCurrentCompileContext() == eCompileContext::codeBlock && !looseIdentifierMode){
        if(q.isStatic)
            parsingError("'static' doesn't apply to a local: a local lasts one call. Keep a value between calls "
                         "in a global or a static member.");
        if(q.isRequired)
            parsingError("'required' applies to a class or object member, not a local.");
    }

    // Static member access: ClassName.member — route to processStatement
    // Object instances need no special handling here — they're lexed as identifiers.
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

    // extend enum X { ... } — append members to an existing enum/bnum. Must precede the object
    // extension catch below (after `extend`, `enum`/`bnum` arrives as a bare identifier).
    if(q.isExtend && !q.isExtern && (tok.is(token::enumDeclaration) || tok.is(token::bnumDeclaration) || tok.value == "enum" || tok.value == "bnum"))
        return processEnumDeclaration(tok, false, token(), /*isExtend*/true);

    // `extend <unionName> { … }` — attach members to a named union. It's an emitter class under
    // the hood, so route to the class-extend path (which appends members) rather than the object one.
    if(q.isExtend && !q.isExtern && (tok.is(eTokenType::identifier) || tok.isDataType()) && !tok.is(token::classDeclaration)){
        auto* uc = languageService.findClass((string)tok);
        if(uc && uc->isUnion())
            return processClassDeclaration(tok, false, /*isExtend*/true, /*isEmitterClass*/true, false, tok);
    }
    if(q.isExtend && !q.isExtern && (tok.is(eTokenType::identifier) || tok.isDataType()) && !tok.is(token::classDeclaration))
        return processObjectExtension(tok);

    if(!q.isExtern && tok.is("grammar")) return processGrammarDeclaration();

    // Named union declaration: `union Name = A | B [ { members } ]`. `union` is a soft keyword
    // (handled here like `grammar`), so it stays usable as an identifier elsewhere. It takes no
    // qualifiers — a qualifier before it (`alias union`, `static union`, …) is not a union
    // declaration and falls through to the generic error path, exactly like any bogus construct.
    // `extend <unionName> { … }` is routed via the extend path below.
    if(!q.anySet() && tok.is("union")) return processUnionDeclaration(q);

    // Namespace-scoped type path: identifier.identifier...identifier resolving to a type.
    // Consume the dotted path, resolve to a flat type, then re-dispatch with the synthetic token.
    if(tok.is(eTokenType::identifier) && file.peekToken().is(token::period) && isNamespacedTypePath(tok)){
        tok = consumeTypeToken(tok);
        // tok is now a synthetic dataType — fall through to grammar matching with the resolved type
    }

    // #using type import: if tok is an identifier that matches an alias member (isAlias=true)
    // of any imported scope, resolve it to the flat type name.
    // e.g., after `#using bgl.glulx;`, bare `window` resolves to `glulxWindow`.
    // A parameter or local of that name is the variable (imports are the lowest tier, §10.4).
    bool namesLocal = false;
    if(tok.is(eTokenType::identifier) && getCurrentCompileContext() == eCompileContext::codeBlock)
        if(auto* f = dynamic_cast<functionDef*>(&contextObject))
            namesLocal = qualifyFromParams(tok.value, f) || qualifyFromBodyLocals(tok.value, dynamic_cast<statementBlock*>(f->body))
                      || qualifyFromAncestorBlocks(tok.value, dynamic_cast<statementBlock*>(f->body));
    // So is a member of the class or object being compiled.
    if(!namesLocal && tok.is(eTokenType::identifier) && getCurrentCompileContext() == eCompileContext::codeBlock){
        classDef* cls = currentClass != nullptr ? currentClass : currentObject != nullptr ? currentObject->objectClass : nullptr;
        auto realMember = [&](typeMember* m){   // an alias member is a type name, not a member to reach
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return m->name == tok.value && !(vd && vd->isNamespaceAlias());
        };
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members) if(realMember(m)){ namesLocal = true; break; }
        if(!namesLocal && cls != nullptr)
            namesLocal = cls->findMember(realMember) != nullptr;
    }
    if(tok.is(eTokenType::identifier) && !tok.isDataType() && !namesLocal){
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

    // Union type at a declaration head: `A | B | ... name`. A '|' immediately after a type name
    // is only ever a union (a type name is not a valid expression operand), so collapse the
    // '|'-separated members into ONE synthetic dataType token carrying the canonical union name.
    // The grammar's `dataType identifier …` declaration rules then match unchanged, so this one
    // choke point gives unions to locals, return types, members, and typed object declarations.
    // A func<…>/array<…> first member is recognized by matching its angle brackets ahead.
    if(!q.isExtern && tok.isDataType()){
        bool generic = tok.value == "func" || tok.value == "array" || tok.value == "rawarray";
        if(!generic && file.peekToken().value == "|")
            tok.value = maybeParseUnionTail(tok.value);
        else if(generic && genericTypeIsUnionHead()){
            string first;
            if(tok.value == "func") first = parseFuncType();
            else {
                first = parseArrayTypeTail(tok.value);
                if(first == "array<char>" || first == "array<charliteral>") first = "bytearray";
            }
            tok.value = maybeParseUnionTail(first);
        }
    }

    // `extend class Ns.Part { … }`: a type alias reached through an object names its class.
    if(q.isExtend && tok.is(token::classDeclaration) && file.peekToken(2).is(token::period)){
        token head = file.getToken();
        string path = head.originalValue.empty() ? head.value : head.originalValue;
        objectDef* od = languageService.findGlobalAs<objectDef>(head.value);
        string cls;
        while(od != nullptr && file.peekToken().is(token::period)){
            file.getToken();
            token part = file.getToken({eTokenType::identifier, eTokenType::dataType});
            path += "." + (part.originalValue.empty() ? part.value : part.originalValue);
            variableDeclaration* alias = nullptr;
            for(typeMember* m : od->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == part.value && vd->isNamespaceAlias()) alias = vd;
            od = nullptr;
            if(alias == nullptr) break;
            if(alias->isAlias){ cls = alias->type.name; break; }
            od = languageService.findGlobalAs<objectDef>(alias->type.name);
        }
        classDef* target = cls.empty() ? nullptr : languageService.findClass(cls);
        if(target == nullptr || file.peekToken().is(token::period))
            parsingError(format("extend class '{0}': the path doesn't name a class.", path));
        head.value = target->name;
        head.originalValue = target->dName();
        head.tokenType = eTokenType::dataType;
        file.pendingToken = head;
        file.hasPendingToken = true;
    }

    // Try grammar-driven matching
    GrammarMatch match = matchGrammar(tok);

    if(match.success && match.handler) {
        return (this->*match.handler)(match.matchedTokens, q, contextObject);
    }

    // `int a, b;`: one variable per declaration.
    if(tok.isDataType() && (file.peekToken(1).is(eTokenType::identifier) || file.peekToken(1).is(eTokenType::dataType))
       && file.peekToken(2).value == ",")
        parsingError(format("A declaration names one variable: write `{0} {1}; {0} …;` rather than a list.",
                            tok.originalValue.empty() ? tok.value : tok.originalValue, file.peekToken(1).value));
    // ── FALLTHROUGH: grammar match failed ──
    // All declaration, statement, and directive patterns are in the grammar table.
    // Anything reaching here is either an expression statement (code block) or an error (global).

    if(getCurrentCompileContext() == eCompileContext::global) {
        // `class X` where X already names a class, object or enum: say so, not "expected type name".
        if(q.isExtend && tok.is(token::classDeclaration)){
            token target = file.peekToken();
            string shown = target.originalValue.empty() ? target.value : target.originalValue;
            if(languageService.findGlobalAs<objectDef>(target.value) != nullptr)
                parsingError(format("'{0}' is an object, not a class; extend it with `extend {0} {{ … }}`.", shown));
            if(target.is(eTokenType::identifier))
                parsingError(format("extend class '{0}': there is no class '{0}' to extend.", shown));
        }
        if(tok.is(token::classDeclaration)){
            string nm = file.peekToken().value;
            transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
            if(languageService.isObjectType(nm))
                parsingError(format("'{0}' is already defined (originally declared at {1})",
                    file.peekToken().originalValue, languageService.declaredAt(nm)));
        }
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
        if(tok.is(eTokenType::identifier) && !tok.isDataType() && file.peekToken(1).is(eTokenType::identifier))
            unknownTypeError(tok);
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
        if(tok.is(token::bracketOpen))
            parsingError("'[ … ];' is an Inform 6 routine; in Beguile write `void name() { … }`, or put the routine "
                         "in an `#i6 { … }` block.");
        if(tok.is("global"))
            parsingError("A variable declared at file scope is already global; drop 'global': `int score = 0;`.");
        if(tok.is(eTokenType::identifier) && file.peekToken().is(token::parenOpen))
            parsingError(format("'{0}(…)' is a statement, and statements go inside a function; at file scope only "
                                "declarations and directives are allowed.", tok.originalValue.empty() ? tok.value : tok.originalValue));
        if(tok.is(eTokenType::directive)){
            string d = tok.value;
            if(d == "#ifdef" || d == "#ifndef")
                parsingError(format("'{0}' is the C preprocessor's; Beguile writes `#if {1}NAME`.", d, d == "#ifndef" ? "!" : ""));
            if(d.rfind("##", 0) == 0)
                parsingError(format("'{0}' belongs in an emitter body; in Beguile source write '{1}'.", d, d.substr(1)));
            parsingError(format("Unrecognized directive '{0}'.", d));
        }
        parsingError(format("Illegal global identifier:'{0}'", (string)tok));
    }

    if(q.isExtern) parsingError(format("Extern declaration only valid for global variables, classes, routines, and enums:'{0}'", (string)tok));

    // A word then a literal matched no statement form: `print "x";`, or a misspelled keyword.
    if(tok.is(eTokenType::identifier) && !looseIdentifierMode){
        token nx = file.peekToken();
        if(nx.isString() || nx.is(eTokenType::integer)){
            string shown = tok.originalValue.empty() ? tok.value : tok.originalValue;
            auto* f = dynamic_cast<functionDef*>(&contextObject);
            auto* fb = f ? dynamic_cast<statementBlock*>(f->body) : nullptr;
            if(languageService.findGlobalAs<functionDef>(tok.value) != nullptr)
                parsingError(format("'{0}' is a function: call it with parentheses, `{0}({1});`.", shown, nx.value));
            if(f != nullptr && resolveIdentifierType(tok.value, f, fb).empty())
                parsingError(format("'{0}' isn't a keyword or a declared name.", shown));
        }
    }
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
        if(t.is(eTokenType::identifier)) t = consumeTypeToken(t);   // `func<int, kinds.crate>`, an alias
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        else if(typeName == "array" || typeName == "rawarray") typeName = parseArrayTypeTail(typeName);
        typeName = maybeParseUnionTail(typeName);   // func<A | B, …>, as array<A | B>
        if(!first) result += ",";
        result += typeName;
        first = false;
        token sep = file.getToken();
        if(sep.value == ">>"){ file.pushBackCloseAngle(sep); break; }  // closes this level + an outer one
        if(sep.is(">")) break;
        // else sep is "," — continue
    }
    result += ">";
    return result;
}


// Consume the `<ElementType>` clause of an array type whose base keyword ("array" or
// "rawarray") the caller has already read, and return the full templated name — e.g.
// "array<int>" or the nested "array<array<int>>". Mirrors parseFuncType, including the
// `>>` split: the lexer merges two adjacent closing angles into one `>>` token, so at a
// nested close we deliver one `>` here and stash the other for the enclosing level.
string bglParser::parseArrayTypeTail(const string& base){
    file.getToken("<");
    token et = file.getToken({eTokenType::dataType, eTokenType::identifier});
    if(et.is(eTokenType::identifier)) et = consumeTypeToken(et);   // `array<array<ns.code>>`, an imported alias
    string elem = et.value;
    if(elem == "func")                             elem = parseFuncType();
    else if(elem == "array" || elem == "rawarray") elem = parseArrayTypeTail(elem);   // nested array-of-arrays
    elem = maybeParseUnionTail(elem);                                                 // array<A | B>
    token sep = file.getToken();
    if(sep.value == ">>") file.pushBackCloseAngle(sep);   // closes this level + an outer one
    else if(!sep.is(">")) parsingError("Expected '>' to close 'array<...>'");
    return base + "<" + elem + ">";
}


// Read a single complete member type after a union '|' — a base type name plus any
// func<...> / array<...> tail. Mirrors the inline resolution the declaration sites do,
// including the array<char> -> bytearray mapping so a member reads identically to a
// standalone declaration of the same type.
string bglParser::readUnionMemberType(){
    token t = file.getToken({eTokenType::dataType, eTokenType::identifier});
    bool isLiteral = false;
    if(t.value == "literal" && isLiteralQualifierAhead()){
        isLiteral = true;
        t = file.getToken({eTokenType::dataType, eTokenType::identifier});
    }
    string tn = t.value;
    if(tn == "func") tn = parseFuncType();
    else if(tn == "array" || tn == "rawarray"){
        tn = parseArrayTypeTail(tn);                     // "array<Elem>" / "rawarray<Elem>"
        if(tn == "array<char>" || tn == "array<charliteral>") tn = "bytearray";
    }
    if(isLiteral) pendingLiteralMembers.push_back(tn);
    return tn;
}

string bglParser::readArrayElementType(){
    token t = file.getToken({eTokenType::dataType, eTokenType::identifier});
    pendingLiteralElements = false;
    if(t.value == "literal" && isLiteralQualifierAhead()){
        pendingLiteralElements = true;
        t = file.getToken({eTokenType::dataType, eTokenType::identifier});
    }
    if(t.is(eTokenType::identifier)) t = consumeTypeToken(t);   // `array<stock.grade>`, an imported alias
    string elemType = t.value;
    if(elemType == "func") elemType = parseFuncType();  // func<...> element: consume its own <...>
    else if(elemType == "array" || elemType == "rawarray") elemType = parseArrayTypeTail(elemType);  // array<array<T>>
    return maybeParseUnionTail(elemType);              // array<A | B>
}

// After a `literal` token: true when a type follows, so `literal` is the qualifier rather than a
// name (it is not a reserved word).
bool bglParser::isLiteralQualifierAhead(){
    token next = file.peekToken();
    if(next.is(eTokenType::dataType) || next.value == "func" || next.value == "array" || next.value == "rawarray") return true;
    // A generic type parameter (`literal T item`) is an identifier followed by the declared name.
    token after = file.peekToken(2);
    return next.is(eTokenType::identifier) && (after.is(eTokenType::identifier) || after.is(eTokenType::dataType));
}

// After a just-read `func`/`array`/`rawarray`: true when its `<…>` is followed by '|'.
bool bglParser::genericTypeIsUnionHead(){
    if(file.peekToken().value != "<") return false;
    int depth = 0;
    for(int n = 1; n < 64; n++){
        token t = file.peekToken(n);
        if(t.is(eTokenType::eof) || t.value == ";" || t.value == "{" || t.value == "=") return false;
        if(t.value == "<") depth++;
        else if(t.value == ">") depth--;
        else if(t.value == ">>") depth -= 2;
        if(depth <= 0) return depth == 0 && file.peekToken(n + 1).value == "|";
    }
    return false;
}

// If a '|' follows the just-read first type, consume the '|'-separated members and return
// the canonical union name. Members are sorted+deduped so `string|func<>` and `func<>|string`
// name the same type (and a lone member after dedup collapses back to that member — a union of
// a type with itself is just that type). Returns firstType unchanged when no '|' follows.
string bglParser::maybeParseUnionTail(const string& firstType){
    if(file.peekToken().value != "|") return firstType;
    vector<string> members;
    members.push_back(firstType);
    while(file.peekToken().value == "|"){
        file.getToken();                    // consume '|'
        members.push_back(readUnionMemberType());
    }
    return canonicalUnionOf(members);
}

string bglParser::canonicalUnionOf(vector<string> members){
    vector<string> flat;
    for(const string& m : members){
        if(isUnionType(m)) for(const string& x : splitUnionType(m)) flat.push_back(x);
        else flat.push_back(m);
    }
    std::sort(flat.begin(), flat.end());
    flat.erase(std::unique(flat.begin(), flat.end()), flat.end());
    if(flat.size() == 1) return flat[0];
    string result;
    for(size_t i = 0; i < flat.size(); i++){
        if(i) result += "|";
        result += flat[i];
    }
    return result;
}


bool bglParser::processParameterList(functionDef& funcDef){
    token tok=file.getToken(); // first type, or ")" for empty list
    while(tok.isNot(token::parenClose)){
        paramDef& param=*new paramDef();
        param.docComment = tok.docComment;  // doc-comment attached to this param's leading type token
        // `ref` parameter: refers to the argument even when its class copies (spec 3.7).
        if(tok.is("ref")){ param.isRef = true; tok = file.getToken(); }
        pendingLiteralMembers.clear();
        bool firstIsLiteral = false;
        if(tok.value == "literal" && isLiteralQualifierAhead()){ firstIsLiteral = true; tok = file.getToken(); }
        tok = consumeTypeToken(tok);
        if(tok.is(eTokenType::identifier) && file.peekToken(1).is(eTokenType::identifier)) unknownTypeError(tok);
        if(!tok.is(eTokenType::dataType))
            tok.assertDataType(); // original error path for non-type tokens
        string paramTypeName = tok.value;
        if(paramTypeName == "func") paramTypeName = parseFuncType();
        else if(paramTypeName == "array" || paramTypeName == "rawarray") {
            // consume <ElementType> and store the full "array<T>" / "rawarray<T>" name for
            // for-in / type-compat / subscript-dispatch resolution.
            string base = paramTypeName;
            paramTypeName = parseArrayTypeTail(base);   // the element type resolves aliases and dotted paths
            // array<char> maps to bytearray (byte-access operators); others keep the <T> format.
            if(paramTypeName == "array<char>" || paramTypeName == "array<charliteral>")
                paramTypeName = "bytearray";
        }
        if(firstIsLiteral) pendingLiteralMembers.push_back(paramTypeName);
        paramTypeName = maybeParseUnionTail(paramTypeName);  // A | B | ... union parameter type
        param.literalMembers = pendingLiteralMembers;
        pendingLiteralMembers.clear();
        param.type=languageService.getType(paramTypeName);
        if(param.type.name.empty()) param.type.name = paramTypeName; // for func<...> and union types
        if(param.isRef){
            string why = refNotApplicable(getDispatchClass(param.type.name), typeDisplayName(param.type.name));
            if(why.empty() && languageService.findEnum(param.type.name) != nullptr)
                why = "'" + typeDisplayName(param.type.name) + "' is an enumeration — its value is held directly, so there is nothing to refer to.";
            if(!why.empty()) parsingError("'ref': " + why);
        }
        tok=file.getToken(); // name, "=", ",", or ")"
        // Accept both identifier and dataType tokens for the parameter name. A dataType here
        // means the name collides with a registered class (e.g., parameter 'b' when 'class B'
        // exists). The parameter declaration is the authoritative use of the name in the
        // function's scope, so we allow the shadow.
        if(tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)){
            param.name=(string) tok;
            for(paramDef* other : funcDef.params)
                if(other->name == param.name)
                    parsingError(format("'{0}' has two parameters named '{1}'.", funcDef.dName(),
                                        tok.originalValue.empty() ? param.name : tok.originalValue));
            param.displayName=tok.originalValue;
            // Disallow parameter names that shadow a global, a class member, or an object member.
            // Emitters are skipped: their parameter names are template substitution keys, not I6 locals.
            // Externs are skipped: their parameter names are documentary only (the body lives outside
            // Beguile), so they never create an I6 local that could shadow anything.
            // Symbolic-constant globals (grammartoken, attribute, property, verb) are skipped:
            // they're I6 constants used in grammar/give/provides contexts, not writable storage,
            // so a parameter of the same name doesn't actually shadow anything reachable in code.
            if(!funcDef.isEmitter && !funcDef.isExternal){
                // Reported at the parameter, once each: a member may be listed more than once.
                set<string> said;
                sourceLocation savedSrc = currentStatementSrc;
                if(tok.src.line > 0) currentStatementSrc = tok.src;
                auto warnOnce = [&](const string& msg){ if(said.insert(msg).second) parsingWarning(msg); };
                for(typeDef* g : languageService.globals)
                    if(g->name == param.name){
                        if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                            const string& t = vd->type.name;
                            if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                        }
                        if(dynamic_cast<verbObjectDef*>(g) != nullptr) continue;   // a verb, exempt as a local is (§3.10)
                        if(auto* cd = dynamic_cast<classDef*>(g))   // a type, not a global `::` reaches
                            warnOnce("Parameter '" + param.name + "' has the name of " + (cd->isUnion() ? "union '" : "class '") + g->dName()
                                + "'; in this routine the name means the parameter, though it still names the type in declarations.");
                        else
                            warnOnce("Parameter '" + param.name + "' shadows global of the same name; '::" + param.name + "' reaches the global.");
                    }
                if(definedSymbols.count(param.name))
                    warnOnce("Parameter '" + param.name + "' has the name of a #define symbol; in this routine the name means the parameter.");
                // A static method has no receiver, so a member can't be shadowed.
                if(currentClass != nullptr && !funcDef.isStatic){
                    for(typeMember* m : currentClass->members)
                        if(m->name == param.name)
                            warnOnce("Parameter '" + param.name + "' shadows a member of class '" + currentClass->dName() + "'.");
                    // Walk base class hierarchy for inherited members (vars and functions) — warning only
                    function<void(classDef*)> checkBases = [&](classDef* c){
                        for(typeMember* m : c->members)
                            if(m->name == param.name)
                                if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                                    warnOnce("Parameter '" + param.name + "' shadows inherited member '" + param.name + "' from class '" + c->dName() + "'.");
                        for(classDef* base : c->baseClasses) checkBases(base);
                    };
                    for(classDef* base : currentClass->baseClasses) checkBases(base);
                }
                if(currentObject != nullptr)
                    for(typeMember* m : currentObject->members)
                        if(m->name == param.name)
                            warnOnce("Parameter '" + param.name + "' shadows a member of object '" + currentObject->dName() + "'.");
                currentStatementSrc = savedSrc;
            }
            tok=file.getToken(); // "=", ",", or ")"
        }
        if(tok.is(token::assignment)){
            // Set expected type to the parameter's declared type so name resolution can
            // disambiguate (e.g. an enum value name shared by multiple enums).
            string savedExpectedDef = currentExpectedType;
            currentExpectedType = param.type.name;
            token defFirst = file.getToken();
            size_t setUpBefore = pendingInjections.size();
            expression* defExpr = parseExpression(defFirst, {token::comma, token::parenClose}, nullptr, nullptr);
            currentExpectedType = savedExpectedDef;
            // A default is one value at each call; `?.` and `??` need statements to run first.
            if(pendingInjections.size() != setUpBefore)
                parsingError(format("The default of '{0}' needs statements to run before it (a `?.` or `??`); give the "
                                    "parameter a plain default and work it out inside the routine.", param.name));
            // An accessor named as a default gives what its getter reads, as a call's argument does.
            if(functionDef* g = defExpr->resolvedType != param.type.name ? nativeGetterOf(defExpr->resolvedType) : nullptr)
                if(getDispatchClass(param.type.name) != getDispatchClass(defExpr->resolvedType)){
                    defExpr->tokens = {parenthesizeReceiver(defExpr->text()) + "." + g->i6name + "()"};
                    defExpr->resolvedType = g->returnType.name;
                }
            param.defaultValue = defExpr->text();
            // As written, for display: a one-token default (`fixed`, `null`) reads better than its
            // emitted value (`16`, `nothing`). Longer defaults show the emitted text.
            if(file.peekToken().isNot(eTokenType::eof) && defExpr->tokens.size() == 1)
                param.defaultSource = defFirst.originalValue.empty() ? defFirst.value : defFirst.originalValue;
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
    // An assignment operator is chosen by the value's type and its ancestors (findAssignOperator), so
    // one taking `var` would never be chosen. Every class derives _bglObject: name the type accepted.
    if(funcDef.name == "=" && funcDef.params.size() == 1 && funcDef.params[0]->type.name == "var")
        parsingError("'operator = (var …)' is never chosen: an assignment operator is picked by the value's type. "
                     "Declare the parameter as the type it accepts (any class: '_bglObject').");
    return false;
}

// Depth-first search through a class and its base classes (first-listed wins).
// Returns the first typeMember* for which pred returns true, or nullptr.
// `cls` must be non-null.
typeMember* bglParser::findMemberInHierarchy(classDef* cls, std::function<bool(typeMember*)> pred){
    return cls->findMember(pred);
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
    // The pre-scan leaves one stub per overload, so the stub with this definition's parameter types
    // is the one it replaces; a stub without parameters stands for any overload of the name.
    auto sameSignature = [&](functionDef* fd){
        if(fd->params.size() != newDef.params.size()) return false;
        for(size_t i = 0; i < fd->params.size(); i++)
            if(fd->params[i]->type.name != newDef.params[i]->type.name) return false;
        return true;
    };
    // The last resort takes a stub whose parameters could be these: a stub's type the pre-scan couldn't
    // resolve (an alias) might be this one, but a stub of known, different types is another overload.
    auto mayBeThis = [&](functionDef* fd){
        if(fd->params.size() != newDef.params.size()) return false;
        for(size_t i = 0; i < fd->params.size(); i++){
            const string& a = fd->params[i]->type.name;
            const string& b = newDef.params[i]->type.name;
            if(a == b) continue;
            // Resolved all the way: no path left in it, and a generic's arguments resolved too.
            std::function<bool(const string&)> known = [&](const string& t) -> bool {
                if(t.empty() || t.find('.') != string::npos) return false;
                if(size_t lt = t.find('<'); lt != string::npos && t.back() == '>'){
                    string args = t.substr(lt + 1, t.size() - lt - 2);
                    size_t from = 0;
                    for(size_t c; (c = args.find(',', from)) != string::npos; from = c + 1){
                        string a = args.substr(from, c - from);
                        a.erase(0, a.find_first_not_of(' '));
                        if(!known(a)) return false;
                    }
                    string last = args.substr(from); last.erase(0, last.find_first_not_of(' '));
                    if(!known(last)) return false;
                    return getDispatchClass(t.substr(0, lt)) != nullptr;
                }
                static const set<string> prim = {"int", "uint", "bool", "char", "string", "var", "float", "object"};
                return prim.count(t) || getDispatchClass(t) != nullptr || languageService.findEnum(t) != nullptr;
            };
            if(known(a) && known(b)) return false;
        }
        return true;
    };
    for(int pass = 0; pass < 3; pass++)
        for(auto it = members.begin(); it != members.end(); ++it)
            if(auto* fd = dynamic_cast<functionDef*>(*it))
                if(fd->name == newDef.name && fd->isPrePassStub
                   && (pass == 2 ? mayBeThis(fd) : pass == 0 ? sameSignature(fd) : fd->params.empty())){
                    // An emitter an `extend` later replaces: every use expands the replacement.
                    if(fd->isReplaceStub && fd->isEmitter && newDef.isEmitter && dynamic_cast<i6Block*>(fd->body) != nullptr)
                        newDef.body = fd->body;
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
            else if(nc == '\\') currentStr += i6CharCode(92, file.peekChar());   // \\  → literal backslash
            else if(nc == '@')  currentStr += i6CharCode(64, file.peekChar());   // \@  → literal at-sign
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
                if(xc == '^') { file.readChar(); currentStr += i6CharCode(94, file.peekChar()); }
                else if(xc == 'y' || xc == 'Y'){ file.readChar(); currentStr += xc == 'y' ? "@{177}" : "@{176}"; }
                else if(string("aeiouAEIOU").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@^"; currentStr += xc;
                } else { currentStr += i6CharCode(94, file.peekChar()); }
            }
            else if(nc == '~') {
                char xc = file.peekChar();
                if(xc == '~') { file.readChar(); currentStr += i6CharCode(126, file.peekChar()); }
                else if(string("anoANO").find(xc) != string::npos) {
                    file.readChar(); currentStr += "@~"; currentStr += xc;
                } else { currentStr += i6CharCode(126, file.peekChar()); }
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
                    file.readChar(); currentStr += "@/"; currentStr += xc;
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
                if(xc == 'a' || xc == 'A' || xc == 'e') {
                    file.readChar(); currentStr += "@o"; currentStr += xc;
                } else { currentStr += 'o'; }
            }
            else if(nc == 's' && file.peekChar()=='s') { file.readChar(); currentStr += "@ss"; }
            else if(nc == 'a' && file.peekChar()=='e') { file.readChar(); currentStr += "@ae"; }
            else if(nc == 'A' && file.peekChar()=='E') { file.readChar(); currentStr += "@AE"; }
            else if(nc == 'O' && file.peekChar()=='E') { file.readChar(); currentStr += "@OE"; }
            else if(nc == 't' && file.peekChar()=='h') { file.readChar(); currentStr += "@th"; }
            else if(nc == 'T' && file.peekChar()=='H') { file.readChar(); currentStr += "@Th"; }
            else if(nc == 'e' && file.peekChar()=='t') { file.readChar(); currentStr += "@et"; }
            else if(nc == 'E' && file.peekChar()=='T') { file.readChar(); currentStr += "@Et"; }
            else if(nc == 'L' && file.peekChar()=='L') { file.readChar(); currentStr += "@LL"; }
            else if(nc == '!' && file.peekChar()=='!') { file.readChar(); currentStr += "@!!"; }
            else if(nc == '?' && file.peekChar()=='?') { file.readChar(); currentStr += "@??"; }
            else if(nc == '<' && file.peekChar()=='<') { file.readChar(); currentStr += "@<<"; }
            else if(nc == '>' && file.peekChar()=='>') { file.readChar(); currentStr += "@>>"; }
            else if(nc == '}')  currentStr += '}';
            else parsingError(format("Unknown escape '\\{0}' in string literal", string(1, nc)));
        } else if(c == '{') {
            if(!currentStr.empty()) {
                interpolatedSegment seg;
                seg.isExpr = false;
                seg.text = currentStr;
                segments.push_back(seg);
                currentStr = "";
            }
            size_t setUpAt = pendingInjections.size();   // earlier set-up (a `?.` guard) belongs to the statement
            file.prevTokenType = eTokenType::symbol; file.prevTokenValue = "{";   // not the `$`: a string here is plain
            token exprFirst = file.getToken();
            if(exprFirst.is(token::braceClose))
                parsingError("An interpolated string's '{ }' holds no expression; write one, or '\\{' for a literal brace.");
            // An interpolation segment prints a value but CALLS a routine: a void call is valid here
            // (emitInterpolatedSegments lowers a void-typed segment to a bare `expr;` statement — the
            // routine runs, nothing is printed). So permit void in segment position.
            bool savedAllowVoid = allowVoidReturnExpr;
            allowVoidReturnExpr = true;
            expression* exprNode = nullptr;
            int savedBraceDepth = file.braceDepth;
            int savedSpanDepth = file.interpSpanDepth; bool savedSpanClosed = file.interpSpanClosed;
            size_t savedSpanStream = file.interpSpanStream;
            file.interpSpanDepth = 0; file.interpSpanClosed = exprFirst.is(token::braceClose); file.interpSpanStream = file.streamDepth();
            try {
                exprNode = parseExpression(exprFirst, {"}"}, func, body);
                file.interpSpanDepth = savedSpanDepth; file.interpSpanClosed = savedSpanClosed; file.interpSpanStream = savedSpanStream;
            } catch(...) {
                file.interpSpanDepth = savedSpanDepth; file.interpSpanClosed = savedSpanClosed; file.interpSpanStream = savedSpanStream;
                // Leave the reader past the whole string, so recovery doesn't start inside it.
                allowVoidReturnExpr = savedAllowVoid;
                file.braceDepth = savedBraceDepth;
                int depth = file.hasPendingToken && file.pendingToken.is(token::braceClose) ? 0 : 1;
                file.hasPendingToken = false;
                for(int ch = file.readChar(); ch != EOF; ch = file.readChar()){
                    if(ch == '\\'){ file.readChar(); continue; }
                    if(ch == '"' && depth > 0){   // a string inside the expression
                        for(int q = file.readChar(); q != EOF && q != '"'; q = file.readChar()) if(q == '\\') file.readChar();
                        continue;
                    }
                    if(ch == '{') depth++;
                    else if(ch == '}' && depth > 0) depth--;
                    else if(ch == '"' && depth == 0) break;
                }
                throw;
            }
            allowVoidReturnExpr = savedAllowVoid;
            // A property accessor prints its value, as `print(x)` does — unless its class prints itself.
            // An object prints as itself: its class's conversion operator isn't a property getter.
            if(functionDef* getter = nativeGetterOf(exprNode->resolvedType);
               getter != nullptr && !isObjectBackedClass(getDispatchClass(exprNode->resolvedType))){
                classDef* cls = getDispatchClass(exprNode->resolvedType);
                bool printsItself = false;
                if(cls != nullptr)
                    for(typeMember* m : cls->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == "print" && fd->params.empty())
                            printsItself = true;
                if(!printsItself){
                    string text = exprNode->text();
                    exprNode->tokens.assign(1, text + "." + getter->i6name + "()");
                    exprNode->resolvedType = getter->returnType.name;
                }
            }
            convertStoragelessForPrint(exprNode);
            holdObjectCallForPrint(exprNode);
            interpolatedSegment seg;
            seg.isExpr = true;
            seg.expr = exprNode;
            seg.injections.assign(pendingInjections.begin() + setUpAt, pendingInjections.end());
            pendingInjections.resize(setUpAt);
            segments.push_back(seg);
        } else if((unsigned char)c >= 0x80) {
            // Directly-typed non-ASCII (smart quotes, accented characters): decode and translate
            // exactly as a plain string literal would, so raw UTF-8 bytes never leak into I6.
            uint32_t codepoint = decodeUtf8((unsigned char)c, [&](){ return file.readChar(); }, [&](){ return file.peekChar(); });
            string tr;
            if(i6TranslateStringCodepoint(codepoint, tr)) currentStr += tr;
            else {
                char hexBuf[16]; snprintf(hexBuf, sizeof(hexBuf), "%04X", codepoint);
                parsingError(format("Unsupported Unicode character U+{0} in interpolated string literal", string(hexBuf)));
            }
        } else if(c == '`') {
            // Reverse tick: same target-dependent fold as a plain string literal.
            string tr; i6TranslateStringCodepoint('`', tr); currentStr += tr;
        } else if(c == '@') {
            currentStr += i6CharCode(64, file.peekChar());   // a plain at-sign (I6 reads `@` as an escape)
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
        else if(tok.is("inline"))                  { q.isInline   = true; advance(); }
        else if(tok.is(token::extend))             { q.isExtend   = true; advance(); }
        else if(tok.is("alias"))                   { q.isAlias    = true; advance(); }
        else if(tok.is("default"))                 { q.isDefault  = true; advance(); }
        else if(tok.is("ref"))                     { q.isRef      = true; advance(); }
        else if(tok.is("superposed"))              { q.isSuperposed = true; advance(); }
        else if(tok.is("typesealed"))              { q.isTypeSealed = true; advance(); }
        else if(tok.is("additive"))                { q.isAdditive   = true; advance(); }
        // `required` is a keyword only in front of a member's type; it stays an ordinary name elsewhere.
        else if(tok.value == "required" && (file.peekToken().is(eTokenType::dataType)
                    || file.peekToken().isOneOf({"array", "rawarray", "inline", "const", "static", "ref", "typesealed", "literal",
                                                 "default", "emitter"})))
                                                   { q.isRequired   = true; advance(); }
        else if(tok.value == "literal" && isLiteralQualifierAhead()) { q.isLiteral = true; advance(); }
        // `value` and `primitive` are keywords only in front of `class`: both are ordinary names elsewhere.
        else if(tok.is("value") && file.peekToken().is(token::classDeclaration))     { q.isValue     = true; advance(); }
        else if(tok.is("primitive") && file.peekToken().is(token::classDeclaration)) { q.isPrimitive = true; advance(); }
        else break;
    }
    declLiteral = q.isLiteral;
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
            // Still on the statement's line: the stream stands just past the offending part.
            auto detail = file.getCurrentFileDetail();
            fileName = currentStatementSrc.file;
            curLine  = currentStatementSrc.line;
            curCol   = (get<1>(detail) == fileName && get<2>(detail) == curLine) ? get<3>(detail)
                     : max(1, currentStatementSrc.col);
        } else {
            auto detail = file.getCurrentFileDetail();
            fileName = get<1>(detail);
            curLine  = get<2>(detail);
            curCol   = get<3>(detail);
        }
        errorMessage=format("{0}:{1}:{2}: ERROR: {3}",fileName,curLine,curCol,msg);
    }
    else{
        // Post-parse checks run with no file open and supply their own `file:line:col: ` prefix;
        // the ERROR label still goes after it, where every other error has it.
        static const std::regex located(R"(^(.*:\d+:\d+: )(.*)$)");
        std::smatch m;
        if(std::regex_match(msg, m, located)) errorMessage = m[1].str() + "ERROR: " + m[2].str();
        else                                  errorMessage = "ERROR: " + msg;
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
            // Still on the statement's line: the stream stands just past the offending part.
            auto detail = file.getCurrentFileDetail();
            fileName = currentStatementSrc.file;
            curLine  = currentStatementSrc.line;
            curCol   = (get<1>(detail) == fileName && get<2>(detail) == curLine) ? get<3>(detail)
                     : max(1, currentStatementSrc.col);
        } else {
            auto detail = file.getCurrentFileDetail();
            fileName = get<1>(detail);
            curLine  = get<2>(detail);
            curCol   = get<3>(detail);
        }
        // Suppress warnings that originate in the runtime library (BLR): they concern the
        // compiler's own internals, which the user cannot act on. (I6 warnings that map into
        // the BLR are filtered the same way when I6 runs.)
        if(!settings.libPath.empty() && !fileName.empty()){
            std::error_code ec;
            string f   = filesystem::weakly_canonical(fileName, ec).string();
            string lib = filesystem::weakly_canonical(settings.libPath, ec).string();
            if(!lib.empty() && f.rfind(lib, 0) == 0) return;
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
// A `return` leaves every nested block it sits inside, so their deinits run first — innermost
// out. The routine's own top-level locals are not here: those are func->cleanups, which the
// emitter already writes ahead of every return.
void bglParser::emitOpenBlockCleanups(statementBlock* body){
    if(body == nullptr) return;
    for(auto blk = activeBlockStack.rbegin(); blk != activeBlockStack.rend(); ++blk){
        auto it = blockCleanups.find(*blk);
        if(it == blockCleanups.end()) continue;
        for(auto r = it->second.rbegin(); r != it->second.rend(); ++r){
            i6RawNode* node = new i6RawNode();
            node->text = *r;
            node->cooked = true;   // names locals, renamed like any other use
            body->statements.push_back(node);
        }
    }
}

// A `break` or `continue` leaves the blocks inside the innermost loop — or, for a `break`, the innermost
// switch when that is nearer — so their deinits run first.
void bglParser::emitLoopExitCleanups(statementBlock* body, bool isBreak){
    if(body == nullptr) return;
    size_t boundary = loopBlockDepths.empty() ? 0 : loopBlockDepths.back();
    if(isBreak && !switchBlockDepths.empty()) boundary = max(boundary, switchBlockDepths.back());
    if(loopBlockDepths.empty() && !(isBreak && !switchBlockDepths.empty())) return;
    for(size_t i = activeBlockStack.size(); i > boundary; i--){
        auto it = blockCleanups.find(activeBlockStack[i - 1]);
        if(it == blockCleanups.end()) continue;
        for(auto r = it->second.rbegin(); r != it->second.rend(); ++r){
            i6RawNode* node = new i6RawNode();
            node->text = *r;
            node->cooked = true;
            body->statements.push_back(node);
        }
    }
}

void bglParser::openCompileContext(eCompileContext newScope, statementBlock* body){
    compileContextStack.push_front(newScope);
    // For codeBlock contexts, always push to the active block stack (nullptr if no body provided).
    // closeCompileContext always pops for codeBlock, so push/pop counts must match.
    if(newScope == eCompileContext::codeBlock) activeBlockStack.push_back(body);
    // Each nested block is a level of the parser's own recursion; past a few hundred the stack runs out.
    if(compileContextStack.size() > 400) parsingError("Blocks are nested too deeply (more than 400 levels).");
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
    if(!activeBlockStack.empty() && expectedScope == eCompileContext::codeBlock){
        statementBlock* closing = activeBlockStack.back();
        activeBlockStack.pop_back();
        // The block's locals die here, so their deinits run here — last declared, first released.
        // Unless the block ends in a `return`, which already ran them on its way out (see
        // emitOpenBlockCleanups); appending after it would only leave I6 unreachable statements.
        auto it = blockCleanups.find(closing);
        bool endsInReturn = !closing->statements.empty()
                         && dynamic_cast<returnStatement*>(closing->statements.back()) != nullptr;
        // A `break` or `continue` ran them too (emitLoopExitCleanups).
        if(!closing->statements.empty())
            if(auto* raw = dynamic_cast<i6RawNode*>(closing->statements.back()); raw && (raw->text == "break;" || raw->text == "continue;"))
                endsInReturn = true;
        if(it != blockCleanups.end() && endsInReturn) blockCleanups.erase(it);
        else if(it != blockCleanups.end()){
            for(auto r = it->second.rbegin(); r != it->second.rend(); ++r){
                i6RawNode* node = new i6RawNode();
                node->text = *r;
                node->cooked = true;   // names locals, renamed like any other use
                closing->statements.push_back(node);
            }
            blockCleanups.erase(it);
        }
    }
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
