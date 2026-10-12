#include "platform.h"
#include <iostream>
#include <fstream>
#include <regex>
#include <sstream>
#include <algorithm>

#include "i6Emitter.h"
#include "settings.h"
#include "bglParser.h"
#include "beguiler.h"
#include "helpers.h"
#include "bglLanguageService.h"
#include "bglParserHelpers.h"

using namespace std;

//The emitter writes to a standard output stream.  We can reassign this if we want...
void i6Emitter::to(ostream& strm){
    out.std::ios::rdbuf(strm.rdbuf());
}

// Stored-emit-first/last placeholder markers. Written into the buffer by generateAll
// at the matching emit-first/last positions, then substituted by resolvedOutput() with
// the concatenated bodies of stored blocks whose names appear in firedStoredNames.
// Both start with `!` so they're harmless I6 comments if substitution somehow fails.
static const char* kStoredFirstMarker = "!__BGL_STORED_EMITFIRST__\n";
static const char* kStoredLastMarker  = "!__BGL_STORED_EMITLAST__\n";

static size_t findWordCI(const string& haystack, const string& needle, size_t pos);

// Inform 6 reads `print (word)` as a print rule (`print (name) obj`), so a printed VALUE that is a
// single word in parentheses — an emitter's `(42)` result, `(x)` — doesn't compile. One followed by
// `;`, `,`, `.` or an operator is such a value: it becomes `0+(…)`, the same number. String literals and
// `!` comments are left alone.
static string fixPrintedParenValues(const string& in){
    string out; out.reserve(in.size() + 64);
    auto isWordChar = [](char c){ return isalnum((unsigned char)c) || c == '_'; };
    size_t i = 0, n = in.size();
    while(i < n){
        char c = in[i];
        if(c == '"'){                                   // string literal: copy through the closing quote
            size_t j = in.find('"', i + 1);
            j = (j == string::npos) ? n : j + 1;
            out.append(in, i, j - i); i = j; continue;
        }
        if(c == '!'){                                   // comment to end of line
            size_t j = in.find('\n', i);
            j = (j == string::npos) ? n : j;
            out.append(in, i, j - i); i = j; continue;
        }
        bool wordStart = (i == 0 || (!isWordChar(in[i - 1]) && in[i - 1] != '.'));
        size_t kw = 0;
        if(wordStart){
            if(n - i >= 9 && strncasecmp(in.c_str() + i, "print_ret", 9) == 0 && (i + 9 >= n || !isWordChar(in[i + 9]))) kw = 9;
            else if(n - i >= 5 && strncasecmp(in.c_str() + i, "print", 5) == 0 && (i + 5 >= n || !isWordChar(in[i + 5]))) kw = 5;
        }
        if(kw == 0){ out += c; i++; continue; }
        out.append(in, i, kw); i += kw;
        size_t p = i;
        while(p < n && (in[p] == ' ' || in[p] == '\t')) p++;
        if(p >= n || in[p] != '(') continue;
        int depth = 0; size_t q = p;
        for(; q < n; q++){
            if(in[q] == '(') depth++;
            else if(in[q] == ')' && --depth == 0) break;
            else if(in[q] == '"' || in[q] == '\n') { q = n; break; }
        }
        if(q >= n) continue;
        // Only a single word in the parentheses reads as a rule name; `(40 + 2)` is already a value.
        string inner = in.substr(p + 1, q - p - 1);
        size_t a0 = inner.find_first_not_of(" \t"), a1 = inner.find_last_not_of(" \t");
        bool oneWord = a0 != string::npos;
        for(size_t k = a0; oneWord && k <= a1; k++)
            if(!(isWordChar(inner[k]) || inner[k] == '$' || inner[k] == '#')) oneWord = false;
        size_t r = q + 1;
        while(r < n && (in[r] == ' ' || in[r] == '\t')) r++;
        char next = r < n ? in[r] : ';';
        if(oneWord && (next == ';' || next == ',' || next == '.' || strchr("+-*/%&|=<>~", next) != nullptr)){
            out.append(in, i, p - i);
            out += "0+";
            i = p;
        }
    }
    return out;
}

string i6Emitter::resolvedOutput(){
    string buf = out.str();
    // A pooled class with owned members copies through its own routine (synthesizePooledOwnedMembers).
    for(const auto& [cls, routine] : ownedCopyRoutines)
        for(size_t at = buf.find(cls + ".copy("); at != string::npos; at = buf.find(cls + ".copy(", at + routine.size()))
            if(at == 0 || !(isalnum((unsigned char)buf[at - 1]) || buf[at - 1] == '_' || buf[at - 1] == '('))
                buf.replace(at, cls.size() + 6, routine + "(");
    auto substitute = [&](const char* marker, const map<string,string>& blocks){
        size_t pos = buf.find(marker);
        if(pos == string::npos) return;
        string content;
        for(const auto& [name, body] : blocks)
            if(languageService.firedStoredNames.count(name))
                content += body + "\n";
        // The marker is a whole-line placeholder in `out`, which the sourceMap was built against.
        // Replacing it with content of a different line count shifts every later .inf line, so
        // re-base each map entry below the marker by that delta — otherwise a fired-nothing
        // #storedEmitFirst (marker → empty) leaves the whole map off by one. (Superposed blocks are
        // appended after these substitutions, so their offsets already see the corrected buffer.)
        int markerLine   = (int)std::count(buf.begin(), buf.begin() + pos, '\n') + 1;
        int markerLines  = (int)std::count(marker, marker + strlen(marker), '\n');
        int contentLines = (int)std::count(content.begin(), content.end(), '\n');
        int delta = contentLines - markerLines;
        buf.replace(pos, strlen(marker), content);
        if(delta != 0)
            for(auto& e : sourceMap)
                if(std::get<0>(e) > markerLine) std::get<0>(e) += delta;
    };
    substitute(kStoredFirstMarker, languageService.storedEmitFirstBlocks);
    substitute(kStoredLastMarker,  languageService.storedEmitLastBlocks);

    // `superposed` routines: materialize one into the output only if its name is referenced
    // somewhere in the already-assembled text. Their own definitions were withheld from `buf`
    // (captured in superposedBlocks), so any word-boundary occurrence of the name IS a genuine
    // call/reference. Loop to a fixed point: an included routine may itself reference another
    // still-withheld superposed routine.
    // Case-INSENSITIVE word-boundary match. Beguile is case-insensitive and lowercases
    // identifiers when emitting normal routine bodies, but routine/definition headers and
    // emitter-inlined text preserve the source case — so a superposed routine `_bglPow` may
    // be referenced as `_bglpow` from inside another (transitively materialized) routine.
    // A case-sensitive scan would miss that and silently drop the dependency (I6 then errors
    // "No such constant"). Match case-insensitively so every genuine reference is observed.
    auto referenced = [&](const string& name) -> bool {
        auto lc = [](char c){ return (char)tolower((unsigned char)c); };
        if(name.empty() || name.size() > buf.size()) return false;
        for(size_t p = 0; p + name.size() <= buf.size(); p++){
            size_t i = 0;
            for(; i < name.size(); i++)
                if(lc(buf[p + i]) != lc(name[i])) break;
            if(i != name.size()) continue;
            char before = p > 0 ? buf[p-1] : ' ';
            char after  = p + name.size() < buf.size() ? buf[p + name.size()] : ' ';
            bool wb = !(isalnum((unsigned char)before) || before=='_')
                   && !(isalnum((unsigned char)after)  || after=='_');
            if(wb) return true;
        }
        return false;
    };
    // Splice one captured block into buf. For a superposed CLASS, first splice its superposed bases
    // (recursively) — an I6 `Class` directive must physically precede any class that derives from it,
    // and the deriving subclass may be the ONLY thing that references the base. Erase before recursing
    // so a (pathological) inheritance cycle can't loop forever.
    std::function<void(const string&)> splice = [&](const string& name){
        auto it = superposedBlocks.find(name);
        if(it == superposedBlocks.end()) return;        // absent: already spliced, or emitted in-place
        string body = it->second;
        superposedBlocks.erase(it);
        if(superposedObjectNames.count(name)) splicedSuperposedObjects.insert(name);
        auto bit = superposedClassBaseNames.find(name);
        if(bit != superposedClassBaseNames.end()){
            vector<string> bases = bit->second;
            superposedClassBaseNames.erase(bit);
            for(const string& base : bases) splice(base);   // base-first
        }
        // The block's captured sourceMap i6Lines are relative to its own text (line 1 = its header).
        // It lands here, starting at line offset+1, so re-base each entry by `offset` before merging.
        int offset = (int)std::count(buf.begin(), buf.end(), '\n');
        buf += body;                                     // observed — collapse it into the story file
        auto mit = superposedBlockMaps.find(name);
        if(mit != superposedBlockMaps.end()){
            for(auto& e : mit->second)
                sourceMap.push_back({offset + std::get<0>(e), std::get<1>(e), std::get<2>(e)});
            superposedBlockMaps.erase(mit);
        }
    };
    bool added = true;
    while(added){
        added = false;
        // Snapshot keys — splice() mutates superposedBlocks (including base classes it pulls in).
        vector<string> keys;
        for(auto& kv : superposedBlocks) keys.push_back(kv.first);
        for(const string& k : keys){
            if(superposedBlocks.count(k) && referenced(k)){
                splice(k);
                added = true;
            }
        }
    }

    pruneSuperposedObjectMethods(buf);

    // The .inf-mode trailer — `end;` through EOF — goes last of all, AFTER the superposed blocks.
    // It used to be written into `out` during emission, which put `end;` ahead of them: I6 stops
    // reading at `end;`, so a superposed routine an island had called was simply not there ("No
    // such constant as ..."). Nothing in the trailer can reference a superposed name, since I6
    // never reads past `end;` either, so withholding it from the search above costs nothing.
    if(!languageService.infTrailer.empty()) buf += languageService.infTrailer;

    // bglInit() does the work a program cannot see it needs: stamping the length headers of sized
    // tracked arrays and byte arrays, running `#startup` blocks, and running the deferred
    // initializers of class-typed globals (§21.2). A library binding calls it by wrapping the
    // library's `main`; a program with no binding has to call it itself, and one that does not
    // still RUNS — it just runs on uninitialized data. A sized `array<T>` reports its raw header
    // word as its length, a sized `array<char>` is still a null pointer, and nothing in the output
    // says so. Checked against the finished text because the binding's call comes from an
    // `#emitlast` block, not from anything the parser saw.
    if(bglInitHasWork){
        // Only a program can start uninitialized. A file with no entry point — a fixture or a
        // library-only source compiled on its own — never starts, so there is nothing to warn
        // about. The routine HEADER is what counts, in either mode: Beguile's `[main;` or the
        // `[ Main ;` of an author's .inf in precompiler mode.
        bool hasEntryPoint = false;
        for(size_t i = 0; (i = findWordCI(buf, "main", i)) != string::npos; i += 4){
            size_t j = buf.find_last_not_of(" \t", i - 1);
            if(i > 0 && j != string::npos && buf[j] == '['){ hasEntryPoint = true; break; }
        }
        bool called = false;
        for(size_t i = 0; (i = findWordCI(buf, "bglinit", i)) != string::npos; i += 7){
            size_t j = buf.find_first_not_of(" \t", i + 7);
            if(j != string::npos && buf[j] == '('){ called = true; break; }
        }
        if(hasEntryPoint && !called)
            std::cerr << "WARNING: nothing calls bglInit(), so this program starts with the BLR "
                         "uninitialized.\n";
    }
    for(size_t p = 0; (p = buf.find(kGlobalEscapeMarker, p)) != string::npos; )
        buf.erase(p, strlen(kGlobalEscapeMarker));
    // A debug build compiles Inform 6's veneer with tracing, which uses `debug_flag`, `workflag` and
    // `DebugAttribute`. A library declares them; a program without one needs them, declared after
    // anything that might.
    if(parser.isSymbolDefined("debug"))
        buf += "\n#Ifndef debug_flag;\nGlobal debug_flag;\n#Endif;\n"
               "#Ifndef workflag;\nAttribute workflag;\n#Endif;\n"
               "#Ifndef DebugAttribute;\n[ DebugAttribute a; print \"(attribute \", a, \")\"; ];\n#Endif;\n";
    buf = fixPrintedParenValues(buf);
    return buf;
}

// Load built-in I6 templates from beguilib/core/__builtins.i6b.
//
// Format (see the file header for the full spec):
//
//   // line build-time comment   /* block build-time comment */   (both stripped)
//   template <name>($p1, $p2, ...) [triggers <name1> <name2> ...] {{{
//       ... raw I6 with $param holes ...
//   }}}
//
// - The `{{{` fence must end the header line; the body runs until a line that is
//   exactly `}}}` (so I6 bodies with unbalanced braces — forIn.open/close — are fine).
// - `$name` holes are substituted by applyTemplate (word-boundary, case-insensitive).
// - `triggers <names>`: when the template is applied, each name is added to
//   languageService.firedStoredNames, gating the matching #storedEmitFirst/#storedEmitLast
//   block for emission. (Replaces the former in-body `##triggerEmitter` annotation.)
// - Template names may contain dots (forIn.open). Params may be written with or without `$`.
// - `//` and `/* ... */` are BUILD-TIME comments — stripped here, never emitted, and valid
//   anywhere (including inside a body, since they're removed before emission). The stripper is
//   string-aware (`"..."`) so `//`/`/*` inside an I6 string literal are left intact.
void i6Emitter::loadBuiltinTemplates(string path){
    ifstream f(path);
    if(!f.is_open()) return;
    string src((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());

    // Strip build-time comments (// to EOL and /* ... */), preserving newlines so line
    // structure and the `}}}` fence survive. String-aware: comments inside "..." stay.
    string clean; clean.reserve(src.size());
    enum { NORM, STR, LINEC, BLOCKC } st = NORM;
    for(size_t i = 0; i < src.size(); i++){
        char c = src[i], n = (i + 1 < src.size()) ? src[i + 1] : '\0';
        switch(st){
            case NORM:
                if(c == '"'){ st = STR; clean += c; }
                else if(c == '/' && n == '/'){ st = LINEC; i++; }
                else if(c == '/' && n == '*'){ st = BLOCKC; i++; }
                else clean += c;
                break;
            case STR:
                clean += c;
                if(c == '"') st = NORM;                 // I6 strings have no escaped `"`
                break;
            case LINEC:
                if(c == '\n'){ st = NORM; clean += c; } // keep the newline
                break;
            case BLOCKC:
                if(c == '*' && n == '/'){ st = NORM; i++; }
                else if(c == '\n') clean += c;          // preserve line count across the block
                break;
        }
    }

    auto trim = [](const string& s) -> string {
        size_t a = s.find_first_not_of(" \t\r");
        if(a == string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    auto splitTokens = [](const string& s, bool lower) -> vector<string> {
        vector<string> out;
        string cur;
        for(char c : s){
            if(c == ',' || c == ' ' || c == '\t'){ if(!cur.empty()){ out.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if(!cur.empty()) out.push_back(cur);
        for(string& t : out){
            if(!t.empty() && t[0] == '$') t = t.substr(1);              // params may be written $p or p
            if(lower) for(char& c : t) c = (char)tolower((unsigned char)c);
        }
        return out;
    };

    istringstream in(clean);
    string line;
    while(getline(in, line)){
        string t = trim(line);
        if(t.empty()) continue;                                        // blank / comment-only line
        if(t.rfind("template", 0) != 0) continue;                      // only `template …` headers start a block

        // Header: template <name>(<params>) [triggers <names>] {{{
        string rest = trim(t.substr(8));                               // after the `template` keyword
        size_t lp = rest.find('('), rp = rest.find(')');
        if(lp == string::npos || rp == string::npos || rp < lp) continue;
        string name = trim(rest.substr(0, lp));
        vector<string> params = splitTokens(rest.substr(lp + 1, rp - lp - 1), /*lower=*/false);

        string tail = trim(rest.substr(rp + 1));                       // `triggers X {{{` or `{{{`
        if(tail.size() >= 3 && tail.compare(tail.size() - 3, 3, "{{{") == 0)
            tail = trim(tail.substr(0, tail.size() - 3));
        vector<string> triggers;
        if(tail.rfind("triggers", 0) == 0)
            triggers = splitTokens(trim(tail.substr(8)), /*lower=*/true);

        // Body: every line until one that is exactly `}}}`.
        string body, bl;
        while(getline(in, bl)){
            while(!bl.empty() && bl.back() == '\r') bl.pop_back();
            if(trim(bl) == "}}}") break;
            body += bl + "\n";
        }

        if(!name.empty()){
            builtinTemplates[name] = {params, body};
            if(!triggers.empty()) builtinTemplateTriggers[name] = triggers;
        }
    }
}

// Emit a named built-in template with $param substitution and indentation applied.
// A parameterless template's body as text (trimmed), firing its triggers as applyTemplate does.
string i6Emitter::templateText(const string& name){
    auto it = builtinTemplates.find(name);
    if(it == builtinTemplates.end()) return "[missing builtin template: " + name + "]";
    if(auto trig = builtinTemplateTriggers.find(name); trig != builtinTemplateTriggers.end())
        for(const string& tname : trig->second)
            languageService.firedStoredNames.insert(tname);
    const string& body = it->second.second;
    size_t b = body.find_first_not_of(" \t\r\n"), e = body.find_last_not_of(" \t\r\n");
    return b == string::npos ? "" : body.substr(b, e - b + 1);
}

static string groupFrameSlots(string text);

void i6Emitter::applyTemplate(string name, map<string,string> args, string indent){
    auto it = builtinTemplates.find(name);
    if(it == builtinTemplates.end()){
        out << indent << "! [missing builtin template: " << name << "]\n";
        return;
    }
    // Fire any ##triggerEmitter annotations recorded for this template — gates the
    // matching #storedEmitFirst/#storedEmitLast blocks for emission in resolvedOutput().
    auto trigIt = builtinTemplateTriggers.find(name);
    if(trigIt != builtinTemplateTriggers.end())
        for(const string& tname : trigIt->second)
            languageService.firedStoredNames.insert(tname);
    string body = it->second.second;
    // substitute $param tokens (word-boundary aware)
    for(auto& [param, val] : args){
        string from = "$" + param;
        size_t pos = 0;
        while((pos = body.find(from, pos)) != string::npos){
            bool leftOk  = pos==0 || !(isalnum(body[pos-1]) || body[pos-1]=='_');
            bool rightOk = pos+from.size()>=body.size() || !(isalnum(body[pos+from.size()]) || body[pos+from.size()]=='_');
            if(leftOk && rightOk){ body.replace(pos, from.size(), val); pos+=val.size(); }
            else pos+=from.size();
        }
    }
    if(!currentSpillAliases.empty()) body = groupFrameSlots(body);   // `(_bglFrm-->1)++` in a lifted loop
    // emit each non-blank line with indent prefix
    istringstream ss(body);
    string line;
    while(getline(ss, line)){
        if(line.empty() || line.find_first_not_of(" \t")==string::npos) continue;
        out << indent << line << "\n";
    }
}

// Static word-boundary replacement (also used in emitStatement via call instead of lambda)
// Case-insensitive substring find. Beguile is case-insensitive at the
// language level — tokens are lowercased in the symbol table — but raw i6Body
// text captured by getRawTextThroughClosingBrace preserves the user's
// original case. So a body containing `$stringVal` must still match a
// substitution target of `$stringval` (the lowercased param name).
static size_t findWordCI(const string& haystack, const string& needle, size_t pos){
    if(needle.empty() || haystack.size() < needle.size()) return string::npos;
    size_t end = haystack.size() - needle.size();
    for(size_t i = pos; i <= end; i++){
        bool match = true;
        for(size_t j = 0; j < needle.size(); j++){
            if(tolower((unsigned char)haystack[i+j]) != tolower((unsigned char)needle[j])){
                match = false; break;
            }
        }
        if(match) return i;
    }
    return string::npos;
}

// I6 operator precedence (DM4 §1.8), loosest first; 0 for a token that isn't an operator.
static int i6OpPrecedence(const string& op){
    static const map<string,int> prec = {
        {"=",1}, {"&&",2}, {"||",2}, {"~~",2},
        {"==",3}, {"~=",3}, {">",3}, {"<",3}, {">=",3}, {"<=",3},
        {"has",3}, {"hasnt",3}, {"in",3}, {"notin",3}, {"ofclass",3}, {"provides",3},
        {"or",4}, {"+",5}, {"-",5}, {"*",6}, {"/",6}, {"%",6}, {"&",6}, {"|",6}, {"~",6},
        {"->",7}, {"-->",7}, {"++",9}, {"--",9}, {".",12}, {"(",12}, {"::",13} };
    auto it = prec.find(op);
    return it == prec.end() ? 0 : it->second;
}

static bool isI6WordChar(char c){ return isalnum((unsigned char)c) || c == '_' || c == '$'; }

// The operator token starting at i (symbolic or a word operator), or "" if none.
static string i6OpAt(const string& s, size_t i){
    static const char* sym[] = {"-->", "->", "==", "~=", ">=", "<=", "&&", "||", "~~", "++", "--", "::",
                                "+", "-", "*", "/", "%", "&", "|", "~", ">", "<", "=", ".", "("};
    for(const char* op : sym) if(s.compare(i, strlen(op), op) == 0) return op;
    if(i < s.size() && isI6WordChar(s[i]) && (i == 0 || !isI6WordChar(s[i-1]))){
        size_t j = i; while(j < s.size() && isI6WordChar(s[j])) j++;
        string w = s.substr(i, j - i);
        for(char& c : w) c = (char)tolower((unsigned char)c);
        if(i6OpPrecedence(w) != 0) return w;
    }
    return "";
}

// The loosest operator at the top level of an expression's text: its precedence, whether every
// operator at that level is `op` (so an associative op needs no brackets), and whether it is a
// prefix. INT_MAX when atomic; -1 when the text isn't one balanced expression (statements, a
// routine, or a fragment whose bracket the caller already emitted) and must be left alone.
static int i6LoosestOperator(const string& text, string& op, bool& uniform, bool& prefix){
    int depth = 0, loosest = INT_MAX;
    bool afterOperand = false;
    for(size_t i = 0; i < text.size(); i++){
        char c = text[i];
        if(c == ' ' || c == '\t' || c == '\n') continue;
        if(c == '"' || c == '\''){
            size_t j = text.find(c, i + 1);
            if(j == string::npos) return -1;
            i = j; afterOperand = true; continue;
        }
        if(c == ';' || c == '{' || c == '[') return -1;
        if(c == '(') { depth++; afterOperand = false; continue; }
        if(c == ')') { if(--depth < 0) return -1; afterOperand = true; continue; }
        if(depth != 0) continue;
        string tok = i6OpAt(text, i);
        if(tok.empty() || tok == "." || tok == "(" || tok == "::"){
            if(tok == "." || tok == "::"){ i += tok.size() - 1; if(i + 1 < text.size() && (text[i+1] == '&' || text[i+1] == '#')) i++; afterOperand = false; continue; }
            if(isI6WordChar(c)){ while(i + 1 < text.size() && isI6WordChar(text[i+1])) i++; }
            afterOperand = true; continue;
        }
        bool isPrefix = !afterOperand && (tok == "-" || tok == "~" || tok == "~~");
        int p = (isPrefix && tok != "~~") ? 8          // unary minus / bitwise not
              : (tok == "++" || tok == "--") ? 9
              : i6OpPrecedence(tok);
        if(p < loosest){ loosest = p; op = tok; uniform = true; prefix = isPrefix; }
        else if(p == loosest && !isPrefix) prefix = false;
        else if(p == loosest && tok != op) uniform = false;
        i += tok.size() - 1;
        afterOperand = (tok == "++" || tok == "--") && afterOperand;
    }
    return depth == 0 ? loosest : -1;
}

// Apply fn to the code between string and dictionary-word literals, leaving the literals as they are.
static string mapOutsideLiterals(const string& text, const std::function<string(string)>& fn){
    string mapped;
    size_t i = 0;
    while(i < text.size()){
        size_t q = text.find_first_of("\"'", i);
        mapped += fn(text.substr(i, q == string::npos ? string::npos : q - i));
        if(q == string::npos) break;
        size_t e = text.find(text[q], q + 1);
        e = (e == string::npos) ? text.size() : e + 1;
        mapped += text.substr(q, e - q);
        i = e;
    }
    return mapped;
}

// Bracket each frame slot (`_bglFrm-->N`) that an adjacent ++/--, unary -/~, call `(` or property
// `.` would otherwise bind to: those all bind tighter than `-->`, so `_bglFrm-->3++` increments the 3
// and `_bglFrm-->1()` calls routine 1, not the slot.
// A `required` routine declared without a body: its stub takes no locals, since nothing could use them.
static bool isRequiredStub(functionDef* fd){
    auto* body = dynamic_cast<statementBlock*>(fd->body);
    return fd->isRequired && (fd->body == nullptr || (body != nullptr && body->statements.empty()));
}

static string groupFrameSlots(string text){
    static const string slot = "_bglFrm-->";
    size_t pos = 0;
    while((pos = text.find(slot, pos)) != string::npos){
        size_t end = pos + slot.size();
        while(end < text.size() && isdigit((unsigned char)text[end])) end++;
        size_t r = end; while(r < text.size() && text[r] == ' ') r++;
        size_t l = pos; while(l > 0 && text[l-1] == ' ') l--;
        bool group = text.compare(r, 2, "++") == 0 || text.compare(r, 2, "--") == 0
                  || (r < text.size() && (text[r] == '(' || text[r] == '.'));
        if(!group && l >= 2 && (text.compare(l-2, 2, "++") == 0 || text.compare(l-2, 2, "--") == 0)) group = true;
        if(!group && l >= 2 && text.compare(l-2, 2, "->") == 0) group = true;   // `cells-->(_bglFrm-->0)`
        if(!group && l >= 1 && (text[l-1] == '-' || text[l-1] == '~')){
            size_t b = l - 1; while(b > 0 && text[b-1] == ' ') b--;
            group = b == 0 || !(isI6WordChar(text[b-1]) || text[b-1] == ')' || text[b-1] == ']');
        }
        if(group && !(l > 0 && text[l-1] == '(' && r < text.size() && text[r] == ')')){
            text.insert(end, ")"); text.insert(pos, "(");
            end += 2;
        }
        pos = end;
    }
    return text;
}

string i6Emitter::replaceOperand(string str, const string& from, const string& to){
    if(to.rfind("##", 0) == 0) return replaceWord(str, from, to);   // an action name is one operand
    string valOp; bool uniform = false, prefix = false;
    int valPrec = i6LoosestOperator(to, valOp, uniform, prefix);
    if(valPrec <= 0 || valPrec == INT_MAX) return replaceWord(str, from, to);
    auto associative = [](const string& op){ return op == "+" || op == "*" || op == "&&" || op == "||" || op == "&" || op == "|"; };
    size_t pos = 0;
    while((pos = findWordCI(str, from, pos)) != string::npos){
        bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$' || str[pos-1]=='.');
        // `obj.&width` / `obj.#width` name the property too (a `$token` there is still substituted).
        if(leftOk && pos >= 2 && from[0] != '$' && (str[pos-1]=='&' || str[pos-1]=='#') && str[pos-2]=='.') leftOk = false;
        bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
        if(!(leftOk && rightOk)){ pos += from.size(); continue; }
        bool group = false;
        // Operator on the left: the value is its right operand (binary) or its operand (prefix).
        size_t l = pos;
        while(l > 0 && (str[l-1] == ' ' || str[l-1] == '\t')) l--;
        if(l > 0){
            string lop;
            for(size_t k = (l >= 3 ? l - 3 : 0); k < l && lop.empty(); k++){
                string t = i6OpAt(str, k);
                if(!t.empty() && k + t.size() == l) lop = t;
            }
            if(!lop.empty() && lop != "(" && lop != "="){
                size_t b = l - lop.size();
                while(b > 0 && (str[b-1] == ' ' || str[b-1] == '\t')) b--;
                bool binary = b > 0 && (isI6WordChar(str[b-1]) || str[b-1] == ')' || str[b-1] == ']' || str[b-1] == '"' || str[b-1] == '\'');
                int lp = i6OpPrecedence(lop);
                if(!binary) group = (lop == "~~") ? valPrec <= 2 : true;
                else group = !prefix && (valPrec < lp || (valPrec == lp && !(uniform && valOp == lop && associative(lop))));
            }
        }
        // Operator on the right: the value is its left operand.
        size_t r = pos + from.size();
        while(r < str.size() && (str[r] == ' ' || str[r] == '\t')) r++;
        if(!group && r < str.size()){
            string rop = i6OpAt(str, r);
            if(!rop.empty() && rop != "="){
                int rp = i6OpPrecedence(rop);
                group = valPrec < rp || (valPrec == rp && rp <= 3 && !(uniform && valOp == rop && associative(rop)));
            }
        }
        string rep = group ? "(" + to + ")" : to;
        str.replace(pos, from.size(), rep);
        pos += rep.size();
    }
    return str;
}

string i6Emitter::replaceWord(string str, const string& from, const string& to){
    size_t pos=0;
    while((pos=findWordCI(str,from,pos))!=string::npos){
        // Word-boundary check. '.' on the left disqualifies so we don't rewrite property
        // accesses (`obj.width` must stay intact when renaming a local `width`).
        bool leftOk  = pos==0 || !(isalnum(str[pos-1]) || str[pos-1]=='_' || str[pos-1]=='$' || str[pos-1]=='.');
        // `obj.&width` / `obj.#width` name the property too (a `$token` there is still substituted).
        if(leftOk && pos >= 2 && from[0] != '$' && (str[pos-1]=='&' || str[pos-1]=='#') && str[pos-2]=='.') leftOk = false;
        if(leftOk && pos >= 2 && str.compare(pos - 2, 2, "::") == 0) leftOk = false;   // `obj.Class::prop`
        bool rightOk = pos+from.size()>=str.size() || !(isalnum(str[pos+from.size()]) || str[pos+from.size()]=='_');
        // `##$v` on a verb argument, which already names its action (`##Take`), keeps one `##`.
        if(leftOk && rightOk && pos >= 2 && to.rfind("##", 0) == 0 && str.compare(pos - 2, 2, "##") == 0){
            str.replace(pos - 2, from.size() + 2, to); pos += to.size() - 2;
        }
        else if(leftOk && rightOk){ str.replace(pos,from.size(),to); pos+=to.size(); }
        else pos+=from.size();
    }
    return str;
}

// Resolve whether the named target is a Z-machine variant
static bool isZTarget(const string& t){ return t=="z3"||t=="z5"||t=="z8"; }

// Recursively walk a statementBlock, collecting every variableDeclaration reachable through
// nested control-flow sub-blocks. Deduped by name — I6 allows only one declaration per routine
// header, so the first occurrence of a given name wins and later same-named decls (e.g. a loop
// counter reused in a sibling branch) share the slot.
void i6Emitter::collectBodyLocals(statementBlock* body, vector<variableDeclaration*>& out, set<string>& seen){
    if(body == nullptr) return;
    for(statement* s : body->statements){
        if(auto* vd = dynamic_cast<variableDeclaration*>(s)){
            if(seen.insert(vd->name).second) out.push_back(vd);
        } else if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            collectBodyLocals(ifs->thenBlock, out, seen);
            collectBodyLocals(ifs->elseBlock, out, seen);
        } else if(auto* fors = dynamic_cast<forStatement*>(s)){
            collectBodyLocals(fors->body, out, seen);
        } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
            collectBodyLocals(fis->body, out, seen);
        } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
            collectBodyLocals(ws->body, out, seen);
        } else if(auto* ds = dynamic_cast<doStatement*>(s)){
            collectBodyLocals(ds->body, out, seen);
        } else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            for(switchCase* sc : sw->cases) collectBodyLocals(sc->body, out, seen);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            collectBodyLocals(tc->tryBody, out, seen);
            collectBodyLocals(tc->catchBody, out, seen);
        }
    }
}

// Every try/catch in a block, at any depth.
static void collectTryStatements(statementBlock* body, vector<tryCatchStatement*>& out){
    if(body == nullptr) return;
    for(statement* s : body->statements){
        if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            collectTryStatements(ifs->thenBlock, out);
            collectTryStatements(ifs->elseBlock, out);
        } else if(auto* fors = dynamic_cast<forStatement*>(s))   collectTryStatements(fors->body, out);
        else if(auto* fis = dynamic_cast<forInStatement*>(s))    collectTryStatements(fis->body, out);
        else if(auto* ws = dynamic_cast<whileStatement*>(s))     collectTryStatements(ws->body, out);
        else if(auto* ds = dynamic_cast<doStatement*>(s))        collectTryStatements(ds->body, out);
        else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            for(switchCase* sc : sw->cases) collectTryStatements(sc->body, out);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            out.push_back(tc);
            collectTryStatements(tc->tryBody, out);
            collectTryStatements(tc->catchBody, out);
        }
    }
}

// What a lifted try body hands back to its caller besides completing: a `return` (at any depth),
// and a `break`/`continue` aimed at a loop outside the try.
struct TryExits { bool ret = false, brk = false, cont = false; };
static void scanTryExits(statementBlock* body, int loopDepth, TryExits& ex){
    if(body == nullptr) return;
    for(statement* s : body->statements){
        if(dynamic_cast<returnStatement*>(s)) ex.ret = true;
        else if(auto* raw = dynamic_cast<i6RawNode*>(s)){
            if(raw->isI6Island || loopDepth > 0) continue;
            if(raw->text == "break;") ex.brk = true;
            else if(raw->text == "continue;") ex.cont = true;
        }
        else if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            scanTryExits(ifs->thenBlock, loopDepth, ex);
            scanTryExits(ifs->elseBlock, loopDepth, ex);
        } else if(auto* fors = dynamic_cast<forStatement*>(s))   scanTryExits(fors->body, loopDepth + 1, ex);
        else if(auto* fis = dynamic_cast<forInStatement*>(s))    scanTryExits(fis->body, loopDepth + 1, ex);
        else if(auto* ws = dynamic_cast<whileStatement*>(s))     scanTryExits(ws->body, loopDepth + 1, ex);
        else if(auto* ds = dynamic_cast<doStatement*>(s))        scanTryExits(ds->body, loopDepth + 1, ex);
        else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            for(switchCase* sc : sw->cases) scanTryExits(sc->body, loopDepth, ex);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            scanTryExits(tc->tryBody, loopDepth, ex);
            scanTryExits(tc->catchBody, loopDepth, ex);
        }
    }
}

bool i6Emitter::funcHasZTry(functionDef* fd){
    if(!isZTarget(currentTarget)) return false;
    vector<tryCatchStatement*> tries;
    collectTryStatements(dynamic_cast<statementBlock*>(fd->body), tries);
    return !tries.empty();
}

vector<string> i6Emitter::glulxTryLocals(functionDef* fd){
    vector<string> names;
    if(isZTarget(currentTarget)) return names;
    vector<tryCatchStatement*> tries;
    collectTryStatements(dynamic_cast<statementBlock*>(fd->body), tries);
    for(tryCatchStatement* tc : tries){
        string id = to_string(tc->id);
        names.push_back("_bgl_cv" + id);
        names.push_back("_bgl_cvs" + id);
        if(frameAllocEmitted) names.push_back("_bgl_ft" + id);
        if(languageService.linqInUse) names.push_back("_bgl_ld" + id);
        names.push_back("_bgl_sf" + id);
        names.push_back("_bgl_sn" + id);
    }
    return names;
}

// A frame-held param's value arrives in its header local; copy it into the frame before any use.
void i6Emitter::emitTryParamCopyIns(const string& indent){
    for(auto& [name, raw] : currentTryParamRaw)
        out << indent << currentSpillAliases[name] << " = " << raw << ";\n";
}

// Per-call set-up of class-typed locals that own an instance. The local is set to its backing (a
// file-scope I6 object, `_bglLocal_<func>_<name>`), so it starts out owning it and an assignment can
// still re-point it. The backing's stored fields are zeroed, since its state would otherwise persist
// across calls: each call sees a fresh instance.
// A verb's action is named for the verb; its object is named apart (`_bglVerb_<name>`).
string i6Emitter::actionNameOf(const string& text){
    string low = text; transform(low.begin(), low.end(), low.begin(), ::tolower);
    for(verbObjectDef* v : languageService.verbs){
        string vi6 = v->i6name; transform(vi6.begin(), vi6.end(), vi6.begin(), ::tolower);
        if(v->name == low || (!vi6.empty() && vi6 == low)) return v->dName();
    }
    return text;
}

bool isHeaderMember(const string& name);

// A routine that calls itself shares its parameters' and locals' instances with every activation:
// each activation keeps the caller's field values in the frame pool from entry until it returns.
void i6Emitter::prepareRecursionSave(functionDef* fd){
    if(!fd->callsItself || fd->ownBackings.empty() || !fd->entrySetUp.empty()) return;
    auto* body = dynamic_cast<statementBlock*>(fd->body);
    if(body == nullptr) return;
    vector<string> slots;
    set<classDef*> visited;
    function<void(classDef*, const string&)> collect = [&](classDef* c, const string& inst){
        if(!c) return;
        for(classDef* base : c->baseClasses) collect(base, inst);
        for(typeMember* m : c->members){
            auto* fm = dynamic_cast<variableDeclaration*>(m);
            if(!fm || fm->isStatic || fm->isPrePassStub || isHeaderMember(fm->name)) continue;
            if(fm->type.name == "attributelist") continue;
            if(fm->type.name == "grammarrulelist" || fm->type.name == "grammarrule") continue;
            if(classDef* ft = languageService.findClass(fm->type.name); ft && ft->isEmitterClass && !ft->isPrimitive) continue;
            if(dynamic_cast<arrayDeclaration*>(fm)) continue;
            if(classDef* owned = ownedFieldClass(fm, visited)){
                visited.insert(owned);
                collect(owned, format("_bglField_{0}_{1}", inst, fm->dName()));
                visited.erase(owned);
                continue;
            }
            slots.push_back(format("{0}.{1}", inst, fm->i6name.empty() ? fm->dName() : fm->i6name));
        }
    };
    for(auto& [backing, typeName] : fd->ownBackings) collect(languageService.findClass(typeName), backing);
    if(slots.empty()) return;
    const string frame = "_bglsv";
    auto* vd = new variableDeclaration();
    vd->name = frame;
    vd->type = languageService.getType("var");
    vd->isSynthetic = true;
    body->statements.insert(body->statements.begin(), vd);
    string save = format("{0} = _bglFrameAlloc({1});", frame, (int)slots.size());
    string restore;
    for(size_t i = 0; i < slots.size(); i++){
        save += format(" {0}-->{1} = {2};", frame, (int)i, slots[i]);
        restore += format("{0} = {1}-->{2}; ", slots[i], frame, (int)i);
    }
    fd->entrySetUp = save;
    fd->cleanups.push_back({frame, restore + format("_bglFrameFree({0});", (int)slots.size())});
}

void i6Emitter::emitOwnedLocalSetup(const vector<variableDeclaration*>& locals, const string& indent){
    for(variableDeclaration* vd : locals){
        if(!vd->isClassLocalWithBacking) continue;
        classDef* cls = languageService.findClass(vd->type.name);
        if(!cls) continue;
        out << format("{0}{1} = {2};\n", indent, spillName(vd->name), vd->backingName);
        // Each field back to its declared value (0 when it has none). A field that owns an instance
        // keeps pointing at it, and that instance's fields are reset in turn.
        set<classDef*> visited;
        function<void(classDef*, const string&)> resetFields = [&](classDef* c, const string& inst){
            if(!c) return;
            for(classDef* base : c->baseClasses) resetFields(base, inst);
            for(typeMember* m : c->members){
                auto* fm = dynamic_cast<variableDeclaration*>(m);
                if(!fm || fm->isStatic || fm->isPrePassStub || isHeaderMember(fm->name)) continue;
                if(fm->type.name == "attributelist") continue;
                if(fm->type.name == "grammarrulelist" || fm->type.name == "grammarrule") continue;
                if(classDef* ft = languageService.findClass(fm->type.name); ft && ft->isEmitterClass && !ft->isPrimitive && !ft->isUnion()) continue;   // compile-time members (parent, children): no property
                if(dynamic_cast<arrayDeclaration*>(fm)) continue;   // inline property data: not a single word
                if(classDef* owned = ownedFieldClass(fm, visited)){
                    visited.insert(owned);
                    resetFields(owned, format("_bglField_{0}_{1}", inst, fm->dName()));
                    visited.erase(owned);
                    continue;
                }
                string value = fm->declaredExpressionValue != nullptr ? staticText(fm->declaredExpressionValue) : "0";
                if(value.empty()) value = "0";
                out << format("{0}{1}.{2} = {3};\n", indent, inst, fm->i6name.empty() ? fm->dName() : fm->i6name, value);
            }
        };
        resetFields(cls, vd->backingName);   // visited starts empty, as in synthesizeFieldBackings
    }
}

// The copy-in for each param that owns an instance (its class copies): at entry the param still holds
// the caller's argument, which is copied into the param's own instance before the param is pointed at
// it. operator= methods are exempt (synthesizeParamBackings), so this is a no-op for them.
// The fall-through cleanups after a body that ends in `return` could never run (the return ran them).
// So does an if/else whose branches both end in one, and a switch with a default whose every case does.
static bool endsInReturn(statementBlock* body){
    if(body == nullptr || body->statements.empty()) return false;
    statement* last = body->statements.back();
    if(dynamic_cast<returnStatement*>(last) != nullptr) return true;
    if(auto* is = dynamic_cast<ifStatement*>(last)) return endsInReturn(is->thenBlock) && endsInReturn(is->elseBlock);
    if(auto* sw = dynamic_cast<switchStatement*>(last)){
        bool hasDefault = false;
        for(switchCase* c : sw->cases){
            if(c->entries.empty()) hasDefault = true;
            if(!endsInReturn(c->body)) return false;
        }
        return hasDefault;
    }
    return false;
}

void i6Emitter::emitParamCopyIns(functionDef* fd, const string& indent){
    if(!fd) return;
    for(paramDef* p : fd->params){
        if(!p->isClassParamWithBacking) continue;
        // Copy the argument into the parameter's own instance, then point the parameter at it. A
        // class with an init/deinit lifecycle has no backing: its copy-in allocates in place.
        string text = p->copyInText;
        if(text.find(kParamArgMarker) != string::npos){
            string arg = templateText("paramCopyIn.arg");
            for(size_t at = text.find(kParamArgMarker); at != string::npos; at = text.find(kParamArgMarker, at + arg.size()))
                text.replace(at, strlen(kParamArgMarker), arg);
        }
        // A parameter past the fifth lives in a spill global on the Z-machine; one named like a
        // property has its local's new name.
        text = spillWord(text);
        out << indent << text << "\n";
        if(!p->backingName.empty())
            out << format("{0}{1} = {2};\n", indent, spillWord(p->name), p->backingName);
    }
}

// Members that belong to an object's I6 header (`Object name "instanceName" parent`) rather than its
// `with` properties.
bool isHeaderMember(const string& name){
    return name == "parent" || name == "instancename";
}

// Check if a function would overflow Z-machine's 15-local limit (needs _bglFrm slot too → 14)
// Returns true if the function needs the frame pool (body locals overflow I6's 14-slot limit).
// Param overflow (>5 params) is handled separately via _bglXPn globals.
bool i6Emitter::funcNeedsSpill(functionDef* fd){
    if(!isZTarget(currentTarget)) return false;
    if(funcHasZTry(fd)) return true;
    int effectiveParams = min((int)fd->params.size(), 5);
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    return (effectiveParams + (int)locals.size()) > 14;
}

// About how many frame-pool words one call of the routine takes: its local arrays, and on Z its spilled
// locals and try frames. An estimate for sizing the pool, not the exact allocation.
int i6Emitter::estimateFrameWords(functionDef* fd){
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    if(body == nullptr) return 0;
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    int words = 0;
    int ws = isZTarget(currentTarget) ? 2 : 4;
    std::function<int(initializerList*)> listWords = [&](initializerList* l){
        int w = (int)l->elements.size() + 3;
        for(expression* e : l->elements) if(auto* in = dynamic_cast<initializerList*>(e)) w += listWords(in);
        return w;
    };
    for(variableDeclaration* vd : locals)
        if(auto* arr = dynamic_cast<arrayDeclaration*>(vd)){
            if(arr->isByteArray) words += 1 + (max(arr->arraySize, 0) + ws - 1) / ws;
            else if(auto* l = dynamic_cast<initializerList*>(arr->declaredExpressionValue)) words += max(listWords(l), arr->arraySize + 3);
            else if(arr->arraySize > 0) words += arr->arraySize + 3;
            else if(!arr->copySource.empty() || !arr->copyLength.empty()) words += 8;
        }
    if(funcNeedsSpill(fd)) words += (int)fd->params.size() + (int)locals.size() + 6;
    return words;
}

bool i6Emitter::funcHasLocalArrays(functionDef* fd){
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    if(body == nullptr) return false;
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    for(variableDeclaration* vd : locals){
        auto* arr = dynamic_cast<arrayDeclaration*>(vd);
        if(!arr) continue;
        // Any local array that draws framePool backing forces pool emission:
        //   • sized word/byte array (arraySize > 0)
        //   • list-initialized word array (arraySize == 0 with an initializer list)
        if(arr->arraySize > 0 || !arr->localInitSource.empty() || !arr->copySource.empty() || !arr->copyLength.empty()) return true;
        if(!arr->isByteArray && dynamic_cast<initializerList*>(arr->declaredExpressionValue)) return true;
    }
    return false;
}

// Build spill map for fd:
//   - excess params  (params[5+])  → _bglXPn globals
//   - overflow body locals          → _bglFrm-->N frame slots
void i6Emitter::buildSpillMap(functionDef* fd){
    clearSpillMap();
    // A routine that returns a value and frees things on the way out (local arrays, deinit) holds
    // the value in a local of its own first: the return expression may read what the cleanups free.
    currentReturnSlot.clear();
    if(auto* rb = dynamic_cast<statementBlock*>(fd->body);
       rb != nullptr && fd->returnType.name != "void" && !fd->returnType.name.empty()
       && (funcHasLocalArrays(fd) || !fd->cleanups.empty())){
        const string slot = "_bglret";
        bool present = false;
        for(statement* st : rb->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(st); vd && vd->name == slot) present = true;
        if(!present){
            auto* vd = new variableDeclaration();
            vd->name = slot;
            vd->type = languageService.getType("var");
            vd->isSynthetic = true;
            rb->statements.insert(rb->statements.begin(), vd);
        }
        currentReturnSlot = slot;
    }
    // Build the per-function display-name map regardless of target. Even on Glulx (no spill),
    // this lets spillName preserve user-chosen casing for params and locals.
    auto rememberDisplay = [&](const string& canonical, const string& display){
        if(!display.empty() && display != canonical)
            currentDisplayNames[canonical] = display;
    };
    for(paramDef* p : fd->params) rememberDisplay(p->name, p->displayName);
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    for(variableDeclaration* vd : locals) rememberDisplay(vd->name, vd->displayName);

    // Property-shadow rename pass — runs on every target (not just Z), since the I6 issue
    // exists regardless of target. Must run AFTER displayName population so the mangled
    // form preserves the user's chosen casing.
    buildLocalRenameMap(fd);

    if(!isZTarget(currentTarget)) return;
    const int maxParams = 5;
    // Map excess params to _bglXPn globals
    for(int i = maxParams; i < (int)fd->params.size(); i++)
        currentSpillAliases[fd->params[i]->name] = format("_bglXP{0}", i - maxParams);
    if(funcHasZTry(fd)){
        for(int i = 0; i < min((int)fd->params.size(), maxParams); i++){
            const string& name = fd->params[i]->name;
            currentTryParamRaw[name] = spillName(name);
            currentSpillAliases[name] = format("_bglFrm-->{0}", currentSpillCount++);
        }
        for(variableDeclaration* vd : locals)
            currentSpillAliases[vd->name] = format("_bglFrm-->{0}", currentSpillCount++);
        vector<tryCatchStatement*> tries;
        collectTryStatements(body, tries);
        int selfSlot = -1;   // self, then sender: a routine's own, so its tries share them
        for(tryCatchStatement* tc : tries){
            int cookie = currentSpillCount++;
            currentTrySlots[tc->id] = {cookie, currentSpillCount++};
            if(languageService.linqInUse) currentTryLinqSlots[tc->id] = currentSpillCount++;
            if(selfSlot < 0){ selfSlot = currentSpillCount; currentSpillCount += 2; }
            currentTrySelfSlots[tc->id] = selfSlot;
        }
        return;
    }
    // Count only the params that fit in I6 locals
    int effectiveParams = min((int)fd->params.size(), maxParams);
    int total = effectiveParams + (int)locals.size();
    if(total <= 14) return;
    int excess = total - 14;
    for(int i = (int)locals.size() - excess; i < (int)locals.size(); i++)
        currentSpillAliases[locals[i]->name] = format("_bglFrm-->{0}", currentSpillCount++);
}

void i6Emitter::clearSpillMap(){
    currentSpillAliases.clear();
    currentDisplayNames.clear();
    currentLocalRenames.clear();
    currentCallEscapes.clear();
    currentTryParamRaw.clear();
    currentTrySlots.clear();
    currentTryLinqSlots.clear();
    currentTrySelfSlots.clear();
    currentSpillCount = 0;
}

// ── property-shadow rename helpers ──────────────────────────────────────────────
// Extract every identifier that appears after a '.' in `tok` and add to `out`.
// `tok` may be a multi-segment chained access like "bgl.glulx.windowOpen" — all
// segments after the first are property accesses. Stops at non-identifier chars.
static void extractPropertyNamesFromToken(const string& tok, set<string>& out){
    size_t pos = 0;
    while((pos = tok.find('.', pos)) != string::npos){
        pos++;
        size_t end = pos;
        while(end < tok.size() && (isalnum((unsigned char)tok[end]) || tok[end] == '_')) end++;
        if(end > pos) out.insert(tok.substr(pos, end - pos));
        pos = end;
    }
}

// Same as token version, but for free-form raw-I6 text fragments (returnExpression,
// for-loop init/increment, raw #i6{} text, emitter bodies). Conservative: any `.NAME`
// pattern is treated as a property access.
static void extractPropertyNamesFromText(const string& text, set<string>& out){
    extractPropertyNamesFromToken(text, out);
}

static void extractPropertyNamesFromExpr(expression* expr, set<string>& out){
    if(!expr) return;
    for(const string& t : expr->tokens) extractPropertyNamesFromToken(t, out);
    if(auto* il = dynamic_cast<initializerList*>(expr))
        for(expression* e : il->elements) extractPropertyNamesFromExpr(e, out);
}

// Recursively walk a statementBlock collecting every identifier used as a property
// accessor (`obj.NAME`) in any expression or raw-text fragment. Used to detect locals
// that would shadow I6 property names.
static void collectDottedAccessNames(statementBlock* body, set<string>& out){
    if(!body) return;
    for(statement* s : body->statements){
        if(auto* asg = dynamic_cast<assignmentStatement*>(s)){
            extractPropertyNamesFromExpr(asg->assignedExpression, out);
            for(auto& seg : asg->interpSegments) extractPropertyNamesFromExpr(seg.expr, out);
        } else if(auto* vd = dynamic_cast<variableDeclaration*>(s)){
            extractPropertyNamesFromExpr(vd->declaredExpressionValue, out);
            for(auto& seg : vd->interpSegments) extractPropertyNamesFromExpr(seg.expr, out);
        } else if(auto* fcs = dynamic_cast<functionCallStatement*>(s)){
            for(expression* e : fcs->args) extractPropertyNamesFromExpr(e, out);
            extractPropertyNamesFromText(fcs->emitterBody, out);
            for(auto& segs : fcs->interpSegmentsPerArg)
                for(auto& seg : segs) extractPropertyNamesFromExpr(seg.expr, out);
        } else if(auto* rs = dynamic_cast<returnStatement*>(s)){
            extractPropertyNamesFromText(rs->returnExpression, out);
        } else if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            extractPropertyNamesFromExpr(ifs->condition, out);
            collectDottedAccessNames(ifs->thenBlock, out);
            collectDottedAccessNames(ifs->elseBlock, out);
        } else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            extractPropertyNamesFromExpr(sw->condition, out);
            for(switchCase* sc : sw->cases){
                for(auto& ce : sc->entries){
                    extractPropertyNamesFromExpr(ce.value, out);
                    extractPropertyNamesFromExpr(ce.rangeLow, out);
                    extractPropertyNamesFromExpr(ce.rangeHigh, out);
                }
                collectDottedAccessNames(sc->body, out);
            }
        } else if(auto* ds = dynamic_cast<doStatement*>(s)){
            extractPropertyNamesFromExpr(ds->condition, out);
            collectDottedAccessNames(ds->body, out);
        } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
            extractPropertyNamesFromExpr(ws->condition, out);
            collectDottedAccessNames(ws->body, out);
        } else if(auto* fors = dynamic_cast<forStatement*>(s)){
            extractPropertyNamesFromExpr(fors->condition, out);
            extractPropertyNamesFromText(fors->initText, out);
            extractPropertyNamesFromText(fors->incrementText, out);
            collectDottedAccessNames(fors->body, out);
        } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
            for(expression* e : fis->inlineElements) extractPropertyNamesFromExpr(e, out);
            collectDottedAccessNames(fis->body, out);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            collectDottedAccessNames(tc->tryBody, out);
            collectDottedAccessNames(tc->catchBody, out);
        } else if(auto* ts = dynamic_cast<throwStatement*>(s)){
            extractPropertyNamesFromExpr(ts->value, out);
        } else if(auto* raw = dynamic_cast<i6RawNode*>(s)){
            extractPropertyNamesFromText(raw->text, out);
            for(auto& part : raw->parts) extractPropertyNamesFromText(part.text, out);
        }
    }
}

// Recursively concatenate the verbatim text of every raw `#i6` block in the body (descending into
// nested control blocks). A param/local whose name appears here is referenced verbatim in raw I6,
// which the emitter never rewrites — so it must NOT be renamed elsewhere (header/body) or the
// references desync. See buildLocalRenameMap.
static void collectRawI6Text(statementBlock* body, string& out){
    if(!body) return;
    for(statement* s : body->statements){
        if(auto* raw = dynamic_cast<i6RawNode*>(s)){
            if(raw->cooked) continue;   // cooked nodes ARE renamed by the emitter — don't block the rename
            if(raw->isI6Island && raw->parts.empty()) continue;   // emitRawNode renames a plain island too
            out += raw->text; out += "\n";
            for(auto& part : raw->parts){ out += part.text; out += "\n"; }
        } else if(auto* ifs = dynamic_cast<ifStatement*>(s)){
            collectRawI6Text(ifs->thenBlock, out); collectRawI6Text(ifs->elseBlock, out);
        } else if(auto* sw = dynamic_cast<switchStatement*>(s)){
            for(switchCase* sc : sw->cases) collectRawI6Text(sc->body, out);
        } else if(auto* ds = dynamic_cast<doStatement*>(s)){ collectRawI6Text(ds->body, out);
        } else if(auto* ws = dynamic_cast<whileStatement*>(s)){ collectRawI6Text(ws->body, out);
        } else if(auto* fors = dynamic_cast<forStatement*>(s)){ collectRawI6Text(fors->body, out);
        } else if(auto* fis = dynamic_cast<forInStatement*>(s)){ collectRawI6Text(fis->body, out);
        } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
            collectRawI6Text(tc->tryBody, out); collectRawI6Text(tc->catchBody, out);
        }
    }
}
// True when `word` appears as a whole identifier (word-boundary) in `text`.
static bool rawTextHasWord(const string& text, const string& word){
    if(word.empty()) return false;
    size_t pos = 0;
    while((pos = text.find(word, pos)) != string::npos){
        bool leftOK  = (pos == 0) || !(isalnum((unsigned char)text[pos-1]) || text[pos-1] == '_');
        size_t end = pos + word.size();
        bool rightOK = (end >= text.size()) || !(isalnum((unsigned char)text[end]) || text[end] == '_');
        if(leftOK && rightOK) return true;
        pos = end;
    }
    return false;
}

void i6Emitter::buildLocalRenameMap(functionDef* fd){
    statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
    if(!body) return;
    set<string> propNames;
    collectDottedAccessNames(body, propNames);
    // Inform 6 reads these as part of a statement or condition when a local carries the name
    // (`move x to to;`, `if (x in in)`), though a global or object of the same name is fine.
    static const set<string> statementWords = {"to", "has", "in"};
    // Raw I6 that nothing renames (compiler-written nodes, islands with parts) keeps a local's name.
    string rawI6; collectRawI6Text(body, rawI6);
    transform(rawI6.begin(), rawI6.end(), rawI6.begin(), ::tolower);
    // Mangle params and locals whose canonical name matches a used property name.
    // Preserve original-case (display name) inside the mangled form for readability.
    auto maybeRename = [&](const string& canonical, const string& display){
        if(currentLocalRenames.count(canonical)) return;
        // Any property of the name counts, not only those this routine reads: I6 takes `obj.x` with a
        // local x in scope as the property numbered by the local, wherever the access sits.
        if(!propNames.count(canonical) && !languageService.isKnownPropertyName(canonical)
           && !fd->globalEscapes.count(canonical) && !statementWords.count(canonical)) return;
        if(rawTextHasWord(rawI6, canonical)) return;   // named in raw I6 nothing renames → keep the raw name
        const string& shown = display.empty() ? canonical : display;
        currentLocalRenames[canonical] = "_l_" + shown;
        // A routine of the name this function calls: a call in the body still reaches the routine.
        if(fd->globalEscapes.count(canonical) && languageService.findGlobalAs<functionDef>(canonical) != nullptr)
            currentCallEscapes[canonical] = canonical;
    };
    for(paramDef* p : fd->params) maybeRename(p->name, p->displayName);
    vector<variableDeclaration*> locals;
    set<string> seen;
    collectBodyLocals(body, locals, seen);
    for(variableDeclaration* vd : locals) maybeRename(vd->name, vd->displayName);
}

// Like expr->text() but substitutes spilled variable names token-by-token, applies
// property-shadow renames, and falls back to original-case display form when known.
// The parser's binary-operator handling folds whole sub-expressions into single
// concatenated tokens (e.g. "width==0"), so per-token lookup alone would miss embedded
// names. After per-token substitution, we run a word-boundary replaceWord pass over
// the assembled result for the spill and rename maps to catch those.
string i6Emitter::exprText(expression* expr){
    if(!expr) return "";
    if(currentSpillAliases.empty() && currentDisplayNames.empty() && currentLocalRenames.empty())
        return expr->text();
    string result;
    for(const string& t : expr->tokens){
        string tok = (t=="!=") ? "~=" : t;
        auto sp = currentSpillAliases.find(tok);
        if(sp != currentSpillAliases.end()){ result += sp->second; continue; }
        auto rn = currentLocalRenames.find(tok);
        if(rn != currentLocalRenames.end()){ result += rn->second; continue; }
        auto dn = currentDisplayNames.find(tok);
        result += (dn != currentDisplayNames.end()) ? dn->second : tok;
    }
    // Catch concatenated sub-expressions (parser folds binary ops like "width==0" into
    // one token). replaceWord is word-boundary-safe and treats '.' as a left disqualifier,
    // so property accesses (`info.width`) stay intact.
    if(currentLocalRenames.empty() && currentSpillAliases.empty()) return result;
    return mapOutsideLiterals(result, [&](string code){
        for(auto& [from, to] : currentSpillAliases)  code = replaceWord(code, from, to);   // a frame slot wins over a rename
        for(auto& [from, to] : currentLocalRenames)  code = replaceWord(code, from, to);
        code = restoreCallEscapes(code);
        return currentSpillAliases.empty() ? code : groupFrameSlots(code);
    });
}

// `_l_count(` (or its frame slot) back to `count(`: the hidden routine, called, is still the routine.
string i6Emitter::restoreCallEscapes(string code){
    for(auto& [name, unused] : currentCallEscapes){
        // The local's own text in this routine: a frame slot when it spilled, else its rename.
        auto sp = currentSpillAliases.find(name);
        auto rn = currentLocalRenames.find(name);
        string renamed = sp != currentSpillAliases.end() ? sp->second : rn != currentLocalRenames.end() ? rn->second : name;
        if(renamed == name) continue;
        string from = renamed + "(";
        for(size_t at = code.find(from); at != string::npos; at = code.find(from, at + name.size() + 1))
            if(at == 0 || !(isalnum((unsigned char)code[at - 1]) || code[at - 1] == '_' || code[at - 1] == '.'))
                code.replace(at, from.size(), name + "(");
    }
    return code;
}

// Single name lookup. Resolution order:
//   1. spill alias  (Z-machine overflow → _bglFrm-->N or _bglXPn)
//   2. local rename (property-shadow avoidance → _l_<name>)
//   3. display name (user's original case)
//   4. canonical name (lowercase fallback)
string i6Emitter::spillName(const string& name){
    auto it = currentSpillAliases.find(name);
    if(it != currentSpillAliases.end()) return it->second;
    auto rn = currentLocalRenames.find(name);
    if(rn != currentLocalRenames.end()) return rn->second;
    auto dn = currentDisplayNames.find(name);
    if(dn != currentDisplayNames.end()) return dn->second;
    // A path (`l.n`, `arr-->i`): rename the locals inside it.
    if(any_of(name.begin(), name.end(), [](char c){ return !(isalnum((unsigned char)c) || c == '_'); }))
        return spillWord(name);
    return name;
}

// Word-boundary substitution of all spill aliases AND local renames in a raw string
// (for initText/incrementText/returnExpression). Renames apply on the same word-boundary
// rules so a property access `obj.width` (where "width" is part of a multi-char span)
// isn't touched by replaceWord, which matches whole tokens only.
string i6Emitter::spillWord(const string& text){
    if(currentSpillAliases.empty() && currentLocalRenames.empty()) return text;
    return mapOutsideLiterals(text, [&](string code){
        for(auto& [from, to] : currentSpillAliases) code = replaceWord(code, from, to);
        for(auto& [from, to] : currentLocalRenames) code = replaceWord(code, from, to);
        code = restoreCallEscapes(code);
        return currentSpillAliases.empty() ? code : groupFrameSlots(code);
    });
}

int i6Emitter::currentLine(){
    const string& s = out.str();
    return (int)count(s.begin(), s.end(), '\n') + 1;
}
void i6Emitter::writeSourceMap(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;
    for(auto& [i6Line, bglFile, bglLine] : sourceMap)
        f << i6Line << "\t" << bglFile << "\t" << bglLine << "\n";
}
void i6Emitter::writeSymbolTable(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;
    // Format: bglName <tab> i6Name <tab> kind
    // Walk languageService.globals; emit variables, objects, functions (non-emitter, non-extern)
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal) continue;
            f << vd->name << "\t" << vd->name << "\tglobal\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal) continue;
            f << od->name << "\t" << od->name << "\tobject\n";
            for(typeMember* m : od->members){
                if(auto* mv = dynamic_cast<variableDeclaration*>(m))
                    f << od->name << "." << mv->name << "\t" << mv->name << "\tproperty\n";
            }
        } else if(auto* fd = dynamic_cast<functionDef*>(node)){
            if(fd->isEmitter || fd->isExternal) continue;
            f << fd->name << "\t" << fd->name << "\tfunction\n";
        }
    }
}
void i6Emitter::writeTypesFile(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;

    // ── Type definitions (class declarations) ─────────────────────────────────
    // One 'type' block per Beguile class; one 'prop' line per property member.
    for(typeDef* node : languageService.globals){
        auto* cd = dynamic_cast<classDef*>(node);
        if(!cd || cd->isEmitterClass || cd->isExternal) continue;
        f << "type " << cd->name << "\n";
        for(typeMember* m : cd->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(!mv || mv->isExternal || mv->type.name.empty()) continue;
            const string& i6n = mv->i6name.empty() ? mv->name : mv->i6name;
            f << "  prop " << mv->name << " " << i6n << " " << mv->type.name << "\n";
        }
    }

    // ── Routine local variables ───────────────────────────────────────────────
    // One 'routine' block per compiled function; 'local' lines for params + locals.
    // Uses the same I6 name the function is emitted under so the adapter can
    // match against the .dbg <routine> <identifier>.
    for(typeDef* node : languageService.globals){
        auto* fd = dynamic_cast<functionDef*>(node);
        if(!fd || fd->isEmitter || fd->isExternal) continue;
        const string& funcI6n = fd->i6name.empty() ? fd->name : fd->i6name;
        f << "routine " << funcI6n << "\n";
        // Storage location per local: `slot` = a direct I6 routine local; `_bglFrm-->N` = spilled
        // into the frame pool at offset N; `_bglXPn` = an excess-param global. Lets the debugger
        // read a value that isn't in the VM's local frame. Format: `local <name> <type> <storage>`.
        auto sit = routineSpillAliases.find(funcI6n);
        const map<string,string>* spills = (sit != routineSpillAliases.end()) ? &sit->second : nullptr;
        auto storage = [&](const string& nm) -> string {
            if(spills){ auto it = spills->find(nm); if(it != spills->end()) return it->second; }
            return "slot";
        };
        for(paramDef* p : fd->params){
            if(p->type.name.empty() || p->type.name == "void") continue;
            f << "  local " << p->name << " " << p->type.name << " " << storage(p->name) << "\n";
        }
        auto* body = dynamic_cast<statementBlock*>(fd->body);
        if(body){
            for(statement* s : body->statements){
                auto* vd = dynamic_cast<variableDeclaration*>(s);
                if(!vd || vd->type.name.empty()) continue;
                // Reconstruct array<elementType> for local array declarations: a sized local like
                // `array<var> ev[2]` carries type.name "array" (element type only on the arrayDecl),
                // so without this the debugger sees bare "array" and can't render element values.
                // Mirrors the member `propTypeName` lambda below.
                string localTn = vd->type.name;
                if(auto* arr = dynamic_cast<arrayDeclaration*>(vd); arr && !arr->elementType.empty()){
                    localTn = (arr->isRaw ? "rawarray<" : "array<") + arr->elementType + ">";
                    // Append the declared element count as array<T>[N]. The debugger uses this
                    // compile-time size for the element count instead of the runtime header word,
                    // which can be overwritten (e.g. a buffer handed to glk_select), so it can still
                    // enumerate all elements when the in-memory tracked header is clobbered.
                    if(arr->arraySize > 0) localTn += "[" + std::to_string(arr->arraySize) + "]";
                }
                f << "  local " << vd->name << " " << localTn << " " << storage(vd->name)
                  << (vd->isSynthetic ? " synthetic" : "") << "\n";
            }
        }
        // Synthetic frame pointer, present only when the routine spills. Marked `synthetic` so the
        // debugger hides it from the locals view (the robust replacement for a `_bgl` name heuristic).
        auto cit = routineSpillCounts.find(funcI6n);
        if(cit != routineSpillCounts.end() && cit->second > 0)
            f << "  local _bglFrm int slot synthetic\n";
    }

    // ── Global variables ──────────────────────────────────────────────────────
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal || vd->type.name.empty()) continue;
            f << "global " << vd->name << " " << vd->type.name << "\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal || !od->objectClass) continue;
            f << "global " << od->name << " " << od->objectClass->name << "\n";
        }
    }
}

// Emit one `[types]` `routine <name>` block with its typed params + locals. Shared by top-level
// functions and class/object member methods so method-local vars are typed for the debugger.
void i6Emitter::emitRoutineLocalTypes(std::ostream& f, functionDef* fd, const std::string& routineName){
    f << "routine " << routineName << "\n";
    // Storage per local: `slot` = direct I6 routine local; `_bglFrm-->N` = spilled into the frame
    // pool; `_bglXPn` = excess-param global. Format: `local <name> <type> <storage>`.
    const string funcI6n = fd->i6name.empty() ? fd->name : fd->i6name;
    auto sit = routineSpillAliases.find(funcI6n);
    const map<string,string>* spills = (sit != routineSpillAliases.end()) ? &sit->second : nullptr;
    auto storage = [&](const string& nm) -> string {
        if(spills){ auto it = spills->find(nm); if(it != spills->end()) return it->second; }
        return "slot";
    };
    for(paramDef* p : fd->params){
        if(p->type.name.empty() || p->type.name == "void") continue;
        f << "  local " << p->name << " " << p->type.name << " " << storage(p->name) << "\n";
    }
    if(auto* body = dynamic_cast<statementBlock*>(fd->body)){
        for(statement* s : body->statements){
            auto* vd = dynamic_cast<variableDeclaration*>(s);
            if(!vd || vd->type.name.empty()) continue;
            // Reconstruct array<elem>[N] / rawarray<elem>[N] from the arrayDeclaration (type.name is
            // bare "array"); the debugger uses the compile-time [N] as the element count.
            string localTn = vd->type.name;
            if(auto* arr = dynamic_cast<arrayDeclaration*>(vd); arr && !arr->elementType.empty()){
                localTn = (arr->isRaw ? "rawarray<" : "array<") + arr->elementType + ">";
                if(arr->arraySize > 0) localTn += "[" + std::to_string(arr->arraySize) + "]";
            }
            f << "  local " << vd->name << " " << localTn << " " << storage(vd->name)
              << (vd->isSynthetic ? " synthetic" : "") << "\n";
        }
    }
    auto cit = routineSpillCounts.find(funcI6n);
    if(cit != routineSpillCounts.end() && cit->second > 0)
        f << "  local _bglFrm int slot synthetic\n";
}

/**
 * writeDebugBundle — writes a single .bgldbg file containing the source map,
 * symbol table, and type information that the VS Code extension needs for
 * source-level debugging.  Format is section-delimited plain text:
 *
 *   [map]
 *   <source-map lines>
 *   [sym]
 *   <symbol-table lines>
 *   [types]
 *   <type-info lines>
 *
 * The I6 compiler writes its own debug file (.dbg) separately; this bundle
 * collects everything beguiler itself knows about the compiled program.
 */
void i6Emitter::writeDebugBundle(const string& path){
    ofstream f(path);
    if(!f.is_open()) return;

    // ── [map] section ─────────────────────────────────────────────────────────
    f << "[map]\n";
    for(auto& [i6Line, bglFile, bglLine] : sourceMap)
        f << i6Line << "\t" << bglFile << "\t" << bglLine << "\n";

    // ── [sym] section ─────────────────────────────────────────────────────────
    f << "[sym]\n";
    // A `superposed` node that was never referenced is still sitting in superposedBlocks — resolvedOutput()
    // only erases the ones it materialized, and it ran before this (writeFile precedes writeDebugBundle).
    // Such a node has no routine/object in the final .inf, so listing it as a symbol points the debugger
    // at a nonexistent addr 0. Skip it. (Key matches the capture: i6name, else display name.)
    auto unplacedSuperposed = [&](bool isSup, const string& i6n, const string& dn){
        return isSup && superposedBlocks.count(i6n.empty() ? dn : i6n) > 0;
    };
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->isExternal) continue;
            if(unplacedSuperposed(vd->isSuperposed, vd->i6name, vd->dName())) continue;
            f << vd->name << "\t" << vd->name << "\tglobal\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal) continue;
            if(unplacedSuperposed(od->isSuperposed, od->i6name, od->dName())) continue;
            f << od->name << "\t" << od->name << "\tobject\n";
            for(typeMember* m : od->members){
                if(auto* mv = dynamic_cast<variableDeclaration*>(m)){
                    if(isHeaderMember(mv->name)) continue;
                    f << od->name << "." << mv->name << "\t" << mv->name << "\tproperty\n";
                }
            }
        } else if(auto* fd = dynamic_cast<functionDef*>(node)){
            if(fd->isEmitter || fd->isExternal) continue;
            if(unplacedSuperposed(fd->isSuperposed, fd->i6name, fd->dName())) continue;
            f << fd->name << "\t" << fd->name << "\tfunction\n";
        }
    }
    // Extern objects — emit sym entries so the debugger can expand them
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(!vd->isExternal) continue;
            // Extern variables of object type — emit as object so debugger can expand
            if(languageService.findObjectType(vd->type.name) != nullptr
               || isObjectBackedClass(languageService.findClass(vd->type.name)))
                f << vd->name << "\t" << vd->name << "\tobject\n";
            else
                f << vd->name << "\t" << vd->name << "\tglobal\n";
        }
    }
    for(verbObjectDef* v : languageService.verbs){
        if(!v->isExternal) continue;
        f << v->name << "\t" << v->name << "\tobject\n";
    }

    // ── [types] section ───────────────────────────────────────────────────────
    f << "[types]\n";
    // Enum declarations
    for(typeDef* node : languageService.globals){
        auto* ed = dynamic_cast<enumDef*>(node);
        if(!ed) continue;
        f << "enum " << ed->name << "\n";
        for(enumValueDef* v : ed->namedValues)
            f << "  value " << v->name << " " << v->value << "\n";
    }
    // Helper lambda: resolve the debug type string for a member variable.
    // arrayDeclaration nodes emit "array<elementType>" so the debugger can
    // distinguish e.g. array<dictionaryWord> from plain int arrays.
    auto propTypeName = [](variableDeclaration* mv) -> string {
        if(auto* arr = dynamic_cast<arrayDeclaration*>(mv); arr && !arr->elementType.empty())
            return "array<" + arr->elementType + ">";
        return mv->type.name;
    };
    // Recover a member's declared type from the object's class hierarchy (incl. extern bases). An
    // inference-typed instance assignment like `name = {.a,.b}` (type inherited from the class) leaves
    // the instance member's own type.name empty, so without this the debugger can't decode it (e.g. an
    // object's `name` array<dictionaryword> would render as a raw number).
    auto inheritedMemberType = [&](classDef* cd, const string& nm) -> string {
        if(!cd) return "";
        typeMember* cm = cd->findMember([&](typeMember* m){
            auto* cmv = dynamic_cast<variableDeclaration*>(m);
            return cmv != nullptr && cmv->name == nm && !propTypeName(cmv).empty();
        });
        return cm == nullptr ? "" : propTypeName(dynamic_cast<variableDeclaration*>(cm));
    };

    // Class declarations
    for(typeDef* node : languageService.globals){
        auto* cd = dynamic_cast<classDef*>(node);
        if(!cd || cd->isEmitterClass || cd->isExternal) continue;
        f << "type " << cd->name << "\n";
        for(typeMember* m : cd->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(!mv || mv->isExternal || mv->type.name.empty()) continue;
            if(isHeaderMember(mv->name)) continue; // part of the object's header, not a real property
            const string& i6n = mv->i6name.empty() ? mv->name : mv->i6name;
            f << "  prop " << mv->name << " " << i6n << " " << propTypeName(mv) << "\n";
        }
    }
    // Object instances — emit a type entry for each object that has its own properties.
    // This covers bare objects (no class) and class-typed objects with properties unique
    // to the instance (e.g. 'name' on a specific object not defined on its class).
    for(typeDef* node : languageService.globals){
        auto* od = dynamic_cast<objectDef*>(node);
        if(!od || od->isExternal) continue;
        // Debug type of an instance member: its own type if it has one, else the type inherited from
        // the object's class (so inference-typed assignments like `name = {.a,.b}` still emit). Empty
        // = skip (parent slot, grammar/attribute props, or genuinely untyped).
        auto memberDbgType = [&](variableDeclaration* mv) -> string {
            if(!mv || mv->isExternal || isHeaderMember(mv->name)) return "";
            string tn = !mv->type.name.empty() ? propTypeName(mv)
                                               : inheritedMemberType(od->objectClass, mv->name);
            if(tn.empty() || tn == "grammartable" || tn == "grammarrulelist"
               || tn == "grammarrule" || tn == "attributelist") return "";
            return tn;
        };
        bool hasProps = false;
        for(typeMember* m : od->members)
            if(!memberDbgType(dynamic_cast<variableDeclaration*>(m)).empty()){ hasProps = true; break; }
        if(!hasProps) continue;
        f << "type " << od->name << "\n";
        for(typeMember* m : od->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            string tn = memberDbgType(mv);
            if(tn.empty()) continue;
            const string& i6n = mv->i6name.empty() ? mv->name : mv->i6name;
            f << "  prop " << mv->name << " " << i6n << " " << tn << "\n";
        }
    }
    // Routine locals — top-level functions, then class/object member methods. Method locals must
    // be included too: without them a method-local var (e.g. an `extend _bglUi` routine's rawArray
    // buffer) shows untyped (a raw number) in the debugger. Member methods key on the .dbg routine name
    // `<owner>.<method>` (e.g. `_bglUi.waitforkey`); routineTyped lookup is case-insensitive.
    for(typeDef* node : languageService.globals){
        if(auto* fd = dynamic_cast<functionDef*>(node)){
            if(fd->isEmitter || fd->isExternal) continue;
            emitRoutineLocalTypes(f, fd, fd->i6name.empty() ? fd->name : fd->i6name);
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            for(typeMember* m : od->members)
                if(auto* mfd = dynamic_cast<functionDef*>(m))
                    if(!mfd->isEmitter && !mfd->isExternal)
                        emitRoutineLocalTypes(f, mfd, od->dName() + "." + (mfd->i6name.empty() ? mfd->name : mfd->i6name));
        } else if(auto* cd = dynamic_cast<classDef*>(node)){
            if(cd->isExternal) continue;
            // Class methods: inform6 names the embedded routine `<class>::<method>` (e.g.
            // `widget::meth`) — a `::` separator, unlike an object instance's `<obj>.<method>`.
            for(typeMember* m : cd->members)
                if(auto* mfd = dynamic_cast<functionDef*>(m))
                    if(!mfd->isEmitter && !mfd->isExternal)
                        emitRoutineLocalTypes(f, mfd, cd->dName() + "::" + (mfd->i6name.empty() ? mfd->name : mfd->i6name));
        }
    }
    // Global variables
    for(typeDef* node : languageService.globals){
        if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
            if(vd->type.name.empty()) continue;
            f << "global " << vd->name << " " << vd->type.name << "\n";
        } else if(auto* od = dynamic_cast<objectDef*>(node)){
            if(od->isExternal) continue;  // object stubs have no properties to expand
            if(od->objectClass)
                f << "global " << od->name << " " << od->objectClass->name << "\n";
            else
                f << "global " << od->name << " " << od->name << "\n";
        }
    }
}

// Pure-I6 .inf-mode fast path: emits only the user's own header/raw body/trailer; true when it handled everything.
bool i6Emitter::emitPhaseInfModeRawOnly(vector<typeDef*>& nodeList){
    if(!languageService.isInfMode || languageService.sawBglIsland) return false;
    if(!languageService.infHeader.empty())
        out << languageService.infHeader;
    for(typeDef* node : nodeList)
        if(dynamic_cast<i6RawNode*>(node))
            generateI6(node);
    return true;   // the trailer is appended by resolvedOutput(), after any superposed blocks
}

// The leading ICL block (user's `!%` in .inf-mode, else synthesised) plus target/framePool state.
void i6Emitter::emitPhaseIclAndTarget(){
if(!languageService.infHeader.empty())
    out << languageService.infHeader;
else
    emitICL(&beguilerSettings);
currentTarget = beguilerSettings.target;
for(char& c : currentTarget) c = (char)tolower(c);
framePoolSize = beguilerSettings.framePoolSize;
}

// Scratch globals (ternary temps, switch temp, try/catch cookie + per-block save slots), each only when used.
void i6Emitter::emitPhaseScratchGlobals(){
// Tells the library's raw I6 which array layout is in force: `<array>`'s length trailer, or the
// core's length in word 0. First, so code emitted ahead of everything else can test it.
if(languageService.arrayInUse)
    out << "Constant _BGL_TRACKED_ARRAYS;\n";
for(int i = 0; i < languageService.ternaryTempCount; i++)
    out << format("global _bgl_temp{0};\n", i);
if(languageService.switchTempNeeded)
    out << "global _bgl_sw;\n";
if(languageService.tryCatchNeeded){
    out << "global _bgl_catch_cookie;\n";
    out << "global _bgl_throwv;\n";
    if(isZTarget(currentTarget)){
        out << "global _bgl_thrown;\n";      // set by a throw, so a lifted try's return value reads as thrown
        out << "global _bgl_tryrc;\n";       // the lifted try's result: thrown value, or 0/1/2/3 (done/return/break/continue)
        out << "global _bgl_tryret;\n";      // the value of a `return` inside the try
    } else
        out << "global _bgl_tryret;\n";      // a `return`'s value, computed before the try's cookie is restored
}
}

// Scans every function/method for framePool need and Z-machine excess-param count, then emits both.
void i6Emitter::emitPhaseRuntimeNeedsScan(vector<typeDef*>& nodeList){
    bool needsPool = false;
    int maxXP = 0;
    // The pool holds the largest routine frame a nominal four calls deep, unless the program sets its size;
    // a routine that calls itself holding local storage is checked against a size the program set.
    const int depth = 4;
    int maxFrame = 0;
    vector<pair<functionDef*, int>> recursive;
    auto scanFd = [&](functionDef* fd){
        if(fd->isEmitter || fd->isExternal) return;
        if(funcNeedsSpill(fd) || funcHasLocalArrays(fd) || (fd->callsItself && !fd->ownBackings.empty())) needsPool = true;
        maxXP = max(maxXP, max(0, (int)fd->params.size() - 5));
        int words = estimateFrameWords(fd);
        maxFrame = max(maxFrame, words);
        if(fd->callsItself && words > 0 && funcHasLocalArrays(fd)) recursive.push_back({fd, words});
    };
    for(typeDef* node : nodeList){
        if(auto* fd = dynamic_cast<functionDef*>(node)) scanFd(fd);
        else if(auto* cd = dynamic_cast<classDef*>(node)) {
            for(typeMember* m : cd->members)
                if(auto* fd = dynamic_cast<functionDef*>(m)) scanFd(fd);
        }
        else if(auto* obj = dynamic_cast<objectDef*>(node))
            // verbObjectDef is-a objectDef, so verbs land here too: scan ALL their member
            // functions (the handler + any helpers). scanFd skips emitters, so the verb's
            // perform()/operator== emitters are ignored automatically — no need to single out
            // the handler by name.
            for(typeMember* m : obj->members)
                if(auto* fd = dynamic_cast<functionDef*>(m)) scanFd(fd);
    }
    // Static methods emit from the type registry (emitAllStaticClassRoutines), which also holds
    // extern classes that never reach nodeList.
    for(typeDef* td : languageService.objectTypes)
        if(auto* cd = dynamic_cast<classDef*>(td))
            for(typeMember* m : cd->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd != nullptr && fd->isStatic) scanFd(fd);
    if(needsPool && !beguilerSettings.framePoolSizeSet)
        framePoolSize = max(framePoolSize, maxFrame * depth);
    if(needsPool && beguilerSettings.framePoolSizeSet)
        for(auto& [fd, words] : recursive)
            if(words * depth > framePoolSize)
                cerr << format("{0}:{1}:1: warning: '{2}' calls itself and takes about {3} words of the frame pool "
                               "per call; framePoolSize = {4} holds {5} of its calls. Raise framePoolSize if it goes "
                               "deeper.\n", fd->src.file, fd->src.line, fd->dName(), words, framePoolSize,
                               framePoolSize / words);
    if(needsPool){
        applyTemplate("framePool", {{"size", to_string(framePoolSize)}}, "");
        frameAllocEmitted = true;
    }
    if(isZTarget(currentTarget)){
        for(int i = 0; i < maxXP; i++)
            out << format("global _bglXP{0};\n", i);
        xpGlobalsNeeded = maxXP;
    }
}

// #emitfirst blocks, plus the placeholder resolvedOutput() replaces with the fired #storedEmitFirst blocks.
void i6Emitter::emitPhaseEmitFirstBlocks(){
for(const string& block : languageService.emitFirstBlocks)
    out << block << "\n";
// Placeholder for #storedEmitFirst blocks — resolved in resolvedOutput() after all
// emission completes (when firedStoredNames is fully populated by applyTemplate calls
// from function-body emission below).
if(!languageService.storedEmitFirstBlocks.empty())
    out << kStoredFirstMarker;
}

// A superposed object is spliced in whole once anything names it; its methods then go one by one.
// A method stays only if its name occurs somewhere outside its own body: I6 property names are
// global, so any occurrence may be a use (a send, `provides`, a property value passed along).
// Repeats until nothing more drops, since a dropped method may have been another's only user.
void i6Emitter::pruneSuperposedObjectMethods(string& buf){
    auto lc = [](string s){ transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    auto countWord = [](const string& hay, const string& word){   // hay and word lowercase
        int n = 0;
        for(size_t p = hay.find(word); p != string::npos; p = hay.find(word, p + 1)){
            char b = p > 0 ? hay[p-1] : ' ', a = p + word.size() < hay.size() ? hay[p + word.size()] : ' ';
            if(!(isalnum((unsigned char)b) || b == '_') && !(isalnum((unsigned char)a) || a == '_')) n++;
        }
        return n;
    };
    bool changed = true;
    while(changed){
        changed = false;
        string lower = lc(buf);
        for(const string& name : splicedSuperposedObjects){
            string head = "\nobject " + name + "\n";
            size_t start = buf.find(head);
            if(start == string::npos) continue;
            start += 1;
            size_t end = buf.find("\n;\n", start);
            if(end == string::npos) continue;
            end += 3;
            // Lines of the block; an item is `  with NAME [` or `       NAME [` through `  ],` / `  ]`.
            vector<string> lines;
            for(size_t p = start; p < end; ){
                size_t e = buf.find('\n', p);
                lines.push_back(buf.substr(p, e - p));
                p = e + 1;
            }
            struct Item { string name; size_t first, last; };
            vector<Item> items;
            static const regex itemHead(R"(^(  with |       )([A-Za-z_]\w*) \[)");
            for(size_t i = 1; i < lines.size(); i++){
                smatch m;
                if(!regex_search(lines[i], m, itemHead)) continue;
                size_t j = i;
                while(j < lines.size() && lines[j] != "  ]," && lines[j] != "  ]") j++;
                if(j == lines.size()){ items.clear(); break; }   // not a shape this pass understands
                items.push_back({m[2].str(), i, j});
                i = j;
            }
            if(items.empty()) continue;
            // Every non-item line between items would be data; leave such objects whole.
            bool onlyItems = true;
            for(size_t k = 0; k + 1 < items.size(); k++) if(items[k].last + 1 != items[k+1].first) onlyItems = false;
            if(!onlyItems || items.front().first != 1 || items.back().last + 2 != lines.size()) continue;
            vector<bool> keep(items.size(), true);
            for(size_t k = 0; k < items.size(); k++){
                string itemText;
                for(size_t i = items[k].first; i <= items[k].last; i++) itemText += lines[i] + "\n";
                string w = lc(items[k].name);
                if(countWord(lower, w) - countWord(lc(itemText), w) <= 0) keep[k] = false;
            }
            if(all_of(keep.begin(), keep.end(), [](bool b){ return b; })) continue;
            // Rebuild the block from the kept items, mapping old block lines to new ones.
            int startLine = (int)std::count(buf.begin(), buf.begin() + start, '\n') + 1;
            vector<int> newLine(lines.size(), -1);
            vector<string> out{lines[0]};
            newLine[0] = 0;
            bool firstKept = true;
            size_t lastKept = items.size();
            for(size_t k = 0; k < items.size(); k++) if(keep[k]) lastKept = k;
            for(size_t k = 0; k < items.size(); k++){
                if(!keep[k]) continue;
                for(size_t i = items[k].first; i <= items[k].last; i++){
                    string l = lines[i];
                    if(i == items[k].first) l = (firstKept ? "  with " : "       ") + l.substr(7);
                    if(i == items[k].last) l = k == lastKept ? "  ]" : "  ],";
                    newLine[i] = (int)out.size();
                    out.push_back(l);
                }
                firstKept = false;
            }
            newLine[lines.size() - 1] = (int)out.size();
            out.push_back(lines.back());   // ";"
            string block;
            for(const string& l : out) block += l + "\n";
            int delta = (int)out.size() - (int)lines.size();
            for(auto& e : sourceMap){
                int& ln = std::get<0>(e);
                if(ln >= startLine && ln < startLine + (int)lines.size()){
                    int nl = newLine[ln - startLine];
                    ln = nl < 0 ? -1 : startLine + nl;
                } else if(ln >= startLine + (int)lines.size()) ln += delta;
            }
            sourceMap.erase(remove_if(sourceMap.begin(), sourceMap.end(),
                [](const tuple<int,string,int>& e){ return std::get<0>(e) < 0; }), sourceMap.end());
            buf.replace(start, end - start, block);
            changed = true;
            break;   // positions moved; rescan from the top
        }
    }
}

// Registers sized-uninitialised tracked word arrays so bglInit can stamp their $9084 magic. Emits nothing.
void i6Emitter::collectTrackedWordArrays(){
if(languageService.arrayInUse){
    for(typeDef* g : languageService.globals){
        auto* arr = dynamic_cast<arrayDeclaration*>(g);
        if(arr == nullptr) continue;
        if(arr->isExternal || arr->isByteArray || arr->isRaw) continue;  // rawArray: no tracking, no magic stamp
        if(arr->isSuperposed) continue;  // superposed arrays may not be emitted; a bglInit stamp
                                         // would dangle. They self-init at their point of use.
        // Sized-uninit only — list-init arrays bake the magic inline.
        if(arr->arraySize > 0 && arr->stringInitializer.empty()
           && dynamic_cast<initializerList*>(arr->declaredExpressionValue) == nullptr){
            trackedArraysNeedingMagicInit.push_back(arr->dName());
        }
    }
}
}

// Registers sized-uninitialised tracked byte arrays and declares the globals bglInit must assign to.
void i6Emitter::emitPhaseTrackedByteArrayGlobals(){
if(languageService.bufInUse){
    for(typeDef* g : languageService.globals){
        auto* arr = dynamic_cast<arrayDeclaration*>(g);
        if(arr == nullptr) continue;
        if(arr->isExternal || !arr->isByteArray) continue;
        if(arr->arraySize > 0 && arr->stringInitializer.empty()
           && dynamic_cast<initializerList*>(arr->declaredExpressionValue) == nullptr){
            trackedByteArraysNeedingMagicInit.push_back(arr->dName());
            // Emit the user-visible pointer Global EARLY (before bglInit) so the
            // runtime assignment `name = name_raw + WORDSIZE + 2;` in bglInit has a
            // declared lvalue to write to. I6 doesn't accept symbolic arithmetic
            // (e.g. `name_raw + WORDSIZE + 2`) as a Global initializer or Constant
            // value, so the assignment lives in bglInit; this just declares the
            // variable. Pass 3's byte-array emission skips re-emitting the global.
            out << format("global {0} = 0;\n", arr->dName());
        }
    }
    // An object's sized `array<char>` members are tracked buffers too: bglInit points each at its raw allocation.
    for(typeDef* g : languageService.globals){
        auto* obj = dynamic_cast<objectDef*>(g);
        if(obj == nullptr || obj->isExternal) continue;
        vector<typeMember*> members = obj->members;
        for(arrayDeclaration* a : inheritedByteArrays(obj)) members.push_back(a);
        for(typeMember* m : members){
            auto* arr = dynamic_cast<arrayDeclaration*>(m);
            if(arr == nullptr || !arr->isByteArray || arr->isRefLocal || arr->isStatic || arr->arraySize <= 0
               || !arr->stringInitializer.empty() || arr->declaredExpressionValue != nullptr) continue;
            string raw = "_" + obj->name + "_" + arr->name + "_raw";
            string objName = obj->i6name.empty() ? obj->name : obj->i6name;
            string prop = arr->i6name.empty() ? arr->dName() : arr->i6name;
            trackedByteMembers.push_back({raw, objName + "." + prop, arr->arraySize});
            trackedByteMemberRaws.insert(raw);
        }
        // A member given a tracked byte array (a pointer set at startup) takes it in bglInit.
        for(typeMember* m : obj->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd == nullptr || vd->declaredExpressionValue == nullptr || dynamic_cast<initializerList*>(vd->declaredExpressionValue)) continue;
            string v = vd->declaredExpressionValue->text();
            string lv = v; transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
            for(const string& t : trackedByteArraysNeedingMagicInit){
                string lt = t; transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
                if(lt != lv) continue;
                string objName = obj->i6name.empty() ? obj->name : obj->i6name;
                runtimeMemberValues.insert(vd);
                runtimeMemberInits.push_back(objName + "." + (vd->i6name.empty() ? vd->dName() : vd->i6name) + " = " + t + ";");
            }
        }
    }
}
// Same reason, for any global bglInit assigns to — a class-typed one whose value is applied
// through its type's operator=, or one whose initializer is not a compile-time constant (see
// recordGlobalVariableInit): the directive has to exist before the routine that writes to it.
// Outside the `bufInUse` guard above, which it used to sit inside — a startup assignment to a
// global is not a `<buf>` matter, and without that extension the declaration came AFTER bglInit,
// leaving I6 with "'=' applied to undeclared variable".
// A global a `#startup` body names is written by bglInit too.
{
    string startupText;
    for(const string& block : languageService.startupBlocks) startupText += block + "\n";
    transform(startupText.begin(), startupText.end(), startupText.begin(), ::tolower);
    if(!startupText.empty())
        for(typeDef* g : languageService.globals)
            if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                if(!vd->isExternal && !vd->isConst && dynamic_cast<arrayDeclaration*>(vd) == nullptr
                   && rawTextHasWord(startupText, vd->name))
                    vd->needsEarlyGlobalDecl = true;
}
for(typeDef* g : languageService.globals){
    auto* vd = dynamic_cast<variableDeclaration*>(g);
    if(vd != nullptr && vd->needsEarlyGlobalDecl && !vd->isExternal && !vd->isConst){
        // Carry the initializer when there is one: this is the whole declaration, moved, and pass
        // 3 skips the name once it is here. A global whose own initializer was deferred has had it
        // cleared, so it declares bare and bglInit assigns it.
        if(vd->declaredExpressionValue != nullptr)
            out << format("global {0} = {1};\n", vd->dName(), staticText(vd->declaredExpressionValue));
        else if(globalOwnsInstance(vd))
            out << format("global {0} = {1};\n", vd->dName(), globalInstanceName(vd));   // its own instance (pass 3)
        else
            out << format("global {0};\n", vd->dName());
        earlyDeclaredGlobals.insert(vd->name);
    }
}
// A class is emitted at its first instance (§18.7), which can come before a constant or another
// class's static that its members name; declare those here, ahead of every class, when their values
// are plain (literals, and constants already declared).
{
    set<string> known;
    auto lower = [](string s){ transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    auto plain = [&](const string& text){
        static const regex lit(R"re("[^"]*"|'[^']*'|\$\$[01]+|\$[0-9A-Fa-f]+)re");
        string rest = regex_replace(text, lit, " ");
        static const regex word(R"([A-Za-z_][A-Za-z0-9_]*)");
        for(sregex_iterator it(rest.begin(), rest.end(), word), end; it != end; ++it)
            if(!known.count(lower(it->str()))) return false;
        return true;
    };
    for(typeDef* g : languageService.globals){
        auto* vd = dynamic_cast<variableDeclaration*>(g);
        if(vd == nullptr || !vd->isConst || vd->isExternal || dynamic_cast<arrayDeclaration*>(vd) != nullptr
           || vd->declaredExpressionValue == nullptr || earlyDeclaredGlobals.count(vd->name)) continue;
        string value = staticText(vd->declaredExpressionValue);
        if(value.empty() || !plain(value)) continue;
        out << format("constant {0} = {1};\n", vd->dName(), value);
        earlyDeclaredGlobals.insert(vd->name);
        known.insert(lower(vd->dName()));
    }
    // Plain variables too: a scalar, string, func or reference global with no value or a plain one.
    for(typeDef* g : languageService.globals){
        auto* vd = dynamic_cast<variableDeclaration*>(g);
        if(vd == nullptr || vd->isConst || vd->isExternal || vd->isStatic || !vd->i6name.empty()
           || dynamic_cast<arrayDeclaration*>(vd) != nullptr || earlyDeclaredGlobals.count(vd->name)
           || globalOwnsInstance(vd)) continue;
        const string& t = vd->type.name;
        bool scalar = t == "int" || t == "uint" || t == "bool" || t == "char" || t == "float" || t == "string"
                   || t == "object" || t.rfind("func<", 0) == 0 || languageService.findEnum(t) != nullptr;
        if(!scalar) continue;
        string value = vd->declaredExpressionValue != nullptr ? staticText(vd->declaredExpressionValue) : "";
        if(!value.empty() && !plain(value)) continue;
        out << "global " << vd->dName() << (value.empty() ? "" : " = " + value) << ";\n";
        earlyDeclaredGlobals.insert(vd->name);
    }
    for(typeDef* g : languageService.globals){
        auto* cd = dynamic_cast<classDef*>(g);
        if(cd == nullptr || cd->isExternal || cd->isEmitterClass || cd->isAlias) continue;
        for(typeMember* m : cd->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd == nullptr || !vd->isStatic || vd->staticHasGlobal || dynamic_cast<arrayDeclaration*>(vd) != nullptr) continue;
            string value = vd->declaredExpressionValue != nullptr ? staticText(vd->declaredExpressionValue) : "";
            if(!value.empty() && !plain(value)) continue;
            string name = format("_bgl_{0}_{1}", cd->dName(), vd->dName());
            out << "global " << name << (value.empty() ? "" : " = " + value) << ";\n";
            earlyStaticGlobals.insert(name);
        }
    }
    // An extern class has no directive of its own to bring its statics; they are declared here.
    for(typeDef* td : languageService.objectTypes){
        auto* cd = dynamic_cast<classDef*>(td);
        if(cd == nullptr || !cd->isExternal || cd->isEmitterClass || cd->isAlias) continue;
        for(typeMember* m : cd->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd == nullptr || !vd->isStatic || vd->isPrePassStub || vd->staticHasGlobal || dynamic_cast<arrayDeclaration*>(vd) != nullptr) continue;
            string name = format("_bgl_{0}_{1}", cd->dName(), vd->dName());
            if(!earlyStaticGlobals.insert(name).second) continue;
            string value = vd->declaredExpressionValue != nullptr ? staticText(vd->declaredExpressionValue) : "";
            out << "global " << name << (value.empty() ? "" : " = " + value) << ";\n";
        }
    }
}
}

// The synthesised bglInit routine: array magic stamps, sized-buffer setup, startup blocks, global inits.
void i6Emitter::emitPhaseBglInit(){
{
    sourceLocation bglInitSrc;
    if(auto* fd = languageService.findGlobalAs<functionDef>("bglinit")) bglInitSrc = fd->src;
    if(!bglInitSrc.file.empty() && bglInitSrc.line > 0)
        pushSourceMap(currentLine() + 1, bglInitSrc.file, bglInitSrc.line);
}
out << "global _bglInitDone = 0;\n";
out << "[bglInit;\n";
out << "    if(_bglInitDone) return;\n";
out << "    _bglInitDone = 1;\n";
// Glulx starts with no output system until the library selects Glk, so text a startup value prints
// into a buffer (a string literal) would be lost.
out << "    #Ifdef TARGET_GLULX; @setiosys 2 0; #Endif;\n";
// Stamp the $9084 magic onto every sized-uninitialized tracked array.
// (List-initialized arrays bake the magic in at compile time and don't need this.)
// Form: `arr-->(arr-->0) = $9084;` — write the last slot of the allocation.
for(const string& arrName : trackedArraysNeedingMagicInit)
    out << "    " << arrName << "-->(" << arrName << "-->0) = $9084;\n";
// Sized-buffer setup for tracked char arrays (`array<char> X[N]` with `<buf>`).
// orLibrary SizedBuffer model — the user-visible pointer is offset WORDSIZE+2
// bytes into the raw allocation so that `X-->0` reads as the I6 hybrid-buffer
// length (matching `print_to_array` / Glk conventions) and the size + magic
// prefix lives at negative offsets:
//
//   raw bytes 0..1:                  magic ($90, $84)
//   raw bytes 2..(WORDSIZE+1):       size WORD (= declared char capacity N)
//   raw bytes (WORDSIZE+2)..(2*WORDSIZE+1):  length WORD (initial 0, I6 zero-init)
//   raw bytes (2*WORDSIZE+2)..end:   N bytes of data
//
//   user-visible X = X_raw + WORDSIZE + 2
//   X-->0  = length     (I6 hybrid buffer convention)
//   X->WORDSIZE..  = data
//   X-->(-1)  = size word (capacity)
//   X->(-WORDSIZE-2..-WORDSIZE-1) = $90 $84 magic
//
// We can't reassign a global array's address, so the user-named symbol is a
// Global pointer variable. Each entry in this vector names the user-visible
// (display-cased) array name; we emit stamping against `<name>_raw` and set
// the pointer here.
for(const string& arrName : trackedByteArraysNeedingMagicInit){
    int N = 0;
    for(typeDef* g : languageService.globals){
        auto* arr = dynamic_cast<arrayDeclaration*>(g);
        if(arr != nullptr && arr->dName() == arrName){ N = arr->arraySize; break; }
    }
    const string raw = arrName + "_raw";
    out << "    " << raw << "->0 = $90;\n";
    out << "    " << raw << "->1 = $84;\n";
    // Size word at byte 2 — `(raw + 2)-->0` does word access at byte address
    // raw+2. Works on both Z (aligned) and Glulx (unaligned word access in
    // memory is permitted per the Glulx spec).
    out << "    (" << raw << " + 2)-->0 = " << N << ";\n";
    // Set the user-visible pointer past the prefix metadata. I6 doesn't
    // accept symbolic arithmetic in Global initializers, so we declare the
    // Global early (= 0) and assign the real value here at startup.
    out << "    " << arrName << " = " << raw << " + WORDSIZE + 2;\n";
    out << "    _bglBuf._registerTracked(" << arrName << ");\n";   // <buf> knows a tracked buffer by its registry
}
for(const TrackedByteMember& t : trackedByteMembers){
    out << "    " << t.raw << "->0 = $90;\n";
    out << "    " << t.raw << "->1 = $84;\n";
    out << "    (" << t.raw << " + 2)-->0 = " << t.size << ";\n";
    out << "    " << t.lvalue << " = " << t.raw << " + WORDSIZE + 2;\n";
    out << "    _bglBuf._registerTracked(" << t.lvalue << ");\n";
}
for(const string& init : runtimeMemberInits)
    out << "    " << init << "\n";
for(const string& block : languageService.startupBlocks)
    out << block << "\n";
for(auto& [varName, body] : languageService.globalInits)
    out << "    " << body << "\n";
out << "];\n";
bglInitHasWork = !trackedArraysNeedingMagicInit.empty() || !trackedByteArraysNeedingMagicInit.empty() || !trackedByteMembers.empty() || !runtimeMemberInits.empty()
              || !languageService.startupBlocks.empty() || !languageService.globalInits.empty();
}

// The main source-order walk, emitting each class ahead of its first instance and capturing superposed ones.
void i6Emitter::emitPhaseSourceOrder(vector<typeDef*>& nodeList){
emittedClasses.clear();   // member (shared with create+populate's emitDeferredBackingClass); reset per emit
auto resolveInstanceClass = [&](typeDef* node) -> classDef* {
    if(auto* od = dynamic_cast<objectDef*>(node)){
        if(dynamic_cast<verbObjectDef*>(node)) return nullptr;
        return od->objectClass;
    }
    if(auto* vd = dynamic_cast<variableDeclaration*>(node)){
        if(vd->isExternal || vd->isConst) return nullptr;
        if(dynamic_cast<arrayDeclaration*>(vd)) return nullptr;
        if(vd->type.name == "attribute" || vd->type.name == "attributelist") return nullptr;
        if(vd->type.name == "property") return nullptr;
        auto* cd = languageService.findClass(vd->type.name);
        if(!cd || cd->isEmitterClass || cd->isAlias || cd->isExternal) return nullptr;
        // A reference-class global holds an object reference, which needs no class declared first;
        // only a value class's global owns an instance of it.
        if(!isValueClass(cd)) return nullptr;
        // Only user classes with stored members require class-before-instance ordering.
        for(typeMember* m : cd->members){
            auto* mv = dynamic_cast<variableDeclaration*>(m);
            if(!mv || mv->isStatic) continue;
            if(mv->type.name == "attributelist") continue;
            if(mv->type.name == "grammarrulelist" || mv->type.name == "grammarrule") continue;
            return cd;
        }
        return nullptr;
    }
    return nullptr;
};
// Track extern attributes as we walk — they become "available" for `has` clauses at the
// position of their `extern attribute X;` declaration. This acts as a compile-time proxy
// for the #includeI6 directive that actually defines the attribute in I6; programmers
// should declare the bindings file before the corresponding #includeI6.
set<string> seenExternAttributes;
// Precompute the set of declared extern attributes so we can distinguish "not yet seen
// but will be declared later" (ordering error) from "never declared" (user typo — let
// I6 surface it since we have no way to verify).
set<string> declaredExternAttributes;
for(typeDef* n : nodeList)
    if(auto* vd = dynamic_cast<variableDeclaration*>(n))
        if(vd->isExternal && vd->type.name == "attribute")
            declaredExternAttributes.insert(vd->name);
// Recursive class emission: base classes are emitted before the class itself so that I6's
// `class Derived class Base with …` declaration sees Base already defined.
std::function<void(classDef*, const char*)> emitClassRecursive = [&](classDef* cd, const char* triggerReason){
    if(!cd || !emittedClasses.insert(cd).second) return;
    for(classDef* base : cd->baseClasses) emitClassRecursive(base, "base class");
    // Extern-attribute check: each `has X` must refer to an extern attribute already
    // declared at this point in source, or a non-extern identifier (which we don't police).
    for(typeMember* m : cd->members){
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        if(!vd || vd->type.name != "attributelist") continue;
        auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue);
        if(!list) continue;
        for(expression* elem : list->elements){
            string attrName = elem->text();
            if(!declaredExternAttributes.count(attrName)) continue;  // not a known extern
            if(seenExternAttributes.count(attrName)) continue;       // already seen — OK
            // Declared later in source — emission here would reference an attribute I6
            // hasn't declared yet. Diagnose with source ordering hint.
            const string& clsLabel = cd->displayName.empty() ? cd->name : cd->displayName;
            string where = vd->src.line > 0 ? format("{0}:{1}:{2}: ", vd->src.file, vd->src.line, max(1, vd->src.col)) : string();
            throw runtime_error(format(
                "{3}ERROR: class '{0}' lists attribute '{1}' (`attributes = {{{1}}}`), but its declaration "
                "`extern attribute {1};` comes later in source (needed by: {2}). Declare the attribute, or "
                "include the bindings file that declares it, before the class.", clsLabel, attrName, triggerReason, where));
        }
    }
    generateI6(cd);
    // If this class was captured as superposed (below) but is now emitted in-place — because a
    // source-order instance or a subclass needs its directive to physically precede them — drop the
    // captured copy so resolvedOutput() doesn't also revive it (which would duplicate the Class).
    if(cd->isSuperposed){
        const string& k = cd->i6Name();
        superposedBlocks.erase(k);
        superposedBlockMaps.erase(k);
        superposedClassBaseNames.erase(k);
    }
};
// Capture a superposed class's OWN `Class` directive into superposedBlocks, keyed by its I6 name,
// so resolvedOutput() revives it only if the name is referenced (`new Foo`, `o ofclass Foo`, …).
// Base classes are recorded (not inlined) so revival can emit them base-first.
auto captureSuperposedClass = [&](classDef* cd){
    stringstream captured;
    std::swap(out, captured);
    bool prevSup = emittingSuperposedBody; emittingSuperposedBody = true;
    vector<tuple<int,string,int>> blockMap;
    vector<tuple<int,string,int>>* prevTarget = sourceMapTarget;
    sourceMapTarget = &blockMap;
    generateI6(cd);                                   // emits just this Class directive
    sourceMapTarget = prevTarget;
    emittingSuperposedBody = prevSup;
    std::swap(out, captured);
    const string& k = cd->i6Name();
    superposedBlocks[k] = captured.str();
    superposedBlockMaps[k] = std::move(blockMap);
    vector<string> baseKeys;
    for(classDef* b : cd->baseClasses) if(b && b->isSuperposed) baseKeys.push_back(b->i6Name());
    superposedClassBaseNames[k] = baseKeys;
};
for(typeDef* node : nodeList){
    // Update extern-attribute availability as we pass each extern declaration.
    if(auto* vd = dynamic_cast<variableDeclaration*>(node))
        if(vd->isExternal && vd->type.name == "attribute")
            seenExternAttributes.insert(vd->name);
    if(auto* cd = dynamic_cast<classDef*>(node)){
        // Superposed class: withhold its source-order emit. Capture the directive keyed by name so
        // resolvedOutput() revives it if referenced (`new`/`ofclass`/typed use), base-first. It may
        // still be emitted in-place earlier/later — by the lazy instance→class path below (a static
        // instance needs the class before it), a subclass's base recursion, or a baked owned member
        // (emitDeferredBackingClass) — in which case the capture is dropped there / above.
        if(cd->isSuperposed){
            if(!emittedClasses.count(cd))    // not already emitted in-place by an earlier instance
                captureSuperposedClass(cd);
            continue;
        }
        emitClassRecursive(cd, "source-order class declaration");
        continue;
    }
    if(classDef* needed = resolveInstanceClass(node))
        emitClassRecursive(needed, "instance declaration");
    generateI6(node);
}
}

// #emitlast blocks, plus the placeholder resolvedOutput() replaces with the fired #storedEmitLast blocks.
void i6Emitter::emitPhaseEmitLastBlocks(){
for(const string& block : languageService.emitLastBlocks)
    out << block << "\n";
// Placeholder for #storedEmitLast blocks — resolved in resolvedOutput().
if(!languageService.storedEmitLastBlocks.empty())
    out << kStoredLastMarker;
}

// .inf-mode trailer: the user's `end;` directive and everything after it, spliced in last.
void i6Emitter::emit(vector<typeDef*>& nodeList){
    // Lift compile-time-only verb fields (`meta`, `priority`) onto their verbObjectDefs and stamp
    // own-block grammar lines with the verb's anchor. Done once up-front so the values are visible
    // to every downstream emit path regardless of source order — grammar objects can target a
    // verb whose own-body declaration comes later in source.
    liftAllVerbCompileTimeFields();
    // Class-typed globals that own an instance, by I6 name: static data (array initializers,
    // property values, global initializers) names the instance, which is what the variable holds
    // at compile time — a variable is not a constant.
    for(typeDef* g : languageService.globals)
        if(auto* vd = dynamic_cast<variableDeclaration*>(g))
            if(globalOwnsInstance(vd)){
                string key = vd->i6name.empty() ? vd->dName() : vd->i6name;
                transform(key.begin(), key.end(), key.begin(), ::tolower);
                ownedGlobalInstances[key] = globalInstanceName(vd);
            }
    synthesizeChildrenPlacement();
    synthesizePooledOwnedMembers();
    synthesizeValueElementPools();

    // .inf-mode with zero #bgl/#bglDecl/#bglStmt islands: the file is pure I6. None of
    // the BLR-derived output (bglInit, _bglUtil, bgl, _glulx, __glkHook, etc.) is
    // referenced by anything, so emit only the user's own content — !% header (if
    // present), the raw I6 body (i6RawNode entries from `globals`), and the trailer.
    // Settings-derived constants and emitFirst/Last blocks are skipped: in .inf-mode
    // with no islands, the user owns ICL via !% and has no way to reach #emitfirst.
    if(emitPhaseInfModeRawOnly(nodeList)) return;

    // .inf-mode: the user's own `!%` ICL block (extracted from the top of the .inf file
    // during parsing) is emitted at the very top, and the compiler's ICL generation is
    // skipped so the user's config remains the sole authority. .bgl-mode keeps the
    // existing behavior — the compiler synthesizes ICL from beguilerSettings.
    emitPhaseIclAndTarget();

    // Pass 2: emit any settings-derived constants
    emitSettingsConstants(&beguilerSettings);

    // Emit scratch variables only when actually used
    emitPhaseScratchGlobals();

    // Pass 2b: scan all functions/methods for runtime needs.
    //
    // Two outputs:
    //   • framePool emission (both targets) — needed if ANY function spills Z-machine
    //     locals OR has local arrays (which use the pool for per-call allocation).
    //   • Z-machine excess-param (XP) globals (Z-only) — for params beyond the 5-arg call limit.
    emitPhaseRuntimeNeedsScan(nodeList);

    // Emit #emitfirst blocks — raw I6 after ICL headers, before bglInit
    emitPhaseEmitFirstBlocks();

    // Pre-pass: scan all globals to identify sized-uninitialized tracked word arrays.
    // These get the compact `Array foo table N+2;` emission (later, in Pass 3) plus
    // a magic-stamp statement in bglInit. We must do this BEFORE bglInit is synthesised
    // so the stamp statements appear in the routine body. Walks variableDeclaration
    // arrayDeclaration instances directly to avoid re-running the global-emission path.
    //
    // Gated on languageService.arrayInUse: programs that don't #include <array> get
    // plain N-slot I6 arrays with no header/magic overhead. Tracked-length semantics
    // are an opt-in via the <array> extension (zero-byte core principle).
    collectTrackedWordArrays();
    // Byte-array analog: when `<buf>` is included, sized-uninit `array<char>` declarations
    // get 4 trailing bytes for length(2) + magic(2). Pre-pass collects them so bglInit can
    // stamp the magic at startup. String-initialized byte arrays opt out — their content
    // length is fixed by the literal and the natural I6 buffer's length-byte is sufficient.
    emitPhaseTrackedByteArrayGlobals();

    // Synthesise bglInit — always emitted, even if empty; guarded against double-call.
    // Anchor a source map entry at the routine header so I6 diagnostics about `bglInit`
    // (most commonly "declared but not used" if no startup blocks contributed) point back
    // to the extern declaration in the core library rather than appearing unmappable.
    // (.inf-mode with zero islands skips this entire emit() body via the early return at
    // the top — the no-Beguile-content fast path.)
    emitPhaseBglInit();

    // Pass 3: emit in source order with lazy class emission. When we encounter an instance
    // (variableDeclaration or objectDef whose declared class hasn't been emitted yet), emit
    // the class first, then the instance. Classes at their original source position are
    // emitted normally (unless already emitted by a prior lazy trigger). This keeps source
    // order everywhere except for classes that must move up ahead of a forward-referenced
    // instance — minimising reordering so extern attribute dependencies stay satisfied.
    emitPhaseSourceOrder(nodeList);

    // `static` class methods: free routines named `_bgl_<Class>_<method>`. Driven from the
    // TYPE registry, not `globals` — extern classes never enter globals, and those are exactly
    // the types (string, float) that need this. Emitted after the main walk so the routines
    // they call are already declared.
    emitAllStaticClassRoutines();

    // Any word evicted via `grammar -= {.w}` that NO native verb reclaimed is a pure removal
    // (disable a library command): emit an empty `Extend only 'w' replace;` after all verb grammar.
    emitEvictions();

    // Emit #emitlast blocks at the very end of the I6 output
    emitPhaseEmitLastBlocks();

    // .inf-mode trailer: the user's `end;` directive (and anything after it) was
    // extracted from the .inf body during parsing and is splice in here so it appears as
    // the last content of the file, after all generated I6.
}
void i6Emitter::emitICL(beguilerSettingsDef* cfg){
    if(cfg->target == "glulx")     out << "!% -G\n";
    else if(cfg->target == "z3")   out << "!% -v3\n";
    else if(cfg->target == "z5")   out << "!% -v5\n";
    else if(cfg->target == "z8")   out << "!% -v8\n";
    if(!cfg->errorFormat.empty())  out << format("!% -E{0}\n", cfg->errorFormat);
    // Omit routines nothing references (user code + BLR helpers). I6's own reachability sees the
    // whole compilation incl. #i6 islands and keeps address-taken routines, so it's safe; disable
    // via `#beguilerSettings { omitUnusedRoutines = false; }` for debug builds.
    if(cfg->omitUnusedRoutines)    out << "!% $OMIT_UNUSED_ROUTINES=1\n";

    // Emit all search paths so I6 can resolve its own internal includes.
    // Uses ++include_path (double +) to ADD to the path rather than replace it.
    // I6 implements ++include_path as a *prepend*: each new line is pushed onto
    // the front of Include_Path. We must therefore emit in REVERSE declaration
    // order so the final left-to-right search order matches what the user wrote.
    // (A single comma-separated line would also work, but I6 has a PATHLEN cap
    // that one consolidated line can blow past — so we keep one line per path.)
    for(auto it = cfg->includePaths.rbegin(); it != cfg->includePaths.rend(); ++it)
        out << "!% ++include_path=" << *it << "\n";
}
void i6Emitter::emitSettingsConstants(beguilerSettingsDef* cfg){
    // Beguiler-presence marker for raw-I6 libraries (e.g. orLibrary) to detect a Beguile
    // compile and version-check, via `#ifdef beguiler` / `#iftrue (beguiler >= NNNN)`. Mirrors
    // the Beguile-side `#if beguiler` symbol (= BEGUILER_VERSION, major*1000+minor*10+patch).
    // The empty self-reference `#Ifdef beguiler;#Endif;` marks the constant "used" so I6 won't
    // warn when a game consumes none of it.
    // beguilerMajor/Minor/Patch stay #define-only — derivable from this one when needed.
    out << "Constant beguiler = " << BEGUILER_VERSION << ";\n";
    out << "#Ifdef beguiler;#Endif;\n";

    if(!cfg->serial.empty())
        out << "Serial \"" << cfg->serial << "\";\n";
    if(cfg->release > 0)
        out << "Release " << cfg->release << ";\n";

    // LINQ scratch buffer capacity — referenced from array.bgl's #emitfirst{} block.
    // Only emitted when `#include <array>` was resolved; the #emitfirst directives
    // that consume this constant are also gated on that include.
    if(languageService.linqInUse)
        out << "Constant _BGL_LINQ_SCRATCH_SIZE = " << (cfg->linqScratchSize > 0 ? cfg->linqScratchSize : 32) << ";\n";

    // for-in literal-list scratch capacity (max elements per `for(x in {a,b,c})`) — referenced from
    // _array.bgl's #storedEmitFirst scratchSupport block. Emitted only when that syntax is
    // used (same gate as the block itself, but the flag is set at parse time so the constant lands
    // in the header ahead of the block). Default 31 mirrors the schema's forInScratchSize.
    if(languageService.forInScratchInUse)
        out << "Constant _BGL_FORIN_SCRATCH_CAP = " << (cfg->forInScratchSize > 0 ? cfg->forInScratchSize : 31) << ";\n";

    // Treaty of Babel IFID: embed as a string so babel tools can find it in the story file
    if(!cfg->ifid.empty()){
        out << "Array UUID_ARRAY string \"UUID://" << cfg->ifid << "//\";\n";
        out << "#Ifdef UUID_ARRAY;#Endif;\n";
    }

    // Note: story/author/headline are NOT auto-emitted here.
    // Declare them explicitly in Beguile source using #beguilerSettings references:
    //   const string story    = #beguilerSettings.title;
    //   const string author   = #beguilerSettings.author;
    //   const string headline = #beguilerSettings.headline;
    //   const int gameRelease = #beguilerSettings.release;
}
void i6Emitter::generateI6(typeDef* node){
     // Skip value emitters — they expand inline at use sites, no standalone I6 output
     if(auto* fd = dynamic_cast<functionDef*>(node)){
         if(fd->isValueEmitter) return;
     }
     // Coarse top-level source anchor. The node's own `src` is stamped onto the .inf line where
     // its emission begins — but only AFTER emission, and only if the node actually produced output
     // (see the push at the end of this function). Pushing after-the-fact self-skips every
     // zero-output node (extern/emitter/extend declarations, and `superposed`/captured bodies whose
     // text goes to a side buffer so currentLine() does not advance here) without enumerating them;
     // those cases are handled by the capture paths + resolvedOutput re-basing. emitFunction /
     // emitStatement push their own finer-grained entries inside.
     sourceLocation s;
     if(auto* fd = dynamic_cast<functionDef*>(node))           s = fd->src;
     else if(auto* cd = dynamic_cast<classDef*>(node))         s = cd->src;
     else if(auto* od = dynamic_cast<objectDef*>(node))        s = od->src;
     else if(auto* ed = dynamic_cast<enumDef*>(node))          s = ed->src;
     else if(auto* vd = dynamic_cast<variableDeclaration*>(node)) s = vd->src;
     else if(auto* rn = dynamic_cast<i6RawNode*>(node))        s = rn->src;
     int anchorPreLine = currentLine();

     if (typeid(*node) == typeid(enumDef))  emitEnum((enumDef*)node);
     else if (typeid(*node) == typeid(classDef)) emitClass((classDef*)node);
     else if (typeid(*node) == typeid(objectDef)) {
         // `superposed` object: withhold its whole `object X with …;` block, keyed by name, and
         // let resolvedOutput() append it only if the name is referenced — same mechanism as
         // superposed routines/globals. An I6 object can be declared after its uses (forward
         // references resolve), so appending at end is valid. Lets an always-declared namespace/
         // helper object (and everything it transitively references) cost nothing until used.
         auto* od = (objectDef*)node;
         if(od->isSuperposed && !emittingSuperposedBody){
             stringstream captured;
             std::swap(out, captured);
             emittingSuperposedBody = true;
             vector<tuple<int,string,int>> blockMap;
             vector<tuple<int,string,int>>* prevTarget = sourceMapTarget;
             sourceMapTarget = &blockMap;       // capture the object's method-body entries per-block
             emitObject(od);
             sourceMapTarget = prevTarget;
             emittingSuperposedBody = false;
             std::swap(out, captured);
             string rName = od->i6name.empty() ? od->dName() : od->i6name;
             superposedBlocks[rName] = captured.str();
             superposedBlockMaps[rName] = std::move(blockMap);
             superposedObjectNames.insert(rName);
         } else {
             emitObject(od);
         }
     }
     else if (typeid(*node) == typeid(verbObjectDef)) emitVerbObject((verbObjectDef*)node);
     else if (auto* gtd = dynamic_cast<grammarRuleListDecl*>(node)) emitGrammarRuleListDecl(gtd);
     else if (auto* vsd = dynamic_cast<verbSynonymDecl*>(node)) emitVerbSynonym(vsd);
     else if (auto* vd = dynamic_cast<variableDeclaration*>(node)) {
         // `superposed` file-scope global/array: withhold its declaration, keyed by name, and
         // let resolvedOutput() append it only if something references the name — same mechanism
         // as superposed routines (i6Emitter emitFunction). Lets always-loaded lookup data cost
         // nothing unless used.
         if(vd->isSuperposed && !emittingSuperposedBody){
             stringstream captured;
             std::swap(out, captured);
             emittingSuperposedBody = true;
             vector<tuple<int,string,int>> blockMap;
             vector<tuple<int,string,int>>* prevTarget = sourceMapTarget;
             sourceMapTarget = &blockMap;
             emitGlobal(vd);
             sourceMapTarget = prevTarget;
             emittingSuperposedBody = false;
             std::swap(out, captured);
             string rName = vd->i6name.empty() ? vd->dName() : vd->i6name;
             superposedBlocks[rName] = captured.str();
             superposedBlockMaps[rName] = std::move(blockMap);
         } else {
             emitGlobal(vd);
         }
     }
     else if (typeid(*node) == typeid(functionDef)) emitFunction((functionDef*)node);
     else if (typeid(*node) == typeid(i6RawNode)) {
         auto* raw = (i6RawNode*)node;
         if(!raw->parts.empty()){
             // Composite raw node: interleave text fragments with embedded Beguile statements.
             // Used when `#i6{}` at global scope contains `#bgl{}` regions whose statements
             // need to emit inline within the surrounding I6 stream. Each part carries its
             // own source location for the text fragment, so per-line source mapping points
             // at the correct .bgl line even across multiple #bgl interjections.
             for(auto& part : raw->parts){
                 if(!part.text.empty()) emitRawTextWithSourceMap(part.text, part.textSrc);
                 if(part.stmt != nullptr) emitStatement(part.stmt, "");
             }
             out << "\n";
         } else {
             emitRawTextWithSourceMap(raw->text, raw->src);
             out << "\n";
         }
     }

     // Coarse anchor, pushed now that emission is done: only if the node produced real output on
     // the main buffer (currentLine advanced). Captured/superposed and zero-output nodes are skipped
     // automatically because currentLine() did not move here.
     if(!s.file.empty() && s.line > 0 && currentLine() > anchorPreLine)
         pushSourceMap(anchorPreLine, s.file, s.line);
}

// Stream `text` to the output while pushing a sourceMap entry whenever a newline is
// emitted, mapping the next .inf line to the corresponding line of the .bgl source.
// `srcStart` carries the file and starting source line of `text`. The caller is expected
// to have already established a sourceMap entry for the .inf line we begin writing on
// (e.g. the coarse entry pushed in generateI6); pushing here only at newline boundaries
// avoids overriding that initial entry.
void i6Emitter::emitRawTextWithSourceMap(const string& text, const sourceLocation& srcStart){
    if(srcStart.file.empty() || srcStart.line <= 0){
        out << text;
        return;
    }
    // The text's own first line, too: a one-line island would otherwise borrow the line of whatever
    // was mapped before it (its routine's header).
    if(!text.empty() && text[0] != '\n') pushSourceMap(currentLine(), srcStart.file, srcStart.line);
    int srcOffset = 0;  // newlines passed within `text`
    for(size_t i = 0; i < text.size(); i++){
        out << text[i];
        if(text[i] == '\n'){
            srcOffset++;
            // After the newline, currentLine() reflects the freshly-started .inf line.
            // Map it to the corresponding source line. Skip when there's no content after
            // (avoids a spurious entry past the end of the block).
            if(i + 1 < text.size())
                pushSourceMap(currentLine(), srcStart.file, srcStart.line + srcOffset);
        }
    }
}

void i6Emitter::emitEnum(enumDef* enumNode){
    // Non-extern enum values are inlined as integer literals at use sites (see
    // bglParser.cpp qualifyIdentifier / enum-qualified access / namespaced enum value).
    // No I6 constants are emitted, avoiding unused-constant warnings from the I6 compiler.
    // Extern enums produce no output here either — their values map to I6-defined names
    // (e.g. true/false) resolved at use sites.
    (void)enumNode;
}
// Emit a class's `static` methods as free I6 routines named `_bgl_<Class>_<method>`.
// Mirrors the mangling used for static member VARIABLES (`_bgl_<Class>_<name>` globals).
void i6Emitter::emitAllStaticClassRoutines(){
    for(typeDef* td : languageService.objectTypes)
        if(auto* cd = dynamic_cast<classDef*>(td))
            emitStaticClassRoutines(cd);
}

void i6Emitter::emitStaticClassRoutines(classDef* classNode){
    for(typeMember* m : classNode->members){
        auto* fd = dynamic_cast<functionDef*>(m);
        if(fd == nullptr || !fd->isStatic || fd->isEmitter || fd->isExternal) continue;
        string saved = fd->i6name;
        fd->i6name = staticRoutineName(classNode, fd);
        emitFunction(fd);
        fd->i6name = saved;
    }
}

// The element type's `static deinit` routine name, or "" when the type publishes none.
// Mirrors bglParser::operatorRef's static case; an array whose elements own storage needs
// this at scope exit, where there is no receiver to send the emitter deinit() to.
static std::string findStaticDeinit(classDef* cd){
    if(cd == nullptr) return "";
    for(typeMember* m : cd->members){
        auto* f = dynamic_cast<functionDef*>(m);
        if(f != nullptr && f->name == "deinit" && f->isStatic && !f->isEmitter)
            return i6Emitter::staticRoutineName(cd, f);
    }
    for(classDef* b : cd->baseClasses){
        std::string r = findStaticDeinit(b);
        if(!r.empty()) return r;
    }
    return "";
}

// The class's `static` members, emitted as mangled `_bgl_<Class>_<member>` globals ahead of the directive.
void i6Emitter::emitClassStaticGlobals(classDef* classNode){
for(typeMember* m : classNode->members)
    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
        if(vd->isStatic && !vd->staticHasGlobal && dynamic_cast<arrayDeclaration*>(vd) == nullptr){   // a static array is a global array
            if(earlyStaticGlobals.count(format("_bgl_{0}_{1}", classNode->dName(), vd->dName()))) continue;   // declared early
            out << format("global _bgl_{0}_{1}", classNode->dName(), vd->dName());
            if(vd->declaredExpressionValue != nullptr && !staticText(vd->declaredExpressionValue).empty())
                out << format(" = {0}", staticText(vd->declaredExpressionValue));
            out << ";\n";
        }
}

// Standalone I6 buffers for the class's `array<char>` members; records each mangled global name.
void i6Emitter::emitClassByteArrayMembers(classDef* classNode, map<string, string>& externalArrayNames){
for(typeMember* m : classNode->members){
    if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
        if(arr->isStatic) continue;   // its storage is the class's global array
        if(arr->isByteArray && arr->isRefLocal){ externalArrayNames[arr->name] = "0"; continue; }   // bound with :=
        if(arr->isByteArray){
            string mangledName = "_" + classNode->name + "_" + arr->name;
            // Emit as an I6 hybrid buffer (length word at -->0, data bytes at
            // ->WORDSIZE) so the member backing matches the standalone
            // array<char> layout and the byteArray []/size()/for-in accessors.
            // Mirrors the emitGlobal byte-array path.
            if(!arr->stringInitializer.empty()){
                out << format("Array {0} buffer {1};\n", mangledName, arr->stringInitializer);
            } else if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
                out << format("Array {0} buffer", mangledName);
                bool bracketed = list->elements.size() == 1;   // `buffer 113` would read as a size
                if(bracketed) out << " [";
                for(expression* elem : list->elements){
                    string t = staticText(elem);
                    if(!t.empty() && t.front() == '-') out << " (" << t << ")";
                    else                                out << " " << t;
                }
                out << (bracketed ? " ];\n" : ";\n");
            } else {
                out << format("Array {0} buffer {1};\n", mangledName, arr->arraySize);
            }
            externalArrayNames[arr->name] = mangledName;
        }
    }
}
}

// The `class <Name>[(N)]` directive line and its I6 inheritance list (emitter bases filtered out).
void i6Emitter::emitClassHeader(classDef* classNode){
if(classNode->poolSize > 0){
    const string& n = classNode->poolSizeExpr.empty()
                        ? to_string(classNode->poolSize)
                        : classNode->poolSizeExpr;
    out << format("class {0}({1})\n", classNode->i6Name(), n);
} else
    out << format("class {0}\n", classNode->i6Name());

// Filter out emitter base classes — they have no I6 representation, so referencing
// them in the I6 inheritance chain would produce an undefined symbol.
vector<classDef*> emittableBases;
for(classDef* base : classNode->baseClasses)
    if(!base->isEmitterClass) emittableBases.push_back(base);
if(!emittableBases.empty()){
    out << "  class";
    for(classDef* base : emittableBases) out << format(" {0}", base->i6Name());
    out << "\n";
}
}

// Selects the members that reach the `with` list, dropping emitters, statics, attributes and grammar.
void i6Emitter::collectEmittableMembers(classDef* classNode, vector<typeMember*>& emittable){
// A class derived from verb sets meta and priority for its verbs at compile time; they are not properties.
bool isVerbClass = false;
for(classDef* c = classNode; c != nullptr && !isVerbClass; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0])
    isVerbClass = c->name == "verb";
for(typeMember* m : classNode->members){
    if(auto* fd = dynamic_cast<functionDef*>(m)){
        if(fd->isEmitter) continue;
        // `static` methods have no receiver and emit as free routines
        // (emitStaticClassRoutines). Listing them in `with` as well produced a
        // meaningless two-argument property, and two static overloads of one
        // operator collided as the same property name twice in one `with` list.
        if(fd->isStatic) continue;
    }
    if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
        if(vd->isStatic) continue;
        if(vd->type.name == "attributelist") continue; // emitted separately as 'has' line
        if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue; // emitted as I6 Verb directives
        if(isVerbClass && (vd->name == "meta" || vd->name == "priority" || vd->name == "librarypriority")) continue;
    }
    emittable.push_back(m);
}
}

// The class's whole `with` clause: property members, member arrays and method routine bodies.
void i6Emitter::emitClassWithClause(vector<typeMember*>& emittable, map<string, string>& externalArrayNames){
    out << "  with\n";
    for(size_t i = 0; i < emittable.size(); i++){
        typeMember* m = emittable[i];
        string sep = (i + 1 < emittable.size()) ? "," : "";

        if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
            // Property array on a class: emit inline I6 property values so each
            // instance gets its declared storage. Mirrors emitObject's array path.
            out << format("    {0} ", arr->i6name.empty() ? arr->dName() : arr->i6name);
            auto extIt = externalArrayNames.find(arr->name);
            if(arr->isPromoted){
                // Storage lives in a per-instance global, so the class declares the
                // property and nothing else; each instance overrides it with a pointer
                // to its own. Emitting the data here would exceed the I6 property limit,
                // which is the very thing promotion exists to avoid.
            } else if(extIt != externalArrayNames.end()){
                out << extIt->second;
            } else {
                // capacity + the trailing length slot (see the member length-slot rule). A
                // list-initialised member is full, so its length is the seeded count.
                bool trackedMember = languageService.arrayInUse && !arr->isRaw
                                  && !languageService.isAdditiveProperty(
                                         arr->i6name.empty() ? arr->name : arr->i6name);
                int seeded = 0;
                if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue))
                    for(expression* elem : list->elements){ out << staticText(elem) << " "; seeded++; }
                for(int k = seeded; k < arr->arraySize; k++) out << "0 ";
                if(trackedMember) out << seeded << " ";
            }
            out << sep << "\n";
        }
        else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            // `as <i6name>` (§3.11) renames the emitted property. The declaration has to honour it
            // or it names the property one thing while every access site names it another.
            out << format("    {0}", vd->i6name.empty() ? vd->dName() : vd->i6name);
            // Inherited array<T> members reassigned in a subclass body (`name = {...}`)
            // produce a variableDeclaration (not arrayDeclaration) carrying an
            // initializerList in declaredExpressionValue. expression::text() returns
            // empty for initializer lists since their content lives in `elements`,
            // not `tokens` — walk elements explicitly so the inline I6 property
            // values land in the `with` clause.
            if(auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue)){
                out << " ";
                for(expression* elem : list->elements) out << staticText(elem) << " ";
            } else if(vd->declaredExpressionValue != nullptr && !vd->declaredExpressionValue->text().empty())
                out << format(" {0}", propertyValueText(vd->declaredExpressionValue));
            out << sep << "\n";
        }
        else if(auto* fd = dynamic_cast<functionDef*>(m)){
            prepareRecursionSave(fd);
            buildSpillMap(fd);
            out << format("    {0}[", fd->i6name.empty() ? fd->dName() : fd->i6name);
            string sp;
            for(paramDef* p : fd->params) if(!isRequiredStub(fd))
                if(currentSpillAliases.find(p->name) == currentSpillAliases.end())
                    { out << sp << spillName(p->name); sp=" "; }
                else if(currentTryParamRaw.count(p->name))
                    { out << sp << currentTryParamRaw[p->name]; sp=" "; }
            statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
            vector<variableDeclaration*> locals;
            if(body != nullptr){
                set<string> seen;
                collectBodyLocals(body, locals, seen);
                for(variableDeclaration* vd : locals)
                    if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                        { out << sp << spillName(vd->name); sp=" "; }
            }
            for(const string& n : glulxTryLocals(fd)){ out << sp << n; sp=" "; }
            if(currentSpillCount > 0){ out << sp << "_bglFrm"; }
            out << ";\n";
            if(currentSpillCount > 0)
                out << format("        _bglFrm = _bglFrameAlloc({0});\n", currentSpillCount);
            if(!fd->entrySetUp.empty())
                out << "        " << spillWord(fd->entrySetUp) << "\n";
            emitTryParamCopyIns("        ");
            // Per-call copy-in for params that own an instance, on class/object member methods (same
            // shape as top-level functions, just with a deeper indent).
            emitParamCopyIns(fd, "        ");
            emitOwnedLocalSetup(locals, "        ");
            // Allocate method-local arrays — without this a method-local
            // array<T>/rawArray<T>/array<char> is left as an unallocated null slot (a
            // rawArray<int> buffer handed to glk_select would crash). Frees run on every
            // exit path via currentCleanups, mirroring emitFunction.
            emitLocalArrayAllocs(fd, locals, body, "        ");
            currentCleanups = fd->cleanups.empty() ? nullptr : &fd->cleanups;
            armCleanups(body);
            if(body != nullptr)
                for(statement* s : body->statements)
                    emitStatement(s, "        ");
            if(currentCleanups != nullptr && !endsInReturn(body))
                for(auto& [varName, cbody] : *currentCleanups)
                    out << "        " << spillWord(cbody) << "\n";
            currentCleanups = nullptr;
            if(currentSpillCount > 0 && !endsInReturn(body))
                out << format("        _bglFrameFree({0});\n", currentSpillCount);
            out << "    ]" << sep << "\n";
            clearSpillMap();
        }
    }
}

// The class's `has` clause, assembled from its attributeList members.
void i6Emitter::emitClassAttributes(classDef* classNode){
for(typeMember* m : classNode->members)
    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
        if(vd->type.name == "attributelist")
            if(auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue)){
                out << "  has";
                for(expression* elem : list->elements) out << " " << elem->text();
                out << "\n";
            }
}

void i6Emitter::emitClass(classDef* classNode){
    if(classNode->isExternal || classNode->isEmitterClass || classNode->isAlias) return;

    // emit static members as mangled globals before the class definition
    emitClassStaticGlobals(classNode);

    // Emit external global arrays for byte-array (array<char>) member arrays.
    // Mirrors emitObject: byte arrays can't live as inline word-sized property values,
    // so we emit a standalone Array and store a pointer in the property.
    map<string, string> externalArrayNames; // member name → mangled global array name
    emitClassByteArrayMembers(classNode, externalArrayNames);

    // Pooled class: emit `Class Foo(N)` to reserve N statically-allocated instances.
    // poolSize == -1 is the extern marker form, which doesn't reach here (early return above).
    // poolSizeExpr (non-empty) wins over numeric poolSize when present — see typeDef.h.
    emitClassHeader(classNode);

    // collect emittable members (skip emitter-only functions, static members, and attributeList members)
    vector<typeMember*> emittable;
    collectEmittableMembers(classNode, emittable);

    if(!emittable.empty())
        emitClassWithClause(emittable, externalArrayNames);
    // emit attributeList members as I6 'has' line (same as emitObject)
    emitClassAttributes(classNode);
    out << ";\n";
}
void i6Emitter::emitMember(typeMember* member){
    out<< member->name<<" ";
    if (typeid(*member) == typeid(functionDef))  {
        out<<"[";
        out<<"\ntext\n";
        //emitFunctionBody((enumDef*)member);
        out<<"]";
    }

}
void i6Emitter::emitFunction(functionDef* funcNode){
    if(funcNode->isEmitter || funcNode->isExternal || funcNode->isReplacedDead) return;
    // Pre-pass stubs left in globals weren't replaced by a real definition (typical when
    // pre-scan speculatively registered a name that turned out to live as a class/object
    // member instead). Don't emit them — an empty `[name params;]` would collide with the
    // member's emitted property of the same name.
    if(funcNode->isPrePassStub) return;
    // `superposed`: don't emit inline. Re-run this function into a side buffer (the reentrancy
    // flag bypasses this branch and suppresses sourceMap), stash the routine text keyed by its
    // I6 name, and let resolvedOutput() append it only if something actually references it.
    if(funcNode->isSuperposed && !emittingSuperposedBody){
        stringstream captured;
        std::swap(out, captured);          // out ↔ captured: out is now empty, captured holds real output
        emittingSuperposedBody = true;
        // Redirect sourceMap pushes into a per-block buffer. Because `out` is now empty, currentLine()
        // during the re-emit is the routine's own (capture-relative) line — resolvedOutput() re-bases
        // these onto the real splice position later.
        vector<tuple<int,string,int>> blockMap;
        vector<tuple<int,string,int>>* prevTarget = sourceMapTarget;
        sourceMapTarget = &blockMap;
        emitFunction(funcNode);            // re-enter; this time the branch is bypassed
        sourceMapTarget = prevTarget;
        emittingSuperposedBody = false;
        std::swap(out, captured);          // restore out; captured now holds the routine's I6 text
        string rName = funcNode->i6name.empty() ? funcNode->dName() : funcNode->i6name;
        superposedBlocks[rName] = captured.str();
        superposedBlockMaps[rName] = std::move(blockMap);
        return;
    }
    prepareRecursionSave(funcNode);
    buildSpillMap(funcNode);
    // Persist this routine's spill map for the debug bundle (the transient maps are cleared per
    // routine). Key matches writeDebugBundle's lookup: i6name, else canonical name.
    {
        string rn = funcNode->i6name.empty() ? funcNode->name : funcNode->i6name;
        routineSpillAliases[rn] = currentSpillAliases;
        routineSpillCounts[rn]  = currentSpillCount;
    }
    if(!funcNode->src.file.empty())
        pushSourceMap(currentLine(), funcNode->src.file, funcNode->src.line);
    out << format("[{0}", funcNode->i6name.empty() ? funcNode->dName() : funcNode->i6name);
    for(paramDef* param : funcNode->params)
        if(currentSpillAliases.find(param->name) == currentSpillAliases.end())
            out << format(" {0}", spillName(param->name));
        else if(currentTryParamRaw.count(param->name))
            out << " " << currentTryParamRaw[param->name];

    statementBlock* body = dynamic_cast<statementBlock*>(funcNode->body);
    vector<variableDeclaration*> locals;
    if(body != nullptr){
        set<string> seen;
        collectBodyLocals(body, locals, seen);
        for(variableDeclaration* vd : locals){
            if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                out << format(" {0}", spillName(vd->name));
        }
    }
    for(const string& n : glulxTryLocals(funcNode)) out << " " << n;
    if(currentSpillCount > 0) out << " _bglFrm";
    out << ";\n";
    if(currentSpillCount > 0)
        out << format("    _bglFrm = _bglFrameAlloc({0});\n", currentSpillCount);
    if(!funcNode->entrySetUp.empty())
        out << "    " << spillWord(funcNode->entrySetUp) << "\n";
    emitTryParamCopyIns("    ");

    // Per-call copy-in for params that own an instance (top-level functions). Same shape applies
    // to class member methods — emitClass calls the same helper with its own indent. It runs before
    // the locals are reset: a routine that calls itself may pass one of its own locals, whose
    // instance is the one about to be reset.
    emitParamCopyIns(funcNode, "    ");

    emitOwnedLocalSetup(locals, "    ");

    // Local arrays: framePool-backed allocation per function call (recursion-safe).
    // For each word array declared inside the body, allocate a slice from
    // _bglFramePool, lay down the tracked layout (header + zeros + length + magic),
    // and register the matching free in the function's cleanups so it runs on
    // every return path. Both forms are allocated here:
    //   • sized  `array<T> name[N]`     — N comes from arraySize
    //   • list   `array<T> name = {…}`  — N comes from the initializer length
    // For the list form, only the allocation is hoisted here; the element values
    // are written at the declaration statement (emitStatement) so they evaluate
    // in declaration order rather than at function entry.
    emitLocalArrayAllocs(funcNode, locals, body, "    ");

    currentCleanups = funcNode->cleanups.empty() ? nullptr : &funcNode->cleanups;
    armCleanups(body);
    if(body != nullptr)
        for(statement* stmt : body->statements)
            emitStatement(stmt, "    ");
    // emit deinit cleanups at implicit end of function (fall-through path)
    if(currentCleanups != nullptr && !endsInReturn(body))
        for(auto& [varName, body] : *currentCleanups)
            out << "    " << spillWord(body) << "\n";   // a spilled local is freed through its frame slot
    if(currentSpillCount > 0 && !endsInReturn(body))   // a final return freed the frame already
        out << format("    _bglFrameFree({0});\n", currentSpillCount);
    currentCleanups = nullptr;
    clearSpillMap();
    out << "];\n";
}
void i6Emitter::emitLocalArrayAllocs(functionDef* fn, const vector<variableDeclaration*>& locals,
                                     statementBlock* body, const string& indent){
    // framePool-backed allocation per call (recursion-safe) for each local array, registering the
    // matching free in fn->cleanups (run on every return path + fall-through). Sized `array<T>[N]`
    // and list `array<T> = {…}` both allocate here; the list form's element writes happen at the
    // declaration statement. rawArray<T> is a bare flat block; array<char> uses the byte layout.
    // Shared by top-level functions and class/object member methods.
    if(body == nullptr) return;
    // A routine emitted twice (an inline accessor's) keeps one free per array.
    for(variableDeclaration* vd : locals)
        if(dynamic_cast<arrayDeclaration*>(vd) != nullptr)
            fn->cleanups.erase(remove_if(fn->cleanups.begin(), fn->cleanups.end(),
                [&](auto& c){ return c.first == vd->name; }), fn->cleanups.end());
    for(variableDeclaration* vd : locals){
        auto* arr = dynamic_cast<arrayDeclaration*>(vd);
        if(arr == nullptr) continue;
        if(!arr->copySource.empty() || !arr->copyLength.empty()){   // allocated where it's declared, sized from its value
            fn->cleanups.push_back({arr->name, format("_bglFrameFree({0});", arr->copyHeld)});
            continue;
        }
        if(arr->isByteArray){
            if(!arr->localInitSource.empty()){
                string name = spillName(arr->name), src = arr->localInitSource;
                out << format("{0}{1} = _bglByteArrayLocalAlloc({2}-->0); _bglByteArrayLocalCopy({1}, {2});\n",
                              indent, name, src);
                fn->cleanups.push_back({arr->name, format("_bglByteArrayLocalFree({0}-->0);", src)});
                continue;
            }
            if(arr->arraySize <= 0) continue;
            string name = spillName(arr->name);
            out << format("{0}{1} = _bglByteArrayLocalAlloc({2});\n", indent, name, arr->arraySize);
            fn->cleanups.push_back({arr->name, format("_bglByteArrayLocalFree({0});", arr->arraySize)});
            continue;
        }
        auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue);
        int count = arr->arraySize > 0 ? arr->arraySize
                  : (list ? (int)list->elements.size() : 0);
        if(count <= 0) continue;   // pointer-alias decl (e.g. `= _bglLinqWrite()`) — emitStatement owns it
        string name = spillName(arr->name);
        // A nested list's inner arrays are per call too, held in the outer array's slots.
        std::function<void(const string&, initializerList*)> allocInner = [&](const string& ptr, initializerList* outer){
            if(outer == nullptr || arr->isRaw) return;
            for(size_t i = 0; i < outer->elements.size(); i++)
                if(auto* inner = dynamic_cast<initializerList*>(outer->elements[i])){
                    int k = max(1, (int)inner->elements.size());   // an empty `{}` row still has storage, length 0
                    string slot = format("({0}-->{1})", ptr, i + 1);
                    bool tracked = languageService.arrayInUse;
                    out << format("{0}{1}-->{2} = {3}({4});\n", indent, ptr, i + 1,
                                  tracked ? "_bglArrayLocalAlloc" : "_bglArrayLocalAllocPlain", k);
                    fn->cleanups.push_back({arr->name, format("_bglFrameFree({0});", k + (tracked ? 3 : 1))});
                    allocInner(slot, inner);
                }
        };
        if(languageService.arrayInUse && !arr->isRaw){
            out << format("{0}{1} = _bglArrayLocalAlloc({2});\n", indent, name, count);
            std::string dtor = findStaticDeinit(languageService.findClass(arr->elementType));
            if(dtor.empty()) dtor = valueElementRoutine(arr->elementType, "_bglValFree_");
            if(!dtor.empty())
                fn->cleanups.push_back({arr->name, format("_bglArray.freeAll({0}, 0, {1});", name, dtor)});
            fn->cleanups.push_back({arr->name, format("_bglFrameFree({0});", count + 3)});
            allocInner(name, list);
        } else if(arr->isRaw){
            out << format("{0}{1} = _bglRawArrayLocalAlloc({2});\n", indent, name, count);
            fn->cleanups.push_back({arr->name, format("_bglFrameFree({0});", count)});
        } else {
            out << format("{0}{1} = _bglArrayLocalAllocPlain({2});\n", indent, name, count);
            fn->cleanups.push_back({arr->name, format("_bglFrameFree({0});", count + 1)});
            allocInner(name, list);
        }
    }
}
// Local arrayDeclaration with a non-list initializer: pointer-aliasing decl
// like `array<var> dst = _bglLinqWrite();`. The local slot already exists in
// the routine header (collected via collectBodyLocals); we just emit the
// assignment of the RHS expression. No allocation — caller owns the storage.
// A local `array<T>` declaration: alias-assign, seed a list initializer, or nothing.
void i6Emitter::emitLocalArrayDeclaration(arrayDeclaration* arrLoc, const string& indent){
    if(!arrLoc->copyLength.empty()){   // from a member array: storage sized from its length; the copy follows
        string name = spillName(arrLoc->name), held = spillName(arrLoc->copyHeld);
        out << format("{0}{1} = _bglArrayLocalCopyAlloc({2}, 0, {1}); {3} = {3} + _bglFrameTaken;\n",
                      indent, name, spillWord(arrLoc->copyLength), held);
        return;
    }
    if(!arrLoc->copySource.empty()){   // the copy itself is the assignment that follows
        string name = spillName(arrLoc->name), held = spillName(arrLoc->copyHeld);
        string src = spillName(arrLoc->copySource);
        out << format("{0}{1} = _bglArrayLocalCopyAlloc(_bglArray.length({2}, 0), {2}, {1}); {3} = {3} + _bglFrameTaken; {2} = _bglFrameSrc;\n",
                      indent, name, src, held);
        return;
    }
    if(arrLoc->arraySize == 0
       && arrLoc->declaredExpressionValue != nullptr
       && dynamic_cast<initializerList*>(arrLoc->declaredExpressionValue) == nullptr
       && arrLoc->stringInitializer.empty()){
        out << format("{0}{1} = {2};\n", indent, spillName(arrLoc->name),
                      exprText(arrLoc->declaredExpressionValue));
        return;
    }
    // List-initialized local word array (`array<T> name = {a, b, c}`). The
    // framePool slot was allocated in emitFunction (header/zeros/length=0/magic
    // already laid down). Here we fill the data slots and set the tracked
    // length. operator[] maps element i to raw slot i+1; the length slot lives
    // at raw slot N+1 (just past the data). Byte arrays use the global-emission
    // path instead, so they're excluded.
    if(!arrLoc->isByteArray){
        if(auto* list = dynamic_cast<initializerList*>(arrLoc->declaredExpressionValue)){
            string name = spillName(arrLoc->name);
            int len = (int)list->elements.size();
            // The length slot sits just past the *capacity*, which a declared `[N]`
            // fixes independently of the seed length. _bglArrayLocalAlloc already
            // zeroed the slice, so unseeded slots need no explicit write.
            int cap = arrLoc->arraySize > 0 ? arrLoc->arraySize : len;
            bool tracked = languageService.arrayInUse && !arrLoc->isRaw;   // tracked layout has a length slot at cap+1
            // A nested list fills the per-call inner array emitLocalArrayAllocs put in its slot.
            std::function<void(const string&, initializerList*, int)> fill = [&](const string& ptr, initializerList* l, int c){
                int i = 0;
                for(expression* elem : l->elements){
                    auto* inner = arrLoc->isRaw ? nullptr : dynamic_cast<initializerList*>(elem);
                    if(inner != nullptr)
                        fill(format("({0}-->{1})", ptr, i + 1), inner, max(1, (int)inner->elements.size()));
                    else if(string copy = valueElementRoutine(arrLoc->elementType, "_bglValAssign_"); !copy.empty())
                        out << format("{0}{1}-->{2} = {3}(0, {4});\n", indent, ptr, i + 1, copy, exprText(elem));
                    else   // a rawArray has no length header: its elements start at slot 0
                        out << format("{0}{1}-->{2} = {3};\n", indent, ptr, i + (arrLoc->isRaw ? 0 : 1), exprText(elem));
                    i++;
                }
                if(tracked) out << format("{0}{1}-->{2} = {3};\n", indent, ptr, c + 1, (int)l->elements.size());
            };
            fill(name, list, cap);
            return;
        }
    }
    // A sized word array declared in a loop body starts each pass empty, as a block-scoped local does:
    // its elements and, when tracked, the length slot past them.
    if(arrLoc->inLoop && !arrLoc->isByteArray && arrLoc->arraySize > 0 && arrLoc->declaredExpressionValue == nullptr
       && arrLoc->copySource.empty() && arrLoc->copyLength.empty()){
        string name = spillName(arrLoc->name);
        int last = arrLoc->arraySize + ((languageService.arrayInUse && !arrLoc->isRaw) ? 1 : 0);
        for(int k = arrLoc->isRaw ? 0 : 1; k <= last; k++)
            out << format("{0}{1}-->{2} = 0;\n", indent, name, k);
    }
    return;
}
// A local variable declaration's initializer assignment (plain, emitter-bodied, or interpolated).
void i6Emitter::emitLocalDeclaration(variableDeclaration* var, const string& indent){
    // For class-typed locals with synthesized backing, `$self`/`$val`/`$target`
    // and the plain-assign LHS must resolve to the backing object name (i6name),
    // not the bare local int slot (name). Otherwise emitter operator= bodies run
    // against `nothing.field`, the synthesized `$target._opeq(...)` dispatch
    // misses the backing entirely, and the bare-assign fallback writes to the
    // unused local slot while the backing stays at its zero-init state.
    const string selfText = spillName(var->name);
    // emit initializer assignment if present
    if(var->declaredExpressionValue != nullptr && (!var->declaredExpressionValue->text().empty() || !var->interpSegments.empty())){
        if(!var->interpSegments.empty() && !var->initEmitterBody.empty()){
            // Interpolated string literal: split emitter body at parameter, splice print-block
            string b = var->initEmitterBody;
            b = replaceWord(b, "$self", selfText);
            b = replaceWord(b, "$val",  selfText);
            if(!currentSpillAliases.empty()) b = groupFrameSlots(b);   // `(_bglFrm-->2).capture()`
            emitInterpolatedEmitterBody(b, var->initEmitterParam, var->interpSegments, indent);
        } else if(!var->initEmitterBody.empty()){
            string b = var->initEmitterBody;
            b = replaceOperand(b, "$" + var->initEmitterParam, exprText(var->declaredExpressionValue));
            b = replaceWord(b, "$self",              selfText);
            b = replaceWord(b, "$val",               selfText);
            b = replaceWord(b, "$target",            selfText);
            if(!currentSpillAliases.empty()) b = groupFrameSlots(b);   // `(_bglFrm-->1)._opeq(…)`
            size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
            size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1);
            out << format("{0}{1};\n", indent, b);
        } else {
            string rhs = exprText(var->declaredExpressionValue);
            if(rhs.find("$target") != string::npos){
                rhs = replaceWord(rhs, "$target", selfText);
                if(!currentSpillAliases.empty()) rhs = groupFrameSlots(rhs);
                out << format("{0}{1};\n", indent, rhs);
            } else {
                out << format("{0}{1} = {2};\n", indent, selfText, rhs);
            }
        }
    }
}
// An assignment statement: emitter-bodied, interpolated, `$target`-rewritten, or plain `lhs = rhs`.
void i6Emitter::emitAssignment(assignmentStatement* assign, const string& indent){
    if(!assign->interpSegments.empty() && !assign->emitterBody.empty()){
        // Interpolated string literal: split emitter body at parameter, splice print-block
        string b = assign->emitterBody;
        b=replaceWord(b,"$self", assign->emitterSelf.empty() ? spillName(assign->variableLeft) : spillName(assign->emitterSelf));
        b=replaceWord(b,"$val",  spillName(assign->variableLeft));
        if(!currentSpillAliases.empty()) b = groupFrameSlots(b);
        emitInterpolatedEmitterBody(b, assign->emitterParam, assign->interpSegments, indent);
    } else if(!assign->emitterBody.empty()){
        string b = assign->emitterBody;
        size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
        size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
        // Substitute params before $self to avoid double-substitution when param name matches the LHS variable
        b=replaceOperand(b,"$" + assign->emitterParam, assign->assignedExpression != nullptr ? exprText(assign->assignedExpression) : "");
        b=replaceWord(b,"$self", assign->emitterSelf.empty() ? spillName(assign->variableLeft) : spillName(assign->emitterSelf));
        b=replaceWord(b,"$prop", assign->emitterProp.empty() ? string("0") : assign->emitterProp);   // 0: not a member
        b=replaceWord(b,"$val",  spillName(assign->variableLeft));
        b=replaceWord(b,"$target", spillName(assign->variableLeft));
        if(!currentSpillAliases.empty()) b = groupFrameSlots(b);
        while(!b.empty() && b.back()==';') b.pop_back();
        out << indent << b << ";\n";
    } else {
        string rhs = assign->assignedExpression != nullptr ? exprText(assign->assignedExpression) : "";
        // $target in expression: substitute LHS and emit as statement (no "LHS =" prefix)
        if(rhs.find("$target") != string::npos){
            rhs = replaceWord(rhs, "$target", spillName(assign->variableLeft));
            if(!currentSpillAliases.empty()) rhs = groupFrameSlots(rhs);
            out << indent << rhs << ";\n";
        } else {
            out << format("{0}{1} = {2};\n", indent, spillName(assign->variableLeft), rhs);
        }
    }
}
// A `return`: deinit cleanups and frame-free first, then the matching I6 return form.
void i6Emitter::emitReturn(returnStatement* ret, const string& indent){
    // Inside a lifted try body the function's return is the caller's to make: hand the value back
    // with code 1. Cleanups and the frame free run there.
    if(inLiftedTry){
        string v = ret->returnExpression == "rtrue" ? "1"
                 : ret->returnExpression == "rfalse" || ret->returnExpression.empty() ? "0"
                 : spillWord(ret->returnExpression);
        out << indent << "_bgl_tryret = " << v << ";\n";
        out << indent << "return 1;\n";
        return;
    }
    const string& rx = ret->returnExpression;
    // Only a whole literal is constant: `1 + f(x)` starts with a digit but runs a call.
    static const std::regex literalValue(R"re(^(-?\d+|\$\$?[0-9A-Fa-f]+|\$[+-][0-9.eE+-]+|"[^"]*"|'[^']*')$)re");
    bool constantValue = rx.empty() || rx == "rtrue" || rx == "rfalse" || rx == "true" || rx == "false"
        || std::regex_match(rx, literalValue);
    // Leaving every active try: the cookie goes back to what it was outside the outermost — after the
    // value is computed, since computing it may throw to this try's catch.
    if(!glulxTryStack.empty()){
        if(!constantValue && rx != "_bgl_tryret"){
            out << indent << "_bgl_tryret = " << spillWord(rx) << ";\n";
            returnStatement held = *ret;
            held.returnExpression = "_bgl_tryret";
            emitReturn(&held, indent);
            return;
        }
        out << indent << "_bgl_catch_cookie = " << glulxTryStack.front().first << ";\n";
    }
    // A value the cleanups could disturb is computed into the routine's return slot first.
    bool holdValue = !constantValue && (currentCleanups != nullptr || currentSpillCount > 0)
                  && (!currentReturnSlot.empty() || currentSpillCount > 0);
    // The routine's own slot, unless it lives in the frame being freed: then a global holds it.
    string hold = currentReturnSlot.empty() ? "" : spillName(currentReturnSlot);
    if(hold.empty() || hold.find("-->") != string::npos) hold = "_bglRetHold";
    // A global hold could be overwritten by a routine a cleanup calls: park it on the stack meanwhile.
    bool parked = holdValue && hold == "_bglRetHold" && currentCleanups != nullptr;
    bool glulx = currentTarget == "glulx";
    if(holdValue)
        out << indent << hold << " = " << spillWord(rx) << ";\n";
    if(parked) out << indent << (glulx ? "@copy _bglRetHold sp;\n" : "@push _bglRetHold;\n");
    // emit deinit cleanups before every return
    if(currentCleanups != nullptr)
        for(auto& [varName, body] : *currentCleanups)
            if(!undeclaredTopLocals.count(varName)) out << indent << spillWord(body) << "\n";
    if(currentSpillCount > 0)
        out << format("{0}_bglFrameFree({1});\n", indent, currentSpillCount);
    if(parked) out << indent << (glulx ? "@copy sp _bglRetHold;\n" : "@pull _bglRetHold;\n");
    if(holdValue){
        out << format("{0}return {1};\n", indent, hold);
        return;
    }
    if(ret->returnExpression == "rtrue" || ret->returnExpression == "rfalse")
        out << format("{0}{1};\n", indent, ret->returnExpression);
    else if(ret->returnExpression != "")
        out << format("{0}return {1};\n", indent, spillWord(ret->returnExpression));
    else
        out << indent << "return;\n";
}
// A call in statement position: emitter-body inlining, or a direct call with Z-target arg spilling.
void i6Emitter::emitFunctionCallStatement(functionCallStatement* call, const string& indent){
    if(!call->emitterBody.empty()){
        string b = call->emitterBody;
        // Safety net: an unsubstituted $elem* would reach I6 as a hex literal ($e...)
        // and fail far from the cause. Degrade to word comparison instead.
        // Safety net: an unsubstituted $opref(...) would reach I6 as a hex literal.
        while(true){ size_t a = b.find("$opref("); if(a==string::npos) break;
                     size_t c = b.find(')', a); if(c==string::npos) break;
                     b.replace(a, c-a+1, "0"); }
        size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
        size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
        // Check if any argument is an interpolated string literal
        int interpArgIdx = -1;
        for(size_t i=0; i<call->interpSegmentsPerArg.size(); i++)
            if(!call->interpSegmentsPerArg[i].empty()){ interpArgIdx = (int)i; break; }
        // Substitute all non-interpolated parameters normally
        for(size_t i=0; i<call->emitterParams.size() && i<call->args.size(); i++)
            if((int)i != interpArgIdx)
                b=replaceOperand(b, "$" + call->emitterParams[i], exprText(call->args[i]));
        // $target substitution for a discarded (statement-position) value-returning opcode:
        // the result is thrown away, so store it to `sp` (the stack pointer, I6 variable 0) —
        // a built-in destination that needs no declaration. Normally resolved at parse time
        // (parseStatement); this is the defensive fallback for any body that still carries
        // $target here. Guarded on presence so void emitter statements are untouched.
        if(b.find("$target") != string::npos)
            b = replaceWord(b, "$target", "sp");
        // `give obj (expr)` reads in I6 as a call of obj; `give obj 0+(expr)` is the attribute.
        {
            static const regex giveParen(R"((\bgive\s+[^\s~]+\s+)\()");
            if(b.find("give") != string::npos) b = regex_replace(b, giveParen, "$010+(");   // $01: group 1, then "0+("
        }
        if(interpArgIdx >= 0){
            // Splice interpolated print-block at the interpolated parameter's position
            emitInterpolatedEmitterBody(spillWord(b), call->emitterParams[interpArgIdx],
                call->interpSegmentsPerArg[interpArgIdx], indent);
        } else {
            while(!b.empty() && b.back()==';') b.pop_back();
            string heldSuffix = call->heldSuffix;
            for(size_t k = 0; k < call->heldMarks.size(); k++)
                heldSuffix = replaceEvery(heldSuffix, heldMarkPlaceholder(k), call->heldMarks[k]);
            out << indent << call->heldPrefix << spillWord(b) << heldSuffix << ";\n";   // locals the body names directly (a member-array owner)
        }
    } else {
        // On Z-machine, args beyond the 5th are passed via _bglXPn globals — but ONLY for
        // Beguile-generated routines, which read those globals as their overflow params. An
        // extern (I6-defined) routine takes all its args directly (I6 passes up to 7 on Z5+ via
        // call_vs2), so it must not spill: doing so would drop args 6+ and reference _bglXPn
        // globals that were never declared (the frame-pool scan skips extern functions).
        bool calleeIsExtern = false;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->isExternal && fd->name == call->functionName){ calleeIsExtern = true; break; }
        size_t maxDirectArgs = (!calleeIsExtern && isZTarget(currentTarget) && call->args.size() > 5) ? 5 : call->args.size();
        // Held temporaries release back to a mark opened before any argument is evaluated, and the
        // spilled arguments below are evaluated first: so the mark is opened first, as a statement.
        string heldSuffix = call->heldSuffix;
        for(size_t k = 0; k < call->heldMarks.size(); k++){
            string mark = call->heldMarks[k];
            if(maxDirectArgs < call->args.size()){
                out << indent << mark << ";\n";
                mark = "0";
            }
            heldSuffix = replaceEvery(heldSuffix, heldMarkPlaceholder(k), mark);
        }
        for(size_t i = maxDirectArgs; i < call->args.size(); i++)
            out << format("{0}_bglXP{1} = {2};\n", indent, i - 5, exprText(call->args[i]));
        // Prefer displayName (original case) over functionName (lowercased) — same
        // convention as dName() for declared symbols. displayName is empty for
        // resolved Beguile calls, so they continue to emit lowercase as before.
        const string& emitName = call->displayName.empty() ? call->functionName : call->displayName;
        string callee = spillWord(emitName);
        if(callee.rfind("_bglFrm-->", 0) == 0) callee = "(" + callee + ")";   // a call binds tighter than -->
        out << indent << call->heldPrefix << callee << token::parenOpen;
        for(size_t i = 0; i < maxDirectArgs; i++){
            if(i>0) out << ", ";
            out << exprText(call->args[i]);
        }
        out << token::parenClose << heldSuffix << ";\n";
    }
}
// An `if` / `else` statement and its blocks.
void i6Emitter::emitIfStatement(ifStatement* ifNode, const string& indent){
    out << indent << "if (" << (ifNode->condition != nullptr ? exprText(ifNode->condition) : "") << ") {\n";
    if(ifNode->thenBlock != nullptr)
        for(statement* s : ifNode->thenBlock->statements)
            emitStatement(s, indent + "    ");
    out << indent << "}\n";
    if(ifNode->elseBlock != nullptr){
        out << indent << "else {\n";
        for(statement* s : ifNode->elseBlock->statements)
            emitStatement(s, indent + "    ");
        out << indent << "}\n";
    }
}
// A `do ... while`/`until` loop, emitted as I6 `do { } until (...)`.
void i6Emitter::emitDoStatement(doStatement* doNode, const string& indent){
    string cond = doNode->condition != nullptr ? exprText(doNode->condition) : "";
    out << indent << "do {\n";
    if(doNode->body != nullptr)
        for(statement* s : doNode->body->statements)
            emitStatement(s, indent + "    ");
    // do-while negates the condition: loop while expr → until ~~(expr)
    if(doNode->isWhile)
        out << indent << "} until (~~(" << cond << "));\n";
    else
        out << indent << "} until (" << cond << ");\n";
}
// A `while` loop.
void i6Emitter::emitWhileStatement(whileStatement* whileNode, const string& indent){
    out << indent << "while (" << (whileNode->condition != nullptr ? exprText(whileNode->condition) : "") << ") {\n";
    if(whileNode->body != nullptr)
        for(statement* s : whileNode->body->statements)
            emitStatement(s, indent + "    ");
    out << indent << "}\n";
}
// A C-style `for` loop.
void i6Emitter::emitForStatement(forStatement* forNode, const string& indent){
    out << indent << "for (" << spillWord(forNode->initText) << " : ";
    out << (forNode->condition != nullptr ? exprText(forNode->condition) : "") << " : ";
    out << spillWord(forNode->incrementText) << ") {\n";
    if(forNode->body != nullptr)
        for(statement* s : forNode->body->statements)
            emitStatement(s, indent + "    ");
    out << indent << "}\n";
}
// A `for(x in c)` loop over a string, world-tree children, a member array, or a word/byte array.
void i6Emitter::emitForInStatement(forInStatement* fi, const string& indent){
    // String container: iterated through its class's `getLength()` and `operator[]` emitters, expanded
    // by the parser with markers for the container and the counter.
    if(fi->isStringForIn){
        string c   = spillName(fi->counterVar);
        string el  = spillName(fi->elementVar);
        string str = spillName(fi->arrayVar);
        auto fill = [&](string t){
            for(size_t at = t.find("@@CONTAINER@@"); at != string::npos; at = t.find("@@CONTAINER@@", at + str.size())) t.replace(at, 13, str);
            for(size_t at = t.find("@@COUNTER@@"); at != string::npos; at = t.find("@@COUNTER@@", at + c.size())) t.replace(at, 11, c);
            return t;
        };
        out << indent << c << " = 0;\n";
        out << indent << "for (: " << c << " < " << fill(fi->stringLengthText) << " : " << c << "++) {\n";
        out << indent << "    " << el << " = " << fill(fi->stringCharText) << ";\n";
        if(fi->body != nullptr)
            for(statement* s : fi->body->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
        return;
    }
    // World-tree child collection: walk the container's I6 children directly. The next sibling is
    // read before the body runs, so the body may move the current object out (objectloop can't).
    if(fi->isChildrenForIn){
        string el   = spillName(fi->elementVar);
        string next = spillName(fi->counterVar);
        out << indent << "for (" << el << " = child(" << fi->arrayVar << ") : " << el << " : " << el << " = " << next << ") {\n";
        out << indent << "    " << next << " = sibling(" << el << ");\n";
        if(fi->body != nullptr)
            for(statement* s : fi->body->statements)
                emitStatement(s, indent + "    ");
        out << indent << "}\n";
        return;
    }
    // Inline initializer list: emit push/make templates before the loop
    if(!fi->inlineElements.empty()){
        for(auto* elem : fi->inlineElements)
            applyTemplate("forInList.push", {{"element", exprText(elem)}}, indent);
        applyTemplate("forInList.make",
            {{"target", spillName(fi->arrayVar)}, {"count", to_string(fi->inlineElements.size())}}, indent);
    }
    string openTemplate  = fi->isByteArray ? "forIn.openByte"  : "forIn.open";
    string closeTemplate = fi->isByteArray ? "forIn.closeByte" : "forIn.close";
    string arrayVarS = spillName(fi->arrayVar);
    // Member (property) WORD array: element n at obj.&prop-->n, with the length the parser
    // worked out for its layout (a tracked member keeps it in its last word). Globals/locals keep
    // the count-prefixed/tracked path.
    bool isMemberArr = !fi->isByteArray && !fi->viaPointer && arrayVarS.find('.') != string::npos;
    string arrayArg = arrayVarS;
    string lengthExpr;
    if(!fi->memberBase.empty()){
        openTemplate = "forIn.openMember";
        closeTemplate = "forIn.closeMember";
        arrayArg = fi->memberBase;
        lengthExpr = fi->memberLength;
    } else if(isMemberArr){
        size_t d = arrayVarS.rfind('.');
        string owner = arrayVarS.substr(0, d), prop = arrayVarS.substr(d + 1);
        openTemplate = "forIn.openMember";
        closeTemplate = "forIn.closeMember";   // its own close (byte-identical to forIn.close for now,
                                               // so a variant can grow a distinct tail without touching the others)
        prop = parser.propertyI6Name(prop);
        arrayArg = owner + ".&" + prop;
        lengthExpr = "(" + owner + ".#" + prop + ")/WORDSIZE";
    } else {
        // Length probe: when <array> is included, use the tracked-aware _bglArray.length
        // dispatch. Without it, fall back to the raw slot count ($array-->0) — matches the
        // untracked global-array layout emitted at the same arrayInUse=false gate above.
        lengthExpr = languageService.arrayInUse
            ? "_bglArray.length(" + arrayVarS + ", 0)"   // dual-form: global/local → 0 prop sentinel
            : arrayVarS + "-->0";
    }
    applyTemplate(openTemplate,
        {{"counter", spillName(fi->counterVar)}, {"array", arrayArg},
         {"element", spillName(fi->elementVar)}, {"lengthExpr", lengthExpr}},
        indent);
    if(!fi->elementConversion.empty()){
        string el = spillName(fi->elementVar);
        out << indent << "    " << el << " = " << replaceWord(fi->elementConversion, "@@ELEM@@", el) << ";\n";
    }
    if(fi->body != nullptr)
        for(statement* s : fi->body->statements)
            emitStatement(s, indent + "    ");
    applyTemplate(closeTemplate, {}, indent);
}
// A `switch`: an if/else chain when any case needs guards, otherwise a native I6 `switch`.
void i6Emitter::emitSwitchStatement(switchStatement* sw, const string& indent){
    if(sw->needsIfChain){
        // Emit as if/else if chain (required when any case uses comparison guards)
        string condText = sw->condition != nullptr ? exprText(sw->condition) : "0";
        out << indent << "_bgl_sw = " << condText << ";\n";
        bool first = true;
        for(switchCase* sc : sw->cases){
            if(sc->entries.empty()){
                // default case → else
                out << indent << "else {\n";
            } else {
                out << indent << (first ? "if (" : "else if (");
                bool firstCond = true;
                for(auto& e : sc->entries){
                    if(!firstCond) out << " || ";
                    if(!e.guardCondition.empty()){
                        out << "(" << e.guardCondition << ")";
                    } else if(e.rangeLow != nullptr){
                        out << "(_bgl_sw >= " << e.rangeLow->text() << " && _bgl_sw <= " << e.rangeHigh->text() << ")";
                    } else if(e.value != nullptr){
                        // Check for operator switch() emitter matching this value's type
                        string valType = e.value->resolvedType;
                        string valText = (valType == "verb") ? ("##" + actionNameOf(e.value->text())) : e.value->text();
                        auto it = sw->switchEmitters.find(valType);
                        if(it == sw->switchEmitters.end() && !valType.empty())
                            it = sw->switchEmitters.find("var"); // fallback to var
                        if(it != sw->switchEmitters.end()){
                            // Inline the operator switch() emitter
                            string b = it->second;
                            b = replaceWord(b, "$self", "_bgl_sw");
                            b = replaceWord(b, "$val",  "_bgl_sw");
                            // Find the parameter name — stored as part of the emitter body placeholder
                            // We need to substitute the first non-$self word. Use a generic approach:
                            // The emitter body has one parameter; replace all non-$self param names.
                            // Since we don't store param names here, use a simpler approach:
                            // replace any word that isn't a known keyword or $self with the value.
                            // Actually, we can just do a second replaceWord pass for common param names.
                            // Better: store param name in switchEmitters. For now, use convention.
                            // Let's store as "paramName\0body" and split here.
                            // Actually simplest: just replace the first identifier-like word that isn't $self.
                            // The emitter body after $self replacement looks like: _bgl_sw.equals(v)
                            // We need to replace 'v' with the value. Let's find it by checking
                            // what remains unresolved.
                            // Simplest correct approach: store param name with the body.
                            // switchEmitters stores "paramName:body"
                            size_t colonPos = b.find('\t');
                            if(colonPos != string::npos){
                                string paramName = b.substr(0, colonPos);
                                string body = b.substr(colonPos + 1);
                                body = replaceOperand(body, "$" + paramName, valText);
                                body = replaceWord(body, "$self", "_bgl_sw");
                                body = replaceWord(body, "$val",  "_bgl_sw");
                                size_t s = body.find_first_not_of(" \t\n\r"); if(s!=string::npos) body=body.substr(s);
                                size_t e2 = body.find_last_not_of(" \t\n\r;"); if(e2!=string::npos) body=body.substr(0,e2+1);
                                out << "(" << body << ")";
                            } else {
                                out << "_bgl_sw == " << valText;
                            }
                        } else {
                            out << "_bgl_sw == " << valText;
                        }
                    }
                    firstCond = false;
                }
                out << ") {\n";
                first = false;
            }
            if(sc->body != nullptr)
                for(statement* s : sc->body->statements)
                    emitStatement(s, indent + "    ");
            out << indent << "}\n";
        }
        // Where a case's `break` jumps to. An if-chain has no switch for I6's own break to leave.
        if(!sw->breakLabel.empty()) out << indent << "." << sw->breakLabel << ";\n";
    } else {
        // Standard I6 switch — all entries are values or ranges (no comparison guards)
        out << indent << "switch (" << (sw->condition != nullptr ? exprText(sw->condition) : "") << ") {\n";
        for(switchCase* sc : sw->cases){
            if(!sc->entries.empty()){
                out << indent << "    ";
                for(size_t i = 0; i < sc->entries.size(); i++){
                    if(i > 0) out << ", ";
                    auto& e = sc->entries[i];
                    if(e.rangeLow != nullptr){
                        out << e.rangeLow->text() << " to " << e.rangeHigh->text();
                    } else if(e.value != nullptr){
                        if(e.value->resolvedType == "verb")
                            out << "##" << actionNameOf(e.value->text());
                        else
                            out << e.value->text();
                    }
                }
                out << ":\n";
            } else {
                out << indent << "    default:\n";
            }
            if(sc->body != nullptr)
                for(statement* s : sc->body->statements)
                    emitStatement(s, indent + "        ");
        }
        out << indent << "}\n";
    }
}
// A `try`/`catch`, built on @catch/@throw cookies (separate Glulx and Z-machine shapes).
void i6Emitter::emitTryCatch(tryCatchStatement* tc, const string& indent){
    string id = to_string(tc->id);
    string cvName = "_bgl_cv" + id;
    string cvSave = "_bgl_cvs" + id;
    string tryLabel = "_bgl_try" + id;
    string endLabel = "_bgl_tryend" + id;
    if(currentTarget == "glulx"){
        string frameTop = "_bgl_ft" + id;
        // Glulx: @catch cookie ?label — branches to label on first exec, falls through on throw
        out << indent << "@catch " << cvName << " ?" << tryLabel << ";\n";
        // Catch body (reached via @throw)
        out << indent << "    _bgl_catch_cookie = " << cvSave << ";\n";
        // A throw from another object's method left self and sender there; a send's return reads both.
        out << indent << "    self = _bgl_sf" << id << "; sender = _bgl_sn" << id << ";\n";
        if(frameAllocEmitted) out << indent << "    _bglFrameTop = " << frameTop << ";\n";
        // A throw out of a <linq> predicate skipped the chain's exit, which lowers its depth.
        if(languageService.linqInUse) out << indent << "    _bglLinqActiveDepth = _bgl_ld" << id << ";\n";
        out << indent << "    " << spillName(tc->catchVarName) << " = " << cvName << ";\n";
        if(tc->catchBody != nullptr)
            for(statement* s : tc->catchBody->statements){
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == tc->catchVarName) continue;
                emitStatement(s, indent + "    ");
            }
        // A body that ends in `return` has already left, so what follows it can't run.
        bool catchReturns = endsInReturn(tc->catchBody), tryReturns = endsInReturn(tc->tryBody);
        if(!catchReturns) out << indent << "    jump " << endLabel << ";\n";
        // Try body (normal execution — branched here by @catch)
        out << indent << "." << tryLabel << ";\n";
        out << indent << "    " << cvSave << " = _bgl_catch_cookie;\n";
        out << indent << "    _bgl_sf" << id << " = self; _bgl_sn" << id << " = sender;\n";
        if(frameAllocEmitted) out << indent << "    " << frameTop << " = _bglFrameTop;\n";
        if(languageService.linqInUse) out << indent << "    _bgl_ld" << id << " = _bglLinqActiveDepth;\n";
        out << indent << "    _bgl_catch_cookie = " << cvName << ";\n";
        glulxTryStack.push_back({cvSave, liftedLoopDepth});
        if(tc->tryBody != nullptr)
            for(statement* s : tc->tryBody->statements)
                emitStatement(s, indent + "    ");
        glulxTryStack.pop_back();
        if(!tryReturns) out << indent << "    _bgl_catch_cookie = " << cvSave << ";\n";
        if(!(catchReturns && tryReturns)){
            out << indent << "." << endLabel << ";\n";
            out << indent << "    _bgl_catch_cookie = _bgl_catch_cookie;\n";  // no-op to satisfy I6 label requirement
        }
    } else {
        // Z-machine: the try body runs in its own routine, which catches; a throw returns from it.
        string routine = "_bgl_try" + id;
        auto [cookieSlot, topSlot] = currentTrySlots[tc->id];
        TryExits exits;
        scanTryExits(tc->tryBody, 0, exits);
        {
            stringstream captured;
            std::swap(out, captured);
            vector<tuple<int,string,int>> blockMap;
            vector<tuple<int,string,int>>* prevTarget = sourceMapTarget;
            sourceMapTarget = &blockMap;
            bool prevLifted = inLiftedTry; int prevDepth = liftedLoopDepth;
            inLiftedTry = true; liftedLoopDepth = 0;
            out << "[ " << routine << " _bglFrm _bgl_cv;\n";
            out << "    @catch -> _bgl_cv;\n";
            out << "    _bgl_catch_cookie = _bgl_cv;\n";
            if(tc->tryBody != nullptr)
                for(statement* s : tc->tryBody->statements)
                    emitStatement(s, "    ");
            // A body that ends in `return` has already left; a second return would be unreachable.
            bool bodyReturns = tc->tryBody != nullptr && !tc->tryBody->statements.empty()
                            && dynamic_cast<returnStatement*>(tc->tryBody->statements.back()) != nullptr;
            out << (bodyReturns ? "];\n" : "    return 0;\n];\n");
            inLiftedTry = prevLifted; liftedLoopDepth = prevDepth;
            sourceMapTarget = prevTarget;
            std::swap(out, captured);
            superposedBlocks[routine] = captured.str();
            superposedBlockMaps[routine] = std::move(blockMap);
            routineSpillAliases[routine] = currentSpillAliases;
            routineSpillCounts[routine]  = currentSpillCount;
        }
        string cookie = format("_bglFrm-->{0}", cookieSlot);
        string top    = format("_bglFrm-->{0}", topSlot);
        out << indent << cookie << " = _bgl_catch_cookie;\n";
        out << indent << top << " = _bglFrameTop;\n";
        string linqSlot = currentTryLinqSlots.count(tc->id) ? format("_bglFrm-->{0}", currentTryLinqSlots[tc->id]) : "";
        if(!linqSlot.empty()) out << indent << linqSlot << " = _bglLinqActiveDepth;\n";
        string selfSlot = currentTrySelfSlots.count(tc->id) ? format("_bglFrm-->{0}", currentTrySelfSlots[tc->id]) : "";
        string senderSlot = currentTrySelfSlots.count(tc->id) ? format("_bglFrm-->{0}", currentTrySelfSlots[tc->id] + 1) : "";
        if(!selfSlot.empty()) out << indent << selfSlot << " = self; " << senderSlot << " = sender;\n";
        out << indent << "_bgl_tryrc = " << routine << "(_bglFrm);\n";
        out << indent << "_bgl_catch_cookie = " << cookie << ";\n";
        out << indent << "if (_bgl_thrown) {\n";
        out << indent << "    _bgl_thrown = 0;\n";
        // A throw skipped the returns of every routine between it and here, frees included.
        out << indent << "    _bglFrameTop = " << top << ";\n";
        if(!linqSlot.empty()) out << indent << "    _bglLinqActiveDepth = " << linqSlot << ";\n";
        // A throw from another object's method left self and sender there; a send's return reads both.
        if(!selfSlot.empty()) out << indent << "    self = " << selfSlot << "; sender = " << senderSlot << ";\n";
        out << indent << "    " << spillName(tc->catchVarName) << " = _bgl_tryrc;\n";
        if(tc->catchBody != nullptr)
            for(statement* s : tc->catchBody->statements){
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == tc->catchVarName) continue;
                emitStatement(s, indent + "    ");
            }
        out << indent << "}\n";
        // Not thrown: the result is how the body finished.
        if(exits.ret){
            out << indent << "else if (_bgl_tryrc == 1) {\n";
            returnStatement ret; ret.returnExpression = "_bgl_tryret";
            emitReturn(&ret, indent + "    ");
            out << indent << "}\n";
        }
        if(exits.brk){
            out << indent << "else if (_bgl_tryrc == 2) {\n";
            i6RawNode brk; brk.text = "break;";
            emitRawNode(&brk, indent + "    ");
            out << indent << "}\n";
        }
        if(exits.cont){
            out << indent << "else if (_bgl_tryrc == 3) {\n";
            i6RawNode cont; cont.text = "continue;";
            emitRawNode(&cont, indent + "    ");
            out << indent << "}\n";
        }
    }
}
// A `throw`: unhandled-exception guard, then @throw against the active catch cookie.
void i6Emitter::emitThrow(throwStatement* th, const string& indent){
    string val = th->value ? exprText(th->value) : "0";
    out << indent << "if (_bgl_catch_cookie == 0) {\n";
    out << indent << "    print \"^[Unhandled exception]^\";\n";
    out << indent << "    quit;\n";
    out << indent << "}\n";
    // @throw takes a variable or constant, not an expression.
    out << indent << "_bgl_throwv = " << val << ";\n";
    // Then the locals it leaves are released (not the one being thrown, which goes with it).
    string thrown = th->value != nullptr && th->value->tokens.size() == 1 ? th->value->tokens[0] : "";
    auto names = [&](const string& text){ return !thrown.empty() && rawTextHasWord(text, thrown); };
    for(const string& r : th->releases) if(!names(r)) out << indent << spillWord(r) << "\n";
    if(th->leavesRoutine && currentCleanups != nullptr)
        for(auto& [name, text] : *currentCleanups)   // not a local the routine hasn't reached yet
            if(name != thrown && !names(text) && !undeclaredTopLocals.count(name)) out << indent << spillWord(text) << "\n";
    if(isZTarget(currentTarget)) out << indent << "_bgl_thrown = 1;\n";
    out << indent << "@throw _bgl_throwv _bgl_catch_cookie;\n";
}
// A raw I6 island (`#i6{}` verbatim, or a cooked node whose local names get spill/rename applied).
void i6Emitter::emitRawNode(i6RawNode* raw, const string& indent){
    if(inLiftedTry && liftedLoopDepth == 0 && !raw->isI6Island){
        if(raw->text == "break;")   { out << indent << "return 2;\n"; return; }
        if(raw->text == "continue;"){ out << indent << "return 3;\n"; return; }
    }
    // A break/continue leaves the tries entered inside its loop: restore the outermost one's cookie.
    if(!glulxTryStack.empty() && !raw->isI6Island && (raw->text == "break;" || raw->text == "continue;"))
        for(auto& [save, depth] : glulxTryStack)
            if(depth >= liftedLoopDepth){ out << indent << "_bgl_catch_cookie = " << save << ";\n"; break; }
    out << indent;
    // Cooked nodes reference Beguile locals by name → apply spill/rename (spillWord leaves
    // `.property` accesses intact via its word-boundary rules). User `#i6{}` text is verbatim,
    // except that a local living in the frame is reached through its slot.
    string text = raw->cooked ? spillWord(raw->text) : raw->text;
    if(!raw->cooked && !currentSpillAliases.empty())
        text = mapOutsideLiterals(text, [&](string code){
            for(auto& [from, to] : currentSpillAliases) code = replaceWord(code, from, to);
            return groupFrameSlots(code);
        });
    // A renamed local, everywhere the island names it: in I6 a local's name means the local even after
    // `.` (`obj.prop` reads the property numbered by local `prop`), so the rename keeps every meaning.
    // Strings, `!` comments and quoted dictionary words are left alone.
    if(raw->isI6Island && raw->parts.empty() && !currentLocalRenames.empty()){
        string outText;
        size_t i = 0;
        auto isWordChar = [](char c){ return isalnum((unsigned char)c) || c == '_'; };
        while(i < text.size()){
            char c = text[i];
            if(c == '"'){ size_t e = text.find('"', i + 1); e = e == string::npos ? text.size() : e + 1; outText += text.substr(i, e - i); i = e; continue; }
            if(c == '!'){ size_t e = text.find('\n', i); e = e == string::npos ? text.size() : e; outText += text.substr(i, e - i); i = e; continue; }
            if(c == '\''){
                size_t e = text.find('\'', i + 1);
                if(e != string::npos && e - i <= 12 && text.substr(i, e - i).find_first_of(" \n") == string::npos){
                    outText += text.substr(i, e + 1 - i); i = e + 1; continue;
                }
            }
            if((isalpha((unsigned char)c) || c == '_') && (i == 0 || (!isWordChar(text[i-1]) && text[i-1] != '$'))){
                size_t e = i; while(e < text.size() && isWordChar(text[e])) e++;
                string w = text.substr(i, e - i), lw = w;
                transform(lw.begin(), lw.end(), lw.begin(), ::tolower);
                auto it = currentLocalRenames.find(lw);
                // After `.`, a real property's name is the property ($i6Expr writes `obj.col`); any other
                // word there is the local whose value names the property (`obj.prop`, an I6 idiom).
                size_t b = i; while(b > 0 && text[b-1] == ' ') b--;
                bool afterDot = b > 0 && (text[b-1] == '.' || ((text[b-1] == '&' || text[b-1] == '#') && b > 1 && text[b-2] == '.'));
                bool keep = afterDot && languageService.isKnownPropertyName(lw);
                outText += it != currentLocalRenames.end() && !keep ? it->second : w;
                i = e; continue;
            }
            outText += c; i++;
        }
        text = outText;
    }
    emitRawTextWithSourceMap(text, raw->src);
    out << "\n";
}
// A local's deinit runs on a `return` only once its declaration has been passed.
void i6Emitter::armCleanups(statementBlock* body){
    undeclaredTopLocals.clear();
    if(body != nullptr)
        for(statement* s : body->statements)
            if(typeid(*s) == typeid(variableDeclaration)) undeclaredTopLocals.insert(((variableDeclaration*)s)->name);
}
void i6Emitter::emitStatement(statement* stmt, string indent){
    if(!stmt->src.file.empty())
        pushSourceMap(currentLine(), stmt->src.file, stmt->src.line);
    if(typeid(*stmt) == typeid(variableDeclaration)) undeclaredTopLocals.erase(((variableDeclaration*)stmt)->name);
    if(auto* arrLoc = dynamic_cast<arrayDeclaration*>(stmt)){ emitLocalArrayDeclaration(arrLoc, indent); return; }
    if(typeid(*stmt) == typeid(variableDeclaration)) emitLocalDeclaration((variableDeclaration*)stmt, indent);
    else if(typeid(*stmt) == typeid(assignmentStatement)) emitAssignment((assignmentStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(returnStatement)) emitReturn((returnStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(functionCallStatement)) emitFunctionCallStatement((functionCallStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(ifStatement)) emitIfStatement((ifStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(doStatement))    { liftedLoopDepth++; emitDoStatement((doStatement*)stmt, indent);         liftedLoopDepth--; }
    else if(typeid(*stmt) == typeid(whileStatement)) { liftedLoopDepth++; emitWhileStatement((whileStatement*)stmt, indent);   liftedLoopDepth--; }
    else if(typeid(*stmt) == typeid(forStatement))   { liftedLoopDepth++; emitForStatement((forStatement*)stmt, indent);       liftedLoopDepth--; }
    else if(typeid(*stmt) == typeid(forInStatement)) { liftedLoopDepth++; emitForInStatement((forInStatement*)stmt, indent);   liftedLoopDepth--; }
    else if(typeid(*stmt) == typeid(switchStatement)) emitSwitchStatement((switchStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(tryCatchStatement)) emitTryCatch((tryCatchStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(throwStatement)) emitThrow((throwStatement*)stmt, indent);
    else if(typeid(*stmt) == typeid(i6RawNode)) emitRawNode((i6RawNode*)stmt, indent);
}
void i6Emitter::emitInterpolatedSegments(const vector<interpolatedSegment>& segments, string indent){
    for(auto& seg : segments){
        if(!seg.isExpr){
            if(!seg.text.empty())
                out << indent << "print \"" << seg.text << "\";\n";
        } else {
            for(statement* inj : seg.injections)
                emitStatement(inj, indent);

            string rt = seg.expr->resolvedType;
            string exprStr = exprText(seg.expr);

            // A `print` overload declared for exactly this type (`void print(Res r)`) prints it.
            {
                functionDef* overload = nullptr;
                for(typeDef* g : languageService.globals)
                    if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->name == "print"
                       && (!fd->isEmitter || dynamic_cast<i6Block*>(fd->body) != nullptr)
                       && !fd->isPrePassStub && fd->params.size() == 1 && fd->params[0]->type.name == rt){ overload = fd; break; }
                if(overload != nullptr && !rt.empty() && overload->isEmitter){
                    emitterBindings ob; ob.fn = overload; ob.args.push_back(exprStr); ob.trim = emitterTrim::wsSemi;
                    string body = parser.expandEmitterBody(dynamic_cast<i6Block*>(overload->body), ob);
                    out << indent << (currentSpillAliases.empty() ? body : groupFrameSlots(body)) << ";\n";
                    continue;
                }
                if(overload != nullptr && !rt.empty()){
                    string call = (overload->i6name.empty() ? overload->dName() : overload->i6name) + "(" + exprStr + ");";
                    out << indent << (currentSpillAliases.empty() ? call : groupFrameSlots(call)) << "\n";
                    continue;
                }
            }
            // The value's own print(): an object declared as its own type looks at its members, then its
            // class's.
            auto isPrint = [](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                return fd != nullptr && fd->name == "print" && fd->params.empty();
            };
            functionDef* printFn = nullptr;
            if(auto* od = dynamic_cast<objectDef*>(&languageService.getType(rt)))
                for(typeMember* m : od->members) if(isPrint(m)){ printFn = dynamic_cast<functionDef*>(m); break; }
            // A comparison yields eBool, an enum, which prints as `bool` does.
            classDef* cls = parser.getDispatchClass(rt == "ebool" ? "bool" : rt);
            if(printFn == nullptr && cls != nullptr)
                printFn = dynamic_cast<functionDef*>(cls->findMember(isPrint));
            if(printFn != nullptr){

                if(printFn != nullptr && printFn->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(printFn->body)){
                        emitterBindings pb; pb.self = exprStr; pb.val = exprStr;
                        pb.trim = emitterTrim::wsSemi;
                        string body = parser.expandEmitterBody(blk, pb);
                        out << indent << (currentSpillAliases.empty() ? body : groupFrameSlots(body)) << ";\n";
                        continue;
                    }
                }
                if(printFn != nullptr && !printFn->isEmitter){
                    // A receiver that isn't a plain name (an element `a-->(i+1)`) is bracketed for the send.
                    string recv = exprStr;
                    size_t a = recv.find_first_not_of(" "), b = recv.find_last_not_of(" ");
                    recv = a == string::npos ? recv : recv.substr(a, b - a + 1);
                    if(recv.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") != string::npos)
                        recv = "(" + recv + ")";
                    string call = recv + ".print();";
                    out << indent << (currentSpillAliases.empty() ? call : groupFrameSlots(call)) << "\n";
                    continue;
                }
            }

            if(rt == "void"){
                // Ensure exactly one trailing ';'. Some void emitter bodies include the ';'
                // in their template; some don't. Strip-and-add normalizes both.
                string s = exprStr;
                size_t e = s.find_last_not_of(" \t\n\r;");
                if(e != string::npos) s = s.substr(0, e+1);
                out << indent << s << ";\n";
                continue;
            }

            string cast;
            if(rt == "string") cast = "(string)";
            out << indent << "print " << cast << exprStr << ";\n";
        }
    }
}
// Emits an emitter body that contains an interpolatedStringLiteral parameter.
// Splits the body at the parameter reference (word-boundary match) and splices in the print-block.
// Emitter body lines before and after the parameter reference are emitted as I6 statements.
void i6Emitter::emitInterpolatedEmitterBody(const string& body, const string& paramName, const vector<interpolatedSegment>& segments, string indent){
    string dollarParam = "$" + paramName;
    size_t pos = 0;
    size_t paramPos = string::npos;
    while(pos < body.size()){
        size_t found = body.find(dollarParam, pos);
        if(found == string::npos) break;
        bool leftOk  = found == 0 || !(isalnum(body[found-1]) || body[found-1]=='_' || body[found-1]=='$');
        bool rightOk = found+dollarParam.size() >= body.size() || !(isalnum(body[found+dollarParam.size()]) || body[found+dollarParam.size()]=='_');
        if(leftOk && rightOk){ paramPos = found; break; }
        pos = found + dollarParam.size();
    }

    if(paramPos == string::npos){
        string b = body;
        size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
        size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1);
        if(!b.empty())
            out << indent << b << ";\n";
        return;
    }

    string before = body.substr(0, paramPos);
    {
        size_t s=before.find_first_not_of(" \t\n\r;");
        size_t e=before.find_last_not_of(" \t\n\r;");
        before = (s!=string::npos && e!=string::npos) ? before.substr(s, e-s+1) : "";
        if(!before.empty())
            out << indent << before << ";\n";
    }

    emitInterpolatedSegments(segments, indent);

    size_t afterStart = paramPos + dollarParam.size();
    string after = afterStart < body.size() ? body.substr(afterStart) : "";
    {
        size_t s=after.find_first_not_of(" \t\n\r;");
        size_t e=after.find_last_not_of(" \t\n\r;");
        after = (s!=string::npos && e!=string::npos) ? after.substr(s, e-s+1) : "";
        if(!after.empty())
            out << indent << after << ";\n";
    }
}
// Emit a superposed accessor class's I6 `Class` directive right before the backing instance that
// needs it — once. Its source-order emit was withheld (Pass-3 skips superposed classes), so a
// baked backing (in create+populate) triggers it here. When the host object is itself superposed,
// this runs inside the host's captured block, so class + backing + object materialize (or evaporate)
// together. `emittedClasses` is shared with Pass-3's emitClassRecursive so nothing double-emits.
void i6Emitter::emitDeferredBackingClass(classDef* cls){
    if(!cls || !cls->isSuperposed) return;
    if(!emittedClasses.insert(cls).second) return;    // already emitted
    for(classDef* base : cls->baseClasses)
        if(base && base->isSuperposed) emitDeferredBackingClass(base);  // non-superposed bases came out in source order
    generateI6(cls);
    // Emitted in-place (here, inside the host's block) — drop the name-triggered capture so
    // resolvedOutput() won't also revive it.
    const string& k = cls->i6Name();
    superposedBlocks.erase(k);
    superposedBlockMaps.erase(k);
    superposedClassBaseNames.erase(k);
}
// The class of a field that owns an instance of its own (`_bglField_<instance>_<field>`), or null when
// the field holds a plain value or a reference. `visited` holds the classes already on the path.
classDef* i6Emitter::ownedFieldClass(variableDeclaration* vd, const set<classDef*>& visited){
        if(!vd || vd->isStatic || vd->isConst) return nullptr;
        // `ref` members are bare pointer slots (pointer-alias semantics) — they never own
        // backing storage. This also breaks the infinite regress for self-referential ref
        // members (a Node whose `ref Node next` would otherwise back a Node backing a Node...).
        if(vd->isRefLocal) return nullptr;
        // Skip non-data fields
        if(vd->type.name == "attributelist") return nullptr;
        if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") return nullptr;
        // Field type must be a real, statically-instantiable class
        classDef* fieldCls = languageService.findClass(vd->type.name);
        if(!fieldCls || fieldCls->isEmitterClass || fieldCls->isAlias || fieldCls->isExternal) return nullptr;
        // Skip if already on the instantiation path — same-class fields and any indirect cycles
        // are deliberately left at default (references owned elsewhere).
        if(visited.count(fieldCls)) return nullptr;
        // Skip if the field's class manages its own allocation via init emitter (e.g. string).
        bool hasInitEmitter = false;
        for(typeMember* fm : fieldCls->members)
            if(auto* fn = dynamic_cast<functionDef*>(fm))
                if(fn->isEmitter && fn->name == "init" && fn->params.empty()){ hasInitEmitter = true; break; }
        if(hasInitEmitter) return nullptr;
        // Skip if the field's class has no stored fields (it would emit as a plain global, not
        // an object instance — no point auto-backing it).
        bool storesFields = false;
        for(typeMember* fm : fieldCls->members){
            auto* fvd = dynamic_cast<variableDeclaration*>(fm);
            if(!fvd || fvd->isStatic) continue;
            if(fvd->type.name == "attributelist") continue;
            if(fvd->type.name == "grammarrulelist" || fvd->type.name == "grammarrule") continue;
            storesFields = true; break;
        }
        return storesFields ? fieldCls : nullptr;
}

string i6Emitter::synthesizeFieldBackings(classDef* cls, const string& instanceName, set<classDef*>& visited){
    string clause;
    bool first = true;
    for(typeMember* m : cls->members){
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        classDef* fieldCls = ownedFieldClass(vd, visited);
        if(fieldCls == nullptr) continue;

        // Synthesize a backing instance global. Recurse for its own fields. The backing's
        // mangled name uses the field's display form for human readability.
        string backingName = format("_bglField_{0}_{1}", instanceName, vd->dName());
        // The field's class comes first (an inline accessor's class is declared after its host's
        // instances otherwise); `emittedClasses` keeps its source-order emit from repeating it.
        function<void(classDef*)> emitFirst = [&](classDef* c){
            if(c != nullptr && c->isSuperposed){ emitDeferredBackingClass(c); return; }
            if(c == nullptr || !emittedClasses.insert(c).second) return;
            for(classDef* b : c->baseClasses) emitFirst(b);
            generateI6(c);
        };
        emitFirst(fieldCls);
        visited.insert(fieldCls);
        string subClause = synthesizeFieldBackings(fieldCls, backingName, visited);
        visited.erase(fieldCls);
        out << format("{0} {1}", fieldCls->i6Name(), backingName);
        if(!subClause.empty()) out << " " << subClause;
        out << ";\n";

        if(first){ clause = "with "; first = false; } else { clause += ", "; }
        clause += format("{0} {1}", vd->i6name.empty() ? vd->dName() : vd->i6name, backingName);
    }
    // The instance's own `array<char>` buffers, as an object of the class gets.
    set<string> seen;
    cls->forEachMember([&](typeMember* m){
        auto* ad = dynamic_cast<arrayDeclaration*>(m);
        if(ad == nullptr || !ad->isByteArray || ad->isRefLocal || ad->isStatic || !seen.insert(ad->name).second) return;
        string buf = format("{0}_{1}", instanceName, ad->name);
        if(!ad->stringInitializer.empty())
            out << format("Array {0} buffer {1};\n", buf, ad->stringInitializer);
        else if(auto* list = dynamic_cast<initializerList*>(ad->declaredExpressionValue)){
            out << "Array " << buf << " buffer";
            bool bracketed = list->elements.size() == 1;   // `buffer 113` would read as a size
            if(bracketed) out << " [";
            for(expression* e : list->elements){ string t = staticText(e); out << (t.front() == '-' ? " (" + t + ")" : " " + t); }
            out << (bracketed ? " ];\n" : ";\n");
        } else
            out << format("Array {0} buffer {1};\n", buf, max(1, ad->arraySize));
        if(first){ clause = "with "; first = false; } else { clause += ", "; }
        clause += format("{0} {1}", ad->i6name.empty() ? ad->dName() : ad->i6name, buf);
    });
    return clause;
}

// One standalone `array<char>` declaration: I6 hybrid buffer, or the `<buf>` SizedBuffer raw allocation.
// ── Byte-array paths.
//
// Default (no `<buf>`): emit the standard I6 6.30 hybrid buffer —
//   `Array X buffer N;` allocates WORDSIZE bytes for the length WORD
//   followed by N data bytes. Beguile's byteArray `[i]` reads at
//   `$val->($i + WORDSIZE)` to match.
//
// With `<buf>` (languageService.bufInUse): orLibrary SizedBuffer model.
//   - The user-named symbol is a `Global` pointer variable. bglInit
//     points it `WORDSIZE + 2` bytes into a separate raw allocation so
//     the user-visible pointer behaves as an I6 hybrid buffer (length
//     at `X-->0`, data at `X->WORDSIZE`).
//   - The raw allocation carries the prefix metadata: magic at bytes 0-1,
//     size word at bytes 2..(WORDSIZE+1).
//   - bglInit stamps both pieces; see the SizedBuffer setup block above.
//
// String-initialized byte arrays opt out of SizedBuffer — they're
// compile-time literals whose length is fixed and whose I6 hybrid
// length-word convention is sufficient.
void i6Emitter::emitGlobalByteArray(arrayDeclaration* arr){
    // rawArray<char>: bare bytes from offset 0, with no length word.
    if(arr->isRaw){
        auto* seed = dynamic_cast<initializerList*>(arr->declaredExpressionValue);
        out << format("array {0} ->", arr->dName());
        if(!arr->stringInitializer.empty()) out << " " << arr->stringInitializer;
        else if(seed != nullptr){
            for(expression* elem : seed->elements){
                string t = staticText(elem);
                if(!t.empty() && t.front() == '-') out << " (" << t << ")";
                else                                out << " " << t;
            }
            for(int pad = (int)seed->elements.size(); pad < arr->arraySize; pad++) out << " 0";
        }
        else out << " " << max(arr->arraySize, 1);
        out << ";\n";
        return;
    }
    bool bufTracked = languageService.bufInUse;
    if(!arr->stringInitializer.empty()) {
        out << format("array {0} buffer {1};\n", arr->dName(), arr->stringInitializer);
        return;
    }
    auto* byteSeed = dynamic_cast<initializerList*>(arr->declaredExpressionValue);
    if(arr->arraySize > 0 && byteSeed != nullptr) {
        // Seeded capacity (`array<char> name[N] = {…}`). A byte array carries no
        // length/magic trailer, so capacity is all there is: emit the seed values
        // followed by zeros out to N. The SizedBuffer layout has no room to bake
        // literal bytes into its raw allocation, so it is rejected rather than
        // silently dropping the seed.
        if(bufTracked)
            throw runtime_error("i6Emitter: a seeded `array<char> name[N] = {...}` is not supported while <buf> "
                   "is included (the SizedBuffer allocation is stamped at startup). Declare it as "
                   "`array<char> name = {...}` to size it from the seed, or assign elements at runtime.");
        out << format("array {0} buffer", arr->dName());
        bool bracketed = max((int)byteSeed->elements.size(), arr->arraySize) == 1;
        if(bracketed) out << " [";
        for(expression* elem : byteSeed->elements){
            string t = staticText(elem);
            if(!t.empty() && t.front() == '-') out << " (" << t << ")";
            else                                out << " " << t;
        }
        for(int pad = (int)byteSeed->elements.size(); pad < arr->arraySize; pad++) out << " 0";
        out << (bracketed ? " ];\n" : ";\n");
        return;
    }
    if(arr->arraySize > 0) {
        if(bufTracked){
            // SizedBuffer: emit only the raw allocation here. The user-visible
            // pointer (`Global arrName = 0;`) was already emitted by the pre-pass
            // ahead of bglInit; bglInit sets its runtime value to
            // `arrName_raw + WORDSIZE + 2` and stamps the prefix metadata.
            //
            // Raw size = magic(2) + size word(WORDSIZE) + length word(WORDSIZE) + N data bytes.
            // We use `-> NumBytes` (raw byte array, no I6-managed length header) so
            // bytes 0-1 belong to us for the magic prefix.
            out << format("array {0}_raw -> (2 + 2*WORDSIZE + {1});\n",
                          arr->dName(), arr->arraySize);
        } else {
            out << format("array {0} buffer {1};\n", arr->dName(), arr->arraySize);
        }
        return;
    }
    if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
        out << format("array {0} buffer", arr->dName());
        bool bracketed = list->elements.size() == 1;   // `buffer 113` would read as a size
        if(bracketed) out << " [";
        for(expression* elem : list->elements){
            string t = staticText(elem);
            if(!t.empty() && t.front() == '-') out << " (" << t << ")";
            else                                out << " " << t;
        }
        out << (bracketed ? " ];\n" : ";\n");
        return;
    }
    throw runtime_error("i6Emitter: unable to emit byte array.");
}

// One standalone word array: tracked `table` layout, plain `table`, or a flat `-->` rawArray.
// ── Word-array paths.
// With `<array>` included (languageService.arrayInUse): tracked-length layout —
// N data slots + 1 length + 1 magic = N+2 total.
// Without: plain N-slot I6 table. Zero-byte-core principle: programs that
// don't use `<array>` get untracked I6-native arrays with no overhead.
// `rawArray<T>` is a bare flat block (spec §4.9.1): NO count/length/magic, subscript from
void i6Emitter::emitGlobalWordArray(arrayDeclaration* arr){
// `rawArray<T>` is a bare flat block (spec §4.9.1): NO count/length/magic, subscript from
// word 0. It emits with I6's `--> N` (N zero words) / `--> v...` (literal values) form
// rather than `table` (which prefixes a count word). (Members are unaffected — they use the
// property `.&prop-->n` mechanism.)
bool tracked = languageService.arrayInUse && !arr->isRaw;
auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue);
if(arr->arraySize > 0 && list == nullptr) {
    if(arr->isRaw){
        out << format("array {0} --> {1};\n", arr->dName(), arr->arraySize);  // N flat zero words
        return;
    }
    // Sized, uninitialized. Magic+length get stamped at startup by bglInit when
    // tracked (pre-pass collected the name into trackedArraysNeedingMagicInit).
    int slots = tracked ? arr->arraySize + 2 : arr->arraySize;
    out << format("array {0} table {1};\n", arr->dName(), slots);
    return;
}

if(list != nullptr){
    // List-initialized. With tracking, bake the magic + length into the
    // initializer trailing slots. Without, just emit the data. rawArray uses the flat
    // `-->` form (no count/length/magic).
    //
    // A declared `[N]` fixes the capacity independently of the seed: the
    // seed fills the leading slots, the remainder are zeroed out to N, and
    // the baked length stays at the seed count so append/push carry on from
    // there. Without `[N]`, capacity and length are both the seed count.
    int len = list->elements.size();
    int cap = arr->arraySize > 0 ? arr->arraySize : len;
    vector<string> entries;
    for(expression* elem : list->elements){
        string t = staticText(elem);
        // Wrap negative-leading elements in parens so I6 can't read them as
        // a binary minus against the previous element ("...without bracketing,
        // the minus sign '-' is ambiguous").
        entries.push_back(!t.empty() && t.front() == '-' ? "(" + t + ")" : t);
    }
    for(int pad = len; pad < cap; pad++) entries.push_back("0");   // unused capacity
    if(tracked){
        entries.push_back(to_string(len));   // length = seeded count, not capacity
        entries.push_back("$9084");          // magic
    }
    out << format("array {0} {1}", arr->dName(), arr->isRaw ? "-->" : "table");
    // A lone entry would be read by I6 as the array's size; the bracket form makes it an entry.
    if(entries.size() == 1) out << " [ " << entries[0] << " ]";
    else for(const string& e : entries) out << " " << e;
    out << ";\n";
    return;
}

throw runtime_error("i6Emitter: unable to emit array.");
return;
}

// True when a global of this declared type emits as an I6 `Object` directive rather than a `Global`.
bool i6Emitter::globalEmitsAsObjectInstance(variableDeclaration* varNode){
    bool emitAsObjectInstance = false;
typeDef& td = languageService.getType(varNode->type.name);
if(dynamic_cast<objectDef*>(&td)){
    emitAsObjectInstance = true;  // dedicated objectDef type
} else if(auto* cd = dynamic_cast<classDef*>(&td)){
    // The discriminator is `isEmitterClass`: emitter classes (e.g. the primitive
    // wrappers `int`, `bool`, `char`, `string`, etc.) are Beguile-side type labels
    // over an I6 storage form — instances emit as plain `global X;` with the type
    // info erased at the I6 boundary. Non-emitter classes (e.g. `object`, user
    // classes, `extern class Container : object`) are real types — instances emit
    // as I6 `Object X;` directives. `isAlias` types (e.g. `verb`) dissolve to
    // their parent type at emit time and similarly become object instances.
    if(!cd->isEmitterClass)
        emitAsObjectInstance = true;
}
    return emitAsObjectInstance;
}

// The `<Class> <name>` head of an object-instance global, plus any synthesized field backings.
void i6Emitter::emitGlobalObjectInstanceHead(variableDeclaration* varNode, const string& varI6Name){
string typeName = varNode->type.name;
classDef* instCls = languageService.findClass(typeName);
if(instCls) typeName = instCls->i6Name();
// Synthesize backing globals for class-typed fields so that operator= and
// member-access on those fields write into a real instance, not object 0.
// The visited set starts empty so the FIRST level may back a self-typed field
// (giving operator= a real target). Recursion adds the field's class to visited,
// so the backing's own self-typed slot is left empty — breaking the cycle while
// still satisfying the "real instance to write into" requirement at level 0.
string backingClause;
if(instCls && varNode->declaredExpressionValue == nullptr){
    set<classDef*> visited;
    backingClause = synthesizeFieldBackings(instCls, varI6Name, visited);
}
out<<format("{0} {1}", typeName, varI6Name);
if(!backingClause.empty()) out << " " << backingClause;
}

void i6Emitter::emitGlobal(variableDeclaration* varNode){
    if(varNode->isExternal) return;  // extern declarations are type-system only

    //--Array declarations. We use I6 tables and buffers exclusively for Beguile constructs.
    //
    // Beguile-declared standalone WORD arrays use the length-tracking layout:
    //
    //     allocation:  [I6-header=N+2, d0, d1, ..., d(N-1), length, $9084]
    //                                                              ↑
    //                                                        magic at last slot
    //
    //     arr-->0           = header = N+2
    //     arr-->(i+1)       = data[i]      (operator[] unchanged from today)
    //     arr-->(arr-->0-1) = length
    //     arr-->(arr-->0)   = $9084        (magic for runtime detection)
    //
    // Helpers in __beguileCore.bgl (#emitfirst) do the detection probe and
    // pick the right offsets at runtime; this works for both Beguile-decl
    // arrays (tracked) and I6-native extern arrays (untracked fallback).
    //
    // BYTE arrays (array<char>) keep their I6-native buffer layout — length
    // tracking on char buffers is owned by <string> and the byte-prefix has
    // its own conventions (byte 0 = max, byte 1 = current).
    if(auto* arr = dynamic_cast<arrayDeclaration*>(varNode)){
        if(arr->isByteArray){ emitGlobalByteArray(arr); return; }
        emitGlobalWordArray(arr);
        return;
    }

    if(varNode->isConst){
        if(earlyDeclaredGlobals.count(varNode->name)) return;   // declared ahead of the classes
        out << format("constant {0}", varNode->dName());
        if(varNode->declaredExpressionValue != nullptr)
            out << format(" = {0}", staticText(varNode->declaredExpressionValue));
        out << ";\n";
        return;
    }
    if(varNode->type.name == "attribute"){
        // Honour `as <i6name>` (§3.11), as the general global path below does. Without it the
        // DECLARATION used the Beguile name while every `give`/`has` site used the alias, so I6
        // rejected the uses with "No such constant".
        out << format("attribute {0}", varNode->i6name.empty() ? varNode->dName() : varNode->i6name);
        out << ";\n";
        return;
    }
    if(varNode->type.name == "property"){
        // `additive` marks the slot so I6 accumulates values across the class hierarchy
        // (obj + ancestors) instead of the descendant overriding — `Property additive foo;`.
        out << format("property {0}{1}", varNode->isAdditive ? "additive " : "",
                      varNode->i6name.empty() ? varNode->dName() : varNode->i6name);
        out << ";\n";
        return;
    }
    const string& varI6Name = varNode->i6name.empty() ? varNode->dName() : varNode->i6name;
    // A class-typed global is a variable, like a local. A value-class one without an initializer owns an
    // instance, emitted as its own object (`_bglGlobal_<name>`) that the variable starts out holding;
    // any other refers to an instance: empty until assigned, or holding what its initializer names.
    if(varNode->isInstanceBacking){                     // a local's or parameter's instance: the object itself
        emitGlobalObjectInstanceHead(varNode, varI6Name);
        out << ";\n";
        return;
    }
    if(globalOwnsInstance(varNode)){
        emitGlobalObjectInstanceHead(varNode, globalInstanceName(varNode));
        out << ";\n";
        if(earlyDeclaredGlobals.count(varNode->name)) return;
        out << format("global {0} = {1};\n", varI6Name, globalInstanceName(varNode));
        return;
    }
    // Already declared ahead of bglInit so the routine had a valid lvalue to assign to
    // (see the early-declaration pass). Re-declaring here would collide.
    if(earlyDeclaredGlobals.count(varNode->name)) return;
    out<<format("global {0}", varI6Name);
    if(varNode->declaredExpressionValue != nullptr)
        out<<format(" = {0}", staticText(varNode->declaredExpressionValue));
    out<<";\n";
}

// An expression's text for static data: a bare reference to a class-typed global that owns an
// instance names the instance, since static data needs a constant.
// A property's value list is space-separated, so I6 reads `n - 1` there as possibly two values and
// warns; a bracketed expression is one value.
string i6Emitter::propertyValueText(expression* e){
    string t = staticText(e);
    bool quoted = false;
    for(size_t i = 1; i < t.size(); i++){
        char c = t[i];
        if(c == '"' || c == '\'') quoted = !quoted;
        else if(!quoted && (c == '+' || c == '-' || c == '*' || c == '/' || c == '%'))
            return "(" + t + ")";
    }
    return t;
}

string i6Emitter::staticText(expression* e){
    if(e == nullptr) return "";
    string t = e->text();
    string key = t;
    key.erase(0, key.find_first_not_of(" \t"));
    key.erase(key.find_last_not_of(" \t") + 1);
    transform(key.begin(), key.end(), key.begin(), ::tolower);
    auto it = ownedGlobalInstances.find(key);
    return it != ownedGlobalInstances.end() ? it->second : t;
}

// True when a class-typed global owns an instance: its class is a value class and it has no initializer.
bool i6Emitter::globalOwnsInstance(variableDeclaration* vd){
    if(vd == nullptr || vd->isExternal || vd->isInstanceBacking || vd->declaredExpressionValue != nullptr) return false;
    if(dynamic_cast<arrayDeclaration*>(vd) != nullptr) return false;
    classDef* cls = languageService.findClass(vd->type.name);
    return cls != nullptr && !cls->isEmitterClass && !cls->isAlias && !cls->isExternal && isValueClass(cls);
}

// The object a class-typed global owns: `_bglGlobal_<name>`.
string i6Emitter::globalInstanceName(variableDeclaration* varNode){
    return "_bglGlobal_" + (varNode->i6name.empty() ? varNode->dName() : varNode->i6name);
}
// Standalone tracked globals backing this instance's promoted member arrays (own and inherited).
void i6Emitter::emitObjectPromotedArrays(objectDef* obj){
std::function<void(vector<typeMember*>&)> emitPromoted = [&](vector<typeMember*>& members){
    for(typeMember* m : members)
        if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
            if(arr->isPromoted && arr->arraySize > 0){
                string nm = promotedArrayName(obj->dName(), arr->dName());
                out << format("array {0} table", nm);
                for(int k = 0; k < arr->arraySize; k++) out << " 0";
                out << " 0";        // length: starts empty, grows through append
                out << " $9084";    // tracked marker
                out << ";\n";
            }
};
emitPromoted(obj->members);
std::function<void(classDef*)> scan = [&](classDef* c){
    if(c == nullptr) return;
    emitPromoted(c->members);
    for(classDef* b : c->baseClasses) scan(b);
};
scan(obj->objectClass);
}

// Standalone I6 buffers for this instance's `array<char>` members; records each mangled global name.
// The class's `array<char>` members this object doesn't declare itself: storage is per instance, so
// each gets a buffer of its own (named like an own member's), filled from the class's initializer.
vector<arrayDeclaration*> i6Emitter::inheritedByteArrays(objectDef* obj){
    vector<arrayDeclaration*> out;
    set<string> own;
    for(typeMember* m : obj->members) own.insert(m->name);
    if(obj->objectClass != nullptr)
        obj->objectClass->forEachMember([&](typeMember* m){
            auto* a = dynamic_cast<arrayDeclaration*>(m);
            if(a == nullptr || !a->isByteArray || a->isRefLocal || a->isStatic || own.count(a->name)) return;
            own.insert(a->name);
            out.push_back(a);
        });
    return out;
}

void i6Emitter::emitObjectByteArrayMembers(objectDef* obj, map<string, string>& externalArrayNames){
    vector<typeMember*> members = obj->members;
    for(arrayDeclaration* a : inheritedByteArrays(obj)) members.push_back(a);
    for(typeMember* m : members){
if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
    if(arr->isByteArray && arr->isRefLocal){ externalArrayNames[arr->name] = "0"; continue; }   // bound with :=
    if(arr->isByteArray){
        string mangledName = "_" + obj->name + "_" + arr->name;
        // Emit as an I6 hybrid buffer (length word at -->0, data bytes at
        // ->WORDSIZE) so the member backing matches the standalone
        // array<char> layout and the byteArray []/size()/for-in accessors.
        // Mirrors the emitGlobal byte-array path.
        if(!arr->stringInitializer.empty()){
            out << format("Array {0} buffer {1};\n", mangledName, arr->stringInitializer);
        } else if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
            out << format("Array {0} buffer", mangledName);
            bool bracketed = list->elements.size() == 1;   // `buffer 113` would read as a size
            if(bracketed) out << " [";
            for(expression* elem : list->elements){
                string t = staticText(elem);
                if(!t.empty() && t.front() == '-') out << " (" << t << ")";
                else                                out << " " << t;
            }
            out << (bracketed ? " ];\n" : ";\n");
        } else if(trackedByteMemberRaws.count(mangledName + "_raw")){
            out << format("Array {0}_raw -> (2 + 2*WORDSIZE + {1});\n", mangledName, arr->arraySize);
            externalArrayNames[arr->name] = "0";   // bglInit points it at the raw allocation
            continue;
        } else {
            out << format("Array {0} buffer {1};\n", mangledName, arr->arraySize);
        }
        externalArrayNames[arr->name] = mangledName;
    }
}
    }
}

// Bakes one backing instance per owned value-helper member (own and inherited), recording name + decl.
void i6Emitter::emitObjectOwnedMemberInstances(objectDef* obj, map<string, string>& ownedInstanceNames, map<string, variableDeclaration*>& ownedMemberDecl){
// Does this instance override the member with an initializer (→ a reference/value it points
// at, not an owned instance)? Then leave it alone.
auto overriddenWithInit = [&](const string& mname) -> bool {
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->name == mname && vd->declaredExpressionValue) return true;
    return false;
};
auto consider = [&](variableDeclaration* vd){
    if(!vd || vd->isExternal || isHeaderMember(vd->name) || vd->type.name.empty()) return;
    if(ownedInstanceNames.count(vd->name)) return;         // already baked (own beats inherited)
    if(overriddenWithInit(vd->name)) return;               // instance points it elsewhere
    // `ref` members are bare pointer slots: they name something owned elsewhere, so they
    // start empty and are filled with `:=`. Baking a backing here would allocate an object
    // that the first bind throws away, and would make an unbound slot indistinguishable
    // from a bound one. Matches synthesizeFieldBackings, which already skips them.
    if(vd->isRefLocal) return;
    auto* cls = languageService.findClass(vd->type.name);
    if(!cls || cls->isEmitterClass || cls->isAlias || cls->isExternal) return;   // nothing to instantiate
    if(!isValueClass(cls)) return;                          // a reference: refers to an instance owned elsewhere
    // Bake a backing instance when the member needs one to be a live object: it has stored
    // state, OR it has a non-emitter method/operator that dispatches ON an instance (e.g. a
    // fieldless getter/setter accessor whose bodies reach the host through `outer`). A class
    // with only emitter members and no fields needs no instance (emitters inline).
    bool needsInstance = false;
    for(typeMember* cm : cls->members){
        if(auto* cvd = dynamic_cast<variableDeclaration*>(cm))
            if(!cvd->isExternal && !cvd->isStatic){ needsInstance = true; break; }
        if(auto* cfd = dynamic_cast<functionDef*>(cm))
            if(!cfd->isEmitter && !cfd->isExternal){ needsInstance = true; break; }
    }
    if(!needsInstance) return;
    // One backing PER OBJECT (per class instance), so every instance owns independent state.
    string backing = "_" + obj->name + "_" + vd->name;
    emitDeferredBackingClass(cls);   // materialize a withheld superposed accessor class first
    // The member's own class-typed fields need instances too, or `host.slot.inner` is
    // 0 and anything reached through it writes to nothing. File-scope variables already
    // got this; object members were emitted bare, so nesting stopped after one level.
    set<classDef*> nestedVisited;
    string nestedClause = synthesizeFieldBackings(cls, backing, nestedVisited);
    // A class-body initializer (`coins price = coins{ cents = 3; };`) is every instance's starting
    // value: each backing takes its fields.
    if(vd->declaredExpressionValue != nullptr)
        if(objectDef* init = languageService.findObjectType(vd->declaredExpressionValue->text())){
            set<string> nestedNames;
            for(typeMember* m : init->members)
                if(auto* fv = dynamic_cast<variableDeclaration*>(m); fv && fv->declaredExpressionValue != nullptr){
                    string val = staticText(fv->declaredExpressionValue);
                    if(val.empty()) continue;
                    string prop = fv->i6name.empty() ? fv->dName() : fv->i6name;
                    if(nestedClause.find(" " + prop + " ") != string::npos) continue;   // a nested backing already holds it
                    nestedClause += nestedClause.empty() ? "with " : ", ";
                    nestedClause += prop + " " + val;
                }
        }
    out << format("{0} {1}", cls->i6Name(), backing);
    if(!nestedClause.empty()) out << " " << nestedClause;
    out << ";\n";
    ownedInstanceNames[vd->name] = backing;
    ownedMemberDecl[vd->name] = vd;
};
// Own members first (an instance-level declaration wins), then members inherited from the
// object's class chain — so `class Thing { Box b; }` gives every `Thing` instance its own Box.
for(typeMember* m : obj->members) consider(dynamic_cast<variableDeclaration*>(m));
if(obj->objectClass)
    obj->objectClass->forEachMember([&](typeMember* m){ consider(dynamic_cast<variableDeclaration*>(m)); });
}

// Collects `with` wirings for owned/promoted members inherited from the class chain, not redeclared here.
void i6Emitter::collectInheritedOwnedMembers(objectDef* obj, map<string, string>& ownedInstanceNames, map<string, variableDeclaration*>& ownedMemberDecl, vector<pair<string,string>>& inheritedOwned){
    // A promoted array declared on the CLASS still needs each instance's property pointed at
    // that instance's own global — the class declares the property but no data, since the
    // storage is per-instance.
    {
set<string> ownArrNames;
for(typeMember* m : obj->members)
    if(auto* a = dynamic_cast<arrayDeclaration*>(m)) ownArrNames.insert(a->name);
if(obj->objectClass != nullptr)
    obj->objectClass->forEachMember([&](typeMember* m){
        if(auto* a = dynamic_cast<arrayDeclaration*>(m))
            if(a->isPromoted && !ownArrNames.count(a->name)){
                ownArrNames.insert(a->name);
                inheritedOwned.push_back({a->dName(), promotedArrayName(obj->dName(), a->dName())});
            }
    });
    }
    for(arrayDeclaration* a : inheritedByteArrays(obj)){
        string buffer = "_" + obj->name + "_" + a->name;
        inheritedOwned.push_back({a->i6name.empty() ? a->dName() : a->i6name,
                                  trackedByteMemberRaws.count(buffer + "_raw") ? "0" : buffer});   // bglInit sets a tracked one
    }
    {
set<string> ownMemberNames;
for(typeMember* m : obj->members)
    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) ownMemberNames.insert(vd->name);
for(auto& kv : ownedInstanceNames)
    if(!ownMemberNames.count(kv.first)){
        variableDeclaration* vd = ownedMemberDecl[kv.first];
        string propName = (vd && !vd->i6name.empty()) ? vd->i6name
                        : (vd ? vd->dName() : kv.first);
        inheritedOwned.push_back({propName, kv.second});
    }
    }
}

// The instance's whole `with` clause: property members, methods, raw blocks and inherited owned wirings.
void i6Emitter::emitObjectWithClause(objectDef* obj, map<string, string>& externalArrayNames, map<string, string>& ownedInstanceNames, vector<pair<string,string>>& inheritedOwned, bool isVerbInstance){
bool first = true;
for(typeMember* m : obj->members){
    if(auto* arr = dynamic_cast<arrayDeclaration*>(m)){
        // Property array: emit as inline I6 property values
        out << (first ? "  with " : ",\n       ");
        out << (arr->i6name.empty() ? arr->dName() : arr->i6name) << " ";
        auto extIt = externalArrayNames.find(arr->name);
        if(arr->isPromoted){
            // Too large for an I6 property: the storage is a synthesized global emitted
            // just above, and the property holds a pointer to it. One global PER OWNING
            // INSTANCE, so each object keeps independent state.
            out << promotedArrayName(obj->dName(), arr->dName());
        } else if(extIt != externalArrayNames.end()){
            // String-initialized array: emit pointer to external global array
            out << extIt->second;
        } else {
            // A tracked `array<T>` member carries its length in a TRAILING slot, so the
            // data keeps its 0-based `obj.&prop-->n` indexing and only size()/length()
            // change. Capacity is therefore obj.#prop/WORDSIZE - 1. `rawArray<T>` opts
            // out and stays the bare I6 property array an I6 library can read.
            // `array<dictionaryWord>` is I6 PARSER data — the `name` property and its
            // kin, which I6 reads word by word. A trailing length would be read as a
            // dictionary word and corrupt matching, so those keep the bare layout no
            // matter what. Same reasoning as rawArray, applied by element type.
            bool trackedMember = languageService.arrayInUse && !arr->isRaw
                              && !languageService.isAdditiveProperty(
                                     arr->i6name.empty() ? arr->name : arr->i6name);
            int seeded = 0;
            if(auto* list = dynamic_cast<initializerList*>(arr->declaredExpressionValue)){
                for(expression* elem : list->elements){ out << staticText(elem) << " "; seeded++; }
                for(int k = seeded; k < arr->arraySize; k++) out << "0 ";
            } else {
                // N zero slots (capacity is encoded via obj.#prop, not in element 0)
                for(int k = 0; k < arr->arraySize; k++) out << "0 ";
            }
            // Length starts at the seeded count: a list-initialised member is full,
            // a sized-but-unseeded one is empty and grows through append.
            if(trackedMember) out << seeded << " ";
        }
        first = false;
    } else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
        if(vd->isExternal) continue; // alias members: compile-time indirection only
        if(vd->type.name == "attributelist") continue; // handled separately below
        if(vd->type.name == "grammarrulelist" || vd->type.name == "grammarrule") continue; // emitted as I6 Verb directives
        if(isHeaderMember(vd->name)) continue; // emitted in the object's header, not as a 'with' property
        if(isVerbInstance && (vd->name == "meta" || vd->name == "priority" || vd->name == "librarypriority")) continue; // compile-time-only verb fields
        out << (first ? "  with " : ",\n       ");
        // Honor an explicit i6name (`Type member as <i6name>;`) so the emitted I6 property
        // short-name can differ from the Beguile member name — lets a member sidestep an I6
        // symbol clash (e.g. `auto util = _bglUtil as bglUtil` avoids orLibrary's `object util`).
        // Mirrors the function-member path below.
        out << (vd->i6name.empty() ? vd->dName() : vd->i6name) << " ";
        // Owned-instance member: point the property at the backing object baked above.
        auto ownIt = ownedInstanceNames.find(vd->name);
        // Initializer-list value (typically an inherited array<T> member reassigned
        // here as `name = {...}`). expression::text() returns empty for these since
        // their content lives in `elements`, so walk elements explicitly. See the
        // matching comment in emitClass.
        if(ownIt != ownedInstanceNames.end()){
            out << ownIt->second;
        } else if(auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue)){
            for(expression* elem : list->elements) out << staticText(elem) << " ";
        } else if(vd->declaredExpressionValue && runtimeMemberValues.count(vd))
            out << "0";   // a run-time value: bglInit assigns it
        else if(vd->declaredExpressionValue) out << propertyValueText(vd->declaredExpressionValue);
        first = false;
    } else if(auto* fd = dynamic_cast<functionDef*>(m)){
        if(fd->isEmitter) continue; // emitter methods are inlined at call sites, not emitted as properties
        prepareRecursionSave(fd);
        buildSpillMap(fd);
        out << (first ? "  with " : ",\n       ");
        out << (fd->i6name.empty() ? fd->dName() : fd->i6name) << " [";
        string sp;
        if(!isRequiredStub(fd))
        // Emit param/local names through spillName so the header matches the body: it
        // applies the property-shadow rename map (`width` → `_l_width` when a param/local
        // collides with a used property name) that buildSpillMap just built. Emitting the
        // raw display name here (while the body used the renamed form) left `_l_width`
        // undeclared → "'=' applied to undeclared variable". Mirrors emitFunction/emitClass.
        // (spillName == dName when there's no collision.)
        for(paramDef* p : fd->params)
            if(currentSpillAliases.find(p->name) == currentSpillAliases.end())
                { out << sp << spillName(p->name); sp=" "; }
            else if(currentTryParamRaw.count(p->name))
                { out << sp << currentTryParamRaw[p->name]; sp=" "; }
        statementBlock* body = dynamic_cast<statementBlock*>(fd->body);
        vector<variableDeclaration*> locals;
        if(body){
            set<string> seen;
            collectBodyLocals(body, locals, seen);
            for(variableDeclaration* vd : locals)
                if(currentSpillAliases.find(vd->name) == currentSpillAliases.end())
                    { out << sp << spillName(vd->name); sp=" "; }
        }
        for(const string& n : glulxTryLocals(fd)){ out << sp << n; sp=" "; }
        if(currentSpillCount > 0){ out << sp << "_bglFrm"; }
        out << ";\n";
        if(currentSpillCount > 0)
            out << format("    _bglFrm = _bglFrameAlloc({0});\n", currentSpillCount);
        if(!fd->entrySetUp.empty())
            out << "    " << spillWord(fd->entrySetUp) << "\n";
        emitTryParamCopyIns("    ");
        emitParamCopyIns(fd, "    ");
        emitOwnedLocalSetup(locals, "    ");
        // Allocate object-method-local arrays — was SKIPPED here too, so an object property
        // routine (e.g. `extend _bglUi { waitForKey() }`) with a local rawArray<int> buffer
        // got a null slot that crashed glk_select. Frees run on every exit via currentCleanups.
        emitLocalArrayAllocs(fd, locals, body, "    ");
        currentCleanups = fd->cleanups.empty() ? nullptr : &fd->cleanups;
        armCleanups(body);
        if(body)
            for(statement* s : body->statements)
                emitStatement(s, "    ");
        if(currentCleanups != nullptr && !endsInReturn(body))
            for(auto& [varName, cbody] : *currentCleanups)
                out << "    " << spillWord(cbody) << "\n";
        currentCleanups = nullptr;
        if(currentSpillCount > 0 && !endsInReturn(body))
            out << format("    _bglFrameFree({0});\n", currentSpillCount);
        out << "  ]";
        clearSpillMap();
        first = false;
    } else if(auto* raw = dynamic_cast<i6RawNode*>(m)){
        // raw i6 property block — emitted verbatim inside 'with'
        string text = raw->text;
        size_t s = text.find_first_not_of(" \t\n\r");
        size_t e = text.find_last_not_of(" \t\n\r");
        if(s != string::npos) text = text.substr(s, e - s + 1);
        out << (first ? "  with " : ",\n       ");
        out << text;
        first = false;
    }
}
// Wire inherited owned members (baked above, not present in obj->members).
for(auto& io : inheritedOwned){
    out << (first ? "  with " : ",\n       ");
    out << io.first << " " << io.second;
    first = false;
}
if(!first) out << "\n";
}

// The instance's `has` clause, assembled from its attributeList members.
void i6Emitter::emitObjectAttributes(objectDef* obj){
    for(typeMember* m : obj->members){
if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
    if(vd->type.name != "attributelist") continue;
    if(auto* list = dynamic_cast<initializerList*>(vd->declaredExpressionValue)){
        out << "  has";
        for(expression* elem : list->elements) out << " " << elem->text();
        out << "\n";
    }
}
    }
}

// The owning class's globalDeclaration emitter body, with $self/$selfsub/$val bound to this instance.
void i6Emitter::emitObjectGlobalDeclarationEmitter(objectDef* obj, const string& objI6Name){
    // The nearest class in the object's ancestry that declares one (a verb subclass inherits verb's).
    const classDef* declaring = nullptr;
    function<void(const classDef*)> find = [&](const classDef* c){
        if(c == nullptr || declaring != nullptr) return;
        if(!c->globalDeclarationBody.empty()){ declaring = c; return; }
        for(const classDef* b : c->baseClasses) find(b);
    };
    find(obj->objectClass);
    if(declaring != nullptr){
string body = declaring->globalDeclarationBody;
size_t s = body.find_first_not_of(" \t\n\r"); if(s != string::npos) body = body.substr(s);
size_t e = body.find_last_not_of(" \t\n\r");  if(e != string::npos) body = body.substr(0, e+1);
// The standard tokens, $i6Name included. The action routine is named for the action, which keeps
// the verb's own name (`BoxSub` for `-> Box`).
i6Block blk; blk.i6Body = body;
emitterBindings gb;
gb.selfsub = (obj->i6nameAvoidsKeyword || dynamic_cast<verbObjectDef*>(obj) ? obj->dName() : objI6Name) + "sub";
gb.self = objI6Name; gb.val = objI6Name;
if(obj->objectClass != nullptr){ gb.selfType = obj->objectClass->name; gb.cls = obj->objectClass->i6Name(); }
out << parser.expandEmitterBody(&blk, gb) << "\n";
    }
}

void i6Emitter::emitObject(objectDef* obj){
    // Extern objects are defined in I6 (this binding only records their type/member signatures);
    // emit no `Object` directive or a duplicate would collide with the I6-defined one. A bodied
    // `extern object Name { ... }` now lives in `globals` (so it's referenceable), so the emit loop
    // reaches it here — this guard keeps it emission-free.
    if(obj->isExternal) return;

    // Storage for member arrays promoted out of their property (see
    // promoteMemberArrayIfOversized). Emitted BEFORE the object so the `with prop <name>`
    // pointer has a declared target. One global per owning instance, using the tracked
    // layout — header, data, length, magic — so the promoted member behaves exactly like
    // any other array through the `ref` addressing its declaration was marked with.
    emitObjectPromotedArrays(obj);

    // The header members: the object's name and its initial parent, when set.
    string parentValue, instanceName;
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->declaredExpressionValue){
            if(vd->name == "parent") parentValue = staticText(vd->declaredExpressionValue);
            else if(vd->name == "instancename") instanceName = vd->declaredExpressionValue->text();
        }

    // Emit external global arrays for byte-array (array<char>) member arrays.
    // Byte arrays can't be stored as inline property values (those are word-sized),
    // so we emit a standalone Array and store a pointer as the property value.
    map<string, string> externalArrayNames; // member name → mangled global array name
    emitObjectByteArrayMembers(obj, externalArrayNames);

    // Create + populate: a class-typed member that OWNS its instance (a value-helper class with
    // stored fields that does NOT inherit `object`, and has no initializer pointing elsewhere) needs
    // a real backing object, not a bare property slot — otherwise `obj.member.method()` / operator
    // dispatch lands on `nothing`. Bake one instance of the member's class per such member and point
    // the property at it below. World-tree object references (members of `object`-derived classes, or
    // members initialized to an existing object) keep their existing reference semantics.
    map<string, string> ownedInstanceNames;                 // member name → baked backing object name
    map<string, variableDeclaration*> ownedMemberDecl;      // member name → its declaration (for the property short-name)
    emitObjectOwnedMemberInstances(obj, ownedInstanceNames, ownedMemberDecl);

    // Owned members that are INHERITED from the class chain (not redeclared on this instance) still
    // need their property wired to the baked backing; the obj->members property loop won't see them,
    // so emit `with <member> <backing>` for them explicitly (overriding the class-level default).
    vector<pair<string,string>> inheritedOwned;   // (property short-name, backing object name)
    collectInheritedOwnedMembers(obj, ownedInstanceNames, ownedMemberDecl, inheritedOwned);

    // The object's I6 directive is its class's name. An emitter class (such as `_bglObject`, which the
    // runtime's namespace objects instantiate) has no I6 `Class`, so its instances are plain I6
    // `Object`s. (`object` needs no case of its own: I6 reads `object lamp` as its Object directive.)
    string i6ClassName = (obj->objectClass && !obj->objectClass->isEmitterClass)
                         ? obj->objectClass->i6Name() : "object";
    const string& objI6Name = obj->i6name.empty() ? obj->dName() : obj->i6name;
    string header = i6ClassName + " " + objI6Name;
    if(!instanceName.empty()) header += " " + instanceName;
    if(!parentValue.empty())  header += " " + parentValue;
    out << header << "\n";

    // collect property members (includes raw i6 blocks, which emit as 'with' properties)
    // 'parent' is excluded — it's emitted as a positional argument, not a 'with' property
    // 'meta' / 'priority' on a verb are excluded — they're compile-time-only fields lifted by the
    // verb emitter (meta → `Verb meta '…'` directive; priority → grammar sort anchor).
    bool isVerbInstance = (dynamic_cast<verbObjectDef*>(obj) != nullptr);
    bool hasProps = !inheritedOwned.empty();
    for(typeMember* m : obj->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
            if(vd->isExternal) continue; // alias members have no I6 backing
            if(isVerbInstance && (vd->name == "meta" || vd->name == "priority" || vd->name == "librarypriority")) continue;
            if(vd->type.name != "attributelist" && vd->type.name != "grammarrulelist" && vd->type.name != "grammarrule" && !isHeaderMember(vd->name)) { hasProps = true; break; }
        } else if(auto* fd = dynamic_cast<functionDef*>(m)){ if(!fd->isEmitter) { hasProps = true; break; } }
          else if(dynamic_cast<i6RawNode*>(m))  { hasProps = true; break; }

    if(hasProps)
        emitObjectWithClause(obj, externalArrayNames, ownedInstanceNames, inheritedOwned, isVerbInstance);

    // emit attributeList members as I6 'has' line
    emitObjectAttributes(obj);

    out << ";\n";

    // If the object's class has a globalDeclaration emitter, emit it now with $self/$selfsub substituted
    emitObjectGlobalDeclarationEmitter(obj, objI6Name);
}

//===============================================================================================================================
// Verb and grammar emission
//===============================================================================================================================

void i6Emitter::synthesizeChildrenPlacement(){
    // Map object name → objectDef for placement lookups (Beguile names are lowercased by the lexer,
    // as are the parsed child references, so a direct name match is exact).
    map<string, objectDef*> byName;
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            byName[od->name] = od;

    set<string> claimed;                              // child already placed (one parent per world-tree node)
    map<objectDef*, objectDef*> placedParent;         // placed child → its container (for reorder below)

    for(typeDef* g : languageService.globals){
        auto* container = dynamic_cast<objectDef*>(g);
        if(!container || container->childrenPlacement.empty()) continue;
        const string& containerI6 = container->i6name.empty() ? container->dName() : container->i6name;

        for(expression* childExpr : container->childrenPlacement){
            string childName = childExpr->text();
            auto it = byName.find(childName);
            if(it == byName.end()){
                std::cerr << format("WARNING: children placement in '{0}': '{1}' is not a placeable object; skipped.\n",
                                    container->dName(), childName);
                continue;
            }
            objectDef* child = it->second;
            // A world-tree object has exactly one parent. Two containers naming the same child is a
            // genuine conflict — the resulting tree would be ambiguous — so it's a hard error.
            if(claimed.count(childName))
                throw runtime_error(format("children conflict: '{0}' is listed in more than one container's `children` — a world-tree object has exactly one parent.", childName));
            // An explicit `parent` on the child: a conflict ONLY if it names a different container.
            // Declaring the same link both ways (`a.children = { b }` and `b.parent = a`) is redundant
            // but consistent — the explicit `parent` already places it, so accept it silently.
            string ownParent;
            for(typeMember* m : child->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == "parent" && vd->declaredExpressionValue){ ownParent = vd->declaredExpressionValue->text(); break; }
            if(!ownParent.empty()){
                if(ownParent != containerI6)
                    throw runtime_error(format("children conflict: '{0}' is placed in '{1}'.children but also sets `parent = {2}` — a world-tree object has exactly one parent.", childName, container->dName(), ownParent));
                claimed.insert(childName);   // same container, declared both ways — consistent; parent stands
                continue;
            }
            claimed.insert(childName);
            // Desugar: child.parent = container. The existing positional-parent emission does the rest.
            expression* pv = new expression();
            pv->tokens.push_back(containerI6);
            pv->resolvedType = container->name;
            variableDeclaration* pd = new variableDeclaration();
            pd->name = "parent";
            pd->type = languageService.getType("parentprop");
            pd->declaredExpressionValue = pv;
            child->members.push_back(pd);
            placedParent[child] = container;
        }
    }
    // An object's own `parent = x` is a positional parent too, and x may be declared after it.
    {
        map<string, objectDef*> byI6;
        for(auto& [name, od] : byName){
            string n = od->i6name.empty() ? od->dName() : od->i6name;
            transform(n.begin(), n.end(), n.begin(), ::tolower);
            byI6[n] = od;
        }
        for(auto& [name, child] : byName){
            if(placedParent.count(child)) continue;
            for(typeMember* m : child->members){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(!vd || vd->name != "parent" || !vd->declaredExpressionValue) continue;
                string p = vd->declaredExpressionValue->text();
                transform(p.begin(), p.end(), p.begin(), ::tolower);
                if(auto it = byI6.find(p); it != byI6.end() && it->second != child) placedParent[child] = it->second;
                break;
            }
        }
    }
    if(placedParent.empty()) return;

    // I6 requires a positional parent to be DEFINED before the child that names it. A container is
    // often declared after its contents, so reorder `globals` to emit each placed child after its
    // container. Dependency-respecting and stable: non-placed objects keep their relative order, and a
    // container pulled earlier drags in its own container first (nested rooms/containers).
    vector<typeDef*> reordered;
    set<typeDef*> emitted, visiting;
    std::function<void(typeDef*)> place = [&](typeDef* g){
        if(emitted.count(g)) return;
        if(auto* od = dynamic_cast<objectDef*>(g)){
            auto pit = placedParent.find(od);
            if(pit != placedParent.end()){
                if(!visiting.insert(od).second)
                    throw runtime_error(format("'{0}' is its own ancestor: its `parent` chain leads back to it.", od->dName()));
                place(pit->second);   // container first
                visiting.erase(od);
            }
        }
        if(emitted.insert(g).second) reordered.push_back(g);
    };
    for(typeDef* g : languageService.globals) place(g);
    languageService.globals = reordered;
}

void i6Emitter::synthesizePooledOwnedMembers(){
    bool needRowSets = false;    // a pool slot's array-of-arrays rows are its own, refilled at create()
    bool needByteFill = false;   // a pool slot's array<char> buffer is refilled from the class's at create()
    // An owned value-helper member: a non-`object`, non-initialized member whose class needs a live
    // instance — it has stored state, OR a non-emitter method/operator that dispatches on an instance
    // (e.g. a fieldless getter/setter accessor that reaches the host through `outer`).
    auto ownedClass = [&](variableDeclaration* vd) -> classDef* {
        if(!vd || vd->isExternal || vd->isStatic || vd->isRefLocal || isHeaderMember(vd->name)) return nullptr;
        if(vd->type.name.empty()) return nullptr;
        // An inline-object initializer is the starting value of the member's own instance; any other
        // initializer points the member at something that lives elsewhere.
        if(vd->declaredExpressionValue && vd->declaredExpressionValue->text().rfind("_bglanon", 0) != 0) return nullptr;
        auto* cls = languageService.findClass(vd->type.name);
        if(!cls || cls->isEmitterClass || cls->isAlias || cls->isExternal) return nullptr;   // nothing to instantiate
        if(!isValueClass(cls)) return nullptr;             // a reference: refers to an instance owned elsewhere
        for(typeMember* cm : cls->members){
            if(auto* cvd = dynamic_cast<variableDeclaration*>(cm))
                if(!cvd->isExternal && !cvd->isStatic) return cls;   // has stored storage
            if(auto* cfd = dynamic_cast<functionDef*>(cm))
                if(!cfd->isEmitter && !cfd->isExternal) return cls;  // instance-dispatched method/operator
        }
        return nullptr;
    };

    // Collect pooled classes up front (we mutate globals as we go).
    vector<classDef*> pooled;
    for(typeDef* g : languageService.globals)
        if(auto* cd = dynamic_cast<classDef*>(g))
            if(cd->poolSize > 0)
                pooled.push_back(cd);

    for(classDef* cd : pooled){
        // A symbolic pool size can't be expanded into N backing instances at compile time (§8.2.6).
        if(!cd->poolSizeExpr.empty()){
            cd->forEachMember([&](typeMember* m){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(classDef* oc = ownedClass(vd)){
                    string where = vd->src.line > 0 ? format("{0}:{1}:{2}: ", vd->src.file, vd->src.line, max(1, vd->src.col)) : string();
                    throw runtime_error(format("{0}ERROR: class '{1}' is pooled by a name (`[{2}]`), so it cannot have the "
                        "owned member '{3}' (a '{4}'); give the pool a number.", where, cd->dName(), cd->poolSizeExpr,
                        vd->dName(), oc->dName()));
                }
            });
            continue;
        }
        int N = cd->poolSize;

        // Owned members: own declarations plus those inherited from the class chain (dedup by name).
        vector<variableDeclaration*> owned;
        vector<arrayDeclaration*> bytes;   // array<char> members: a buffer per slot, not the class's one
        vector<arrayDeclaration*> nested;  // array-of-arrays members: a set of rows per slot, not the class's one
        set<string> seenName;
        cd->forEachMember([&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd && !seenName.count(vd->name) && ownedClass(vd)){ owned.push_back(vd); seenName.insert(vd->name); }
            auto* ad = dynamic_cast<arrayDeclaration*>(m);
            if(ad && ad->isByteArray && !ad->isRefLocal && !ad->isStatic && !seenName.count(ad->name)){
                bytes.push_back(ad); seenName.insert(ad->name);
            }
            if(ad && !ad->isByteArray && !ad->isRefLocal && !ad->isStatic && isArrayOfArraysElement(ad->elementType)
               && dynamic_cast<initializerList*>(ad->declaredExpressionValue) && !seenName.count(ad->name)){
                nested.push_back(ad); seenName.insert(ad->name);
            }
        });
        // Members whose type manages its own storage (stringObj): taken at create(), released at destroy().
        string lifecycleCreate, lifecycleDestroy;
        {
            set<string> seenLife;
            cd->forEachMember([&](typeMember* m){
                auto* fvd = dynamic_cast<variableDeclaration*>(m);
                if(fvd == nullptr || !seenLife.insert(fvd->name).second) return;
                string path = "self." + (fvd->i6name.empty() ? fvd->dName() : fvd->i6name);
                lifecycleCreate  += memberLifecycleText(fvd, path, "init");
                lifecycleDestroy += memberLifecycleText(fvd, path, "deinit");
            });
            if(!lifecycleCreate.empty()) lifecycleCreate += "\n";
            if(!lifecycleDestroy.empty()) lifecycleDestroy += "\n";
        }
        if(owned.empty() && bytes.empty() && nested.empty() && lifecycleCreate.empty()) continue;

        // Insertion point: just before the pooled class in `globals`, so the backing instances and
        // free-list precede any create/destroy references. (Pass-3 lazy emission pulls the member's
        // own class up ahead of its instances, so we needn't order that here.)
        auto insertBefore = [&](typeDef* node){
            auto& g = languageService.globals;
            size_t pos = g.size();
            for(size_t i = 0; i < g.size(); i++) if(g[i] == (typeDef*)cd){ pos = i; break; }
            g.insert(g.begin() + pos, node);
        };

        string createInject = lifecycleCreate, destroyInject = lifecycleDestroy;
        // `copy()` is the I6 class message, which copies every property — an owned member's pointer to
        // its backing included. The class's own copy routine keeps dst's backings and copies into them.
        if(classDef* alloc = languageService.findClass("bglallocated"); alloc != nullptr && cd->hasAncestor(alloc)){
            string routine = "_bglOwnedCopy_" + cd->dName();
            string locals, save, restore, fields;
            for(size_t k = 0; k < owned.size(); k++){
                variableDeclaration* vd = owned[k];
                string memI6 = vd->i6name.empty() ? vd->dName() : vd->i6name;
                string slot = "s" + to_string(k);
                locals += " " + slot;
                save += format(" {0} = dst.{1};", slot, memI6);
                restore += format(" dst.{0} = {1};", memI6, slot);
                ownedClass(vd)->forEachMember([&](typeMember* cm){
                    auto* fvd = dynamic_cast<variableDeclaration*>(cm);
                    if(!fvd || fvd->isExternal || fvd->isStatic || dynamic_cast<arrayDeclaration*>(fvd) || ownedClass(fvd)) return;
                    string fi6 = fvd->i6name.empty() ? fvd->dName() : fvd->i6name;
                    fields += format(" {0}.{1} = (src.{2}).{1};", slot, fi6, memI6);
                });
            }
            i6RawNode* cp = new i6RawNode();
            cp->text = "[ " + routine + " dst src" + locals + ";\n " + save + "\n  (" + cd->i6Name() + ").copy(dst, src);\n "
                     + restore + "\n " + fields + "\n];\n";
            insertBefore(cp);
            ownedCopyRoutines[cd->i6Name()] = routine;
        }
        for(variableDeclaration* vd : owned){
            classDef* mc = ownedClass(vd);
            string base  = "_" + cd->name + "_" + vd->name;                 // e.g. _thing_b
            string memI6 = vd->i6name.empty() ? vd->dName() : vd->i6name;

            // N preallocated backing instances (plain objectDefs; emitObject + create+populate
            // bake any nested owned members of each).
            string elems;
            for(int k = 0; k < N; k++){
                string boxName = format("{0}_{1}", base, k);
                objectDef* box = new objectDef();
                box->name = boxName;
                box->objectClass = mc;
                insertBefore(box);
                elems += " " + boxName;
            }
            // Compile-time free-list: array of the backings + a top-of-stack counter (starts full).
            i6RawNode* fl = new i6RawNode();
            // Inform 6 reads a lone value as an array's size, so a one-slot list gets an unused second word.
            fl->text = format("Array {0}_free -->{1}{3};\nGlobal {0}_top = {2};\n", base, elems, N, N == 1 ? " 0" : "");
            insertBefore(fl);

            // create(): pop a free backing, wire it, reset its scalar fields to their defaults.
            createInject += format("{0}_top = {0}_top - 1; self.{1} = {0}_free-->{0}_top;\n", base, memI6);
            // The member's initializer (`coins price = coins{ cents = 3; };`) beats the type's defaults.
            objectDef* init = vd->declaredExpressionValue ? languageService.findObjectType(vd->declaredExpressionValue->text()) : nullptr;
            for(typeMember* cm : mc->members){
                auto* fvd = dynamic_cast<variableDeclaration*>(cm);
                if(!fvd || fvd->isExternal || fvd->isStatic) continue;
                if(ownedClass(fvd)) continue;   // nested owned instance: keeps its baked backing
                string fi6 = fvd->i6name.empty() ? fvd->dName() : fvd->i6name;
                // A word array member is emptied: its elements and the length in its last word.
                if(auto* fad = dynamic_cast<arrayDeclaration*>(fvd)){
                    if(!fad->isByteArray && !fad->isRefLocal && fad->arraySize > 0)
                        for(int k = 0; k <= fad->arraySize; k++)
                            createInject += format("self.{0}.&{1}-->{2} = 0;\n", memI6, fi6, k);
                    continue;
                }
                string def = fvd->declaredExpressionValue ? staticText(fvd->declaredExpressionValue) : "0";
                if(init != nullptr)
                    for(typeMember* im : init->members)
                        if(auto* iv = dynamic_cast<variableDeclaration*>(im); iv && iv->name == fvd->name && iv->declaredExpressionValue)
                            def = staticText(iv->declaredExpressionValue);
                if(def.empty()) def = "0";
                createInject += format("self.{0}.{1} = {2};\n", memI6, fi6, def);
            }
            // destroy(): return the backing to the free-list.
            destroyInject += format("{0}_free-->{0}_top = self.{1}; {0}_top = {0}_top + 1;\n", base, memI6);
        }
        for(arrayDeclaration* ad : bytes){
            string base  = "_" + cd->name + "_" + ad->name;               // the class's own buffer is `base`
            string memI6 = ad->i6name.empty() ? ad->dName() : ad->i6name;
            string cap = ad->arraySize > 0 ? to_string(ad->arraySize) : "(" + base + "-->0)";
            i6RawNode* bufs = new i6RawNode();
            string elems;
            for(int k = 0; k < N; k++){
                bufs->text += ad->arraySize <= 0 && !ad->stringInitializer.empty()
                            ? format("Array {0}_{1} buffer {2};\n", base, k, ad->stringInitializer)   // as long as the class's
                            : format("Array {0}_{1} buffer {2};\n", base, k, ad->arraySize > 0 ? ad->arraySize : 1);
                elems += format(" {0}_{1}", base, k);
            }
            bufs->text += format("Array {0}_free -->{1}{3};\nGlobal {0}_top = {2};\n", base, elems, N, N == 1 ? " 0" : "");
            insertBefore(bufs);
            // create(): a free buffer holding the class's initial characters.
            createInject += format("{0}_top = {0}_top - 1; self.{1} = {0}_free-->{0}_top;"
                                   " _bglByteFill(self.{1}, {0}, {2});\n", base, memI6, cap);
            destroyInject += format("{0}_free-->{0}_top = self.{1}; {0}_top = {0}_top + 1;\n", base, memI6);
            needByteFill = true;
        }

        for(arrayDeclaration* ad : nested){
            vector<arrayDeclaration*> proto;
            for(expression* e : dynamic_cast<initializerList*>(ad->declaredExpressionValue)->elements)
                if(auto* row = e != nullptr ? languageService.findGlobalAs<arrayDeclaration>(e->text()) : nullptr) proto.push_back(row);
            if(proto.empty()) continue;
            string base  = "_" + cd->name + "_" + ad->name;
            string memI6 = ad->i6name.empty() ? ad->dName() : ad->i6name;
            size_t R = proto.size();
            string set, protoList, freeList;
            for(int k = 0; k < N; k++){
                for(size_t i = 0; i < R; i++){
                    auto* mine = new arrayDeclaration(*proto[i]);
                    mine->name = mine->displayName = format("{0}_{1}_{2}", base, k, i);
                    insertBefore(mine);
                    set += " " + mine->name;
                }
                freeList += " " + to_string(k);
            }
            for(arrayDeclaration* row : proto) protoList += " " + row->name;
            i6RawNode* tabs = new i6RawNode();
            tabs->text = format("Array {0}_set -->{1};\nArray {0}_proto -->{2}{5};\nArray {0}_free -->{3}{5};\nGlobal {0}_top = {4};\n",
                                base, set + (N * R == 1 ? " 0" : ""), protoList, freeList, N, (R == 1 || N == 1) ? " 0" : "");
            insertBefore(tabs);
            // create(): a free set of rows, holding the class's initial elements.
            createInject += format("{0}_top = {0}_top - 1; _bglRowsTake(self.&{1}, {0}_set, {0}_proto, {2}, {0}_free-->{0}_top);\n",
                                   base, memI6, R);
            destroyInject += format("{0}_free-->{0}_top = _bglRowsSlot(self.&{1}, {0}_set, {2}, {3}); {0}_top = {0}_top + 1;\n",
                                    base, memI6, R, N);
            needRowSets = true;
        }

        // Inject into create()/destroy(): prepend to an existing method, or synthesize one.
        auto injectHook = [&](const string& hookName, const string& code){
            functionDef* fn = nullptr;
            for(typeMember* m : cd->members)
                if(auto* f = dynamic_cast<functionDef*>(m))
                    if(f->name == hookName){ fn = f; break; }
            i6RawNode* raw = new i6RawNode();
            raw->text = code;
            raw->cooked = true;   // compiler-made: it must not stop the routine's parameters being renamed
            if(fn){
                auto* body = dynamic_cast<statementBlock*>(fn->body);
                if(body){ body->statements.insert(body->statements.begin(), raw); poolHookSetUp.insert(raw); return; }
                // Non-statement body (e.g. raw i6 create) — fall through to synthesize instead.
            }
            functionDef* nf = new functionDef();
            nf->name = hookName;
            nf->isEmitter = false;
            nf->returnType.name = "void";
            statementBlock* sb = new statementBlock();
            sb->statements.push_back(raw);
            // The hook this class would have inherited still runs: its parameters and statements
            // follow this class's own set-up (not the ancestor's, which attaches the ancestor's pool).
            function<functionDef*(classDef*)> inherited = [&](classDef* c) -> functionDef* {
                for(classDef* b : c->baseClasses){
                    for(typeMember* m : b->members)
                        if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == hookName) return f;
                    if(functionDef* f = inherited(b)) return f;
                }
                return nullptr;
            };
            if(functionDef* up = inherited(cd))
                if(auto* upBody = dynamic_cast<statementBlock*>(up->body)){
                    nf->params = up->params;
                    for(statement* st : upBody->statements)
                        if(!poolHookSetUp.count(st)) sb->statements.push_back(st);
                }
            poolHookSetUp.insert(raw);
            nf->body = sb;
            cd->members.push_back(nf);
        };
        injectHook("create",  createInject);
        injectHook("destroy", destroyInject);
    }
    if(needRowSets){
        i6RawNode* rows = new i6RawNode();
        rows->text = "[ _bglRowsTake dst set proto r k i j s d;\n"
                     "    for(i = 0: i < r: i++){ d = set-->(k*r + i); s = proto-->i; for(j = 0: j <= s-->0: j++) d-->j = s-->j; dst-->i = d; }\n];\n"
                     "[ _bglRowsSlot dst set r n k; for(k = 0: k < n: k++) if(set-->(k*r) == dst-->0) return k; return 0; ];\n";
        languageService.globals.push_back(rows);
    }
    if(needByteFill){
        i6RawNode* fill = new i6RawNode();
        fill->text = "[ _bglByteFill dst src n i; for(i = 0: i < n: i++) dst->(WORDSIZE + i) = src->(WORDSIZE + i); ];\n";
        languageService.globals.push_back(fill);
    }
}

// `_bglValAssign_<C>` / `_bglValFree_<C>` when the element type is a value class held in arrays, else "".
string i6Emitter::valueElementRoutine(const string& elemType, const string& prefix){
    classDef* cls = elemType.empty() ? nullptr : languageService.findClass(elemType);
    if(cls == nullptr || !languageService.valueElementArrays.count(cls)) return "";
    if(prefix == "_bglValAssign_" && !languageService.valueElementCopyOps.count(cls)) return "";   // nothing to copy with
    return prefix + cls->dName();
}

// Each value class held in arrays gets one pool of instances, sized by the capacities of those
// arrays, plus the routines `array<T>` calls through $opref: `_bglValAssign_<C>(slot, v)` copies v
// into the slot's instance (drawing one from the pool for an empty slot) and `_bglValFree_<C>(v)`
// returns a pool instance. An instance not from the pool (an initializer's inline object) is
// copied into but never returned.
// A member whose type manages its own storage (`stringObj`): its type's `init` or `deinit` emitter,
// expanded for the member at `path`; "" when the type has none.
string i6Emitter::memberLifecycleText(variableDeclaration* fvd, const string& path, const string& which){
    if(fvd == nullptr || fvd->isStatic || fvd->isExternal || fvd->isRefLocal || fvd->isConst) return "";
    classDef* fc = languageService.findClass(fvd->type.name);
    if(fc == nullptr || !fc->isEmitterClass) return "";
    functionDef* fn = nullptr;
    function<void(classDef*)> walk = [&](classDef* c){
        if(c == nullptr || fn != nullptr) return;
        for(typeMember* m : c->members)
            if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == which && f->isEmitter && f->params.empty()
               && dynamic_cast<i6Block*>(f->body) != nullptr){ fn = f; return; }
        for(classDef* b : c->baseClasses) walk(b);
    };
    walk(fc);
    if(fn == nullptr) return "";
    emitterBindings b; b.self = path; b.val = path; b.trim = emitterTrim::wsSemi;
    string t = parser.expandEmitterBody(dynamic_cast<i6Block*>(fn->body), b);
    while(!t.empty() && (t.back() == ';' || isspace((unsigned char)t.back()))) t.pop_back();
    return t.empty() ? t : t + ";";
}

void i6Emitter::synthesizeValueElementPools(){
    auto derivesFrom = [](classDef* c, classDef* base){
        function<bool(classDef*)> walk = [&](classDef* k){
            if(k == nullptr) return false;
            if(k == base) return true;
            for(classDef* b : k->baseClasses) if(walk(b)) return true;
            return false;
        };
        return walk(c);
    };
    for(auto& [cls, arrays] : languageService.valueElementArrays){
        int N = 0;
        for(auto& rec : arrays){
            int cap = rec.decl->arraySize;
            if(auto* list = dynamic_cast<initializerList*>(rec.decl->declaredExpressionValue))
                cap = max(cap, (int)list->elements.size());
            int copies = 1;
            if(rec.hostClass != nullptr){
                copies = 0;
                for(typeDef* g : languageService.globals){
                    if(auto* od = dynamic_cast<objectDef*>(g); od && derivesFrom(od->objectClass, rec.hostClass)) copies++;
                    if(auto* cd = dynamic_cast<classDef*>(g); cd && cd->poolSize > 0 && derivesFrom(cd, rec.hostClass)) copies += cd->poolSize;
                }
            }
            N += cap * copies;
        }
        if(N < 1) N = 1;
        string base = "_bglVal_" + cls->dName();
        auto& g = languageService.globals;
        size_t at = g.size();
        for(size_t i = 0; i < g.size(); i++) if(g[i] == (typeDef*)cls){ at = i + 1; break; }
        string elems;
        for(int k = 0; k < N; k++){
            objectDef* box = new objectDef();
            box->name = format("{0}_{1}", base, k);
            box->objectClass = cls;
            g.insert(g.begin() + at++, box);
            elems += " " + box->name;
            // Its stringObj (or other self-managing) members get their storage once, at startup.
            cls->forEachMember([&](typeMember* m){
                auto* fvd = dynamic_cast<variableDeclaration*>(m);
                if(fvd == nullptr) return;
                string path = box->name + "." + (fvd->i6name.empty() ? fvd->dName() : fvd->i6name);
                if(string init = memberLifecycleText(fvd, path, "init"); !init.empty())
                    languageService.globalInits.push_back({box->name, init});
            });
        }
        // An instance drawn for an empty slot starts from the class's defaults, as a fresh variable does.
        string reset;
        cls->forEachMember([&](typeMember* m){
            auto* fvd = dynamic_cast<variableDeclaration*>(m);
            if(!fvd || fvd->isExternal || fvd->isStatic || isHeaderMember(fvd->name)) return;
            if(fvd->type.name == "attributelist" || fvd->type.name == "grammarrulelist" || fvd->type.name == "grammarrule") return;
            if(classDef* fc = languageService.findClass(fvd->type.name); fc && !fvd->isRefLocal && !fc->isEmitterClass && isValueClass(fc)) return;
            if(!memberLifecycleText(fvd, "x", "init").empty()) return;   // keeps the storage it was given at startup
            if(dynamic_cast<arrayDeclaration*>(fvd)) return;
            string fi6 = fvd->i6name.empty() ? fvd->dName() : fvd->i6name;
            string def = fvd->declaredExpressionValue ? staticText(fvd->declaredExpressionValue) : "0";
            if(def.empty()) def = "0";
            reset += format(" _bglcur.{0} = {1};", fi6, def);
        });
        string copy;
        if(auto it = languageService.valueElementCopyOps.find(cls); it != languageService.valueElementCopyOps.end())
            copy = format(" _bglcur.{0}(_bglv);", it->second);
        i6RawNode* raw = new i6RawNode();
        string n = to_string(N), cn = cls->dName();
        raw->text =
            "Array " + base + "_pool -->" + elems + (N == 1 ? " 0" : "") + ";\n"
            "Array " + base + "_used -> " + n + ";\n"
            "[ _bglValAssign_" + cn + " _bglcur _bglv _bgli;\n"
            "  if(_bglv == 0) return _bglcur;\n"
            "  if(_bglcur == 0){\n"
            "    for(_bgli = 0: _bgli < " + n + ": _bgli++) if(" + base + "_used->_bgli == 0) break;\n"
            "    if(_bgli == " + n + "){ print \"^[** Programming error: all " + n + " array slots for "
                 + cls->dName() + " values are in use **]^\"; return 0; }\n"
            "    " + base + "_used->_bgli = 1; _bglcur = " + base + "_pool-->_bgli;" + reset + "\n"
            "  }\n"
            " " + copy + "\n"
            "  return _bglcur;\n"
            "];\n"
            + (languageService.valueSlotClasses.count(cls) ? string(
            "[ _bglValSlot_") + cn + " _bgla _bgli _bglcur _bglk;\n"
            "  _bglcur = _bgla-->_bgli;\n"
            "  if(_bglcur ~= 0) return _bglcur;\n"
            "  for(_bglk = 0: _bglk < " + n + ": _bglk++) if(" + base + "_used->_bglk == 0) break;\n"
            "  if(_bglk == " + n + "){ print \"^[** Programming error: all " + n + " array slots for "
                 + cls->dName() + " values are in use **]^\"; return 0; }\n"
            "  " + base + "_used->_bglk = 1; _bglcur = " + base + "_pool-->_bglk;" + reset + "\n"
            "  _bgla-->_bgli = _bglcur;\n"
            "  return _bglcur;\n"
            "];\n" : string()) +
            "[ _bglValFree_" + cn + " _bglv _bgli;\n"
            "  for(_bgli = 0: _bgli < " + n + ": _bgli++) if(" + base + "_pool-->_bgli == _bglv){ " + base + "_used->_bgli = 0; return; }\n"
            "];\n";
        g.insert(g.begin() + at, raw);
    }
}

void i6Emitter::liftAllVerbCompileTimeFields(){
    int defaultPriority = languageService.getClassFieldIntDefault("verb", "priority", 10);
    for(verbObjectDef* vd : languageService.verbs){
        vd->priority = defaultPriority;
        vd->isMeta = false;
        auto apply = [&](const vector<typeMember*>& members){
            for(typeMember* m : members){
                auto* mv = dynamic_cast<variableDeclaration*>(m);
                if(!mv || !mv->declaredExpressionValue) continue;
                if(mv->name == "meta"){
                    string v = mv->declaredExpressionValue->text();
                    vd->isMeta = (v == "true" || v == "1");
                } else if(mv->name == "priority"){
                    try { vd->priority = stoi(mv->declaredExpressionValue->text()); }
                    catch(...) {} // non-int literal: leave anchor at BLR default
                }
            }
        };
        // A subclass of verb may set them for all its verbs; the verb's own body has the last word.
        vector<classDef*> chain;
        for(classDef* c = vd->objectClass; c != nullptr && c->name != "verb";
            c = c->baseClasses.empty() ? nullptr : c->baseClasses[0])
            chain.push_back(c);
        for(auto it = chain.rbegin(); it != chain.rend(); ++it) apply((*it)->members);
        apply(vd->members);
        // Stamp own-block grammar lines with the resolved anchor. Extend-block lines and
        // grammar-object rules had their priority stamped during parsing.
        for(grammarLine& gl : vd->grammarLines)
            if(gl.isOwnLine) gl.priority = vd->priority;
    }
    // Populate verbWords from grammar lines. Walk each verb's OWN-BODY grammarLines only —
    // these are the lines that declare the verb's identity (trigger words). Extend-block
    // contributions (isOwnLine=false) are NOT counted here: those add new grammar to the
    // verb (which the emitter will emit as fresh Verb directives for new trigger words),
    // they don't define which words the verb already claims.
    //
    // For extern verbs the same rule applies — but extern verbs typically declare their
    // claimed words via the `grammar = {...}` body form (which sets isOwnLine=true on the
    // parsed lines). A bare `extern verb V;` with no body falls back to [lowercased name].
    for(verbObjectDef* vd : languageService.verbs){
        set<string> seen;
        for(const grammarLine& gl : vd->grammarLines){
            if(!gl.isOwnLine) continue;
            if(seen.insert(gl.verbWord).second) vd->verbWords.push_back(gl.verbWord);
            for(const string& w : gl.additionalVerbWords)
                if(seen.insert(w).second) vd->verbWords.push_back(w);
        }
        if(vd->verbWords.empty() && vd->isExternal && !vd->claimsNoWords)
            vd->verbWords.push_back(vd->name);
    }

    // Pre-seed declaredVerbWords with every extern verb's claimed dictionary words. This makes
    // grammar lines whose first trigger is stdlib-claimed (e.g. {.i, ...} for Inv) route through
    // Extend rather than emit a duplicate Verb directive at I6 level.
    //
    // I6 treats all trigger words on a verb directive as equivalent synonyms — there is no
    // notion of a "primary" word. So if a non-extern verb's grammar mentions a word the
    // stdlib already claims (whether as the line's first dict word or as a `|` alternate),
    // we must emit it as `Extend 'w'` to avoid I6's "two different verb definitions refer
    // to 'w'" error. The fan-out path in emitGrammarLines handles mixed first/existing
    // trigger sets by emitting one I6 directive per word.
    for(verbObjectDef* vd : languageService.verbs)
        if(vd->isExternal)
            for(const string& w : vd->verbWords)
                declaredVerbWords.insert(w);

    // --- Final prep step: fold trigger words shared by multiple NATIVE verbs into one owner ---
    // A dictionary word may be the trigger of only ONE I6 `Verb`. When two NATIVE Beguile verbs
    // both claim a word, the naive path emits `Verb 'w'` for the first declarer and `Extend 'w'`
    // for the rest — leaking I6 `Extend` into a program that is entirely Beguile-owned. That
    // contradicts the model's promise that a totally-native verb never lowers through `Extend`.
    //
    // Fold instead: the first native verb (in declaration order) to claim a word OWNS it, and every
    // later native verb's grammar lines for that word are MOVED into the owner's grammar, tagged with
    // the origin verb's action (`targetVerb`) so each line still routes to its own handler. The owner
    // then resolves to a single pure `Verb 'w' * … -> a * … -> b;` covering every contributor, and the
    // donor verbs emit no grammar directive for that word (their object + action routine still emit).
    //
    // Runs here (not as a separate emit() pass) because it needs exactly what the loops above have
    // just produced: extern-claimed words seeded into declaredVerbWords, so extern-owned words are
    // excluded. A moved line keeps its priority and is marked non-own, so the resolver sorts it into
    // the owner's rule list by priority exactly as an in-verb contribution would — rule order (and
    // thus runtime matching) is identical to the unfolded Verb+Extend form.
    //
    // Excluded from folding:
    //   - extern/library-owned words (already in declaredVerbWords): Beguile does not own that I6
    //     `Verb`, so those lines must still reach it via `Extend` (emitVerbGrammar's fallback path).
    //   - any word touched by a `replace grammar` line: whole-verb replace is per-verb-object
    //     semantics that do not compose across a shared directive; those words keep the Extend path.

    // Words a native `replace grammar` touches — folding these would let one verb's replace wipe
    // another verb's contribution, so leave the whole word to the Extend path.
    set<string> replaceWords;
    for(verbObjectDef* vd : languageService.verbs){
        if(vd->isExternal) continue;
        for(const grammarLine& gl : vd->grammarLines)
            if(gl.isReplaceMode && !gl.verbWord.empty()) replaceWords.insert(gl.verbWord);
    }

    // Owner (first native declarer) per foldable word.
    map<string, verbObjectDef*> ownerOf;
    for(verbObjectDef* vd : languageService.verbs){
        if(vd->isExternal) continue;
        for(const grammarLine& gl : vd->grammarLines){
            const string& w = gl.verbWord;
            if(w.empty() || declaredVerbWords.count(w) || replaceWords.count(w)) continue;
            ownerOf.emplace(w, vd);   // first insert wins → first declarer owns the word
        }
    }

    // Move every non-owner native verb's lines for an owned word into that word's owner.
    for(verbObjectDef* vd : languageService.verbs){
        if(vd->isExternal) continue;
        vector<grammarLine> kept;
        for(grammarLine gl : vd->grammarLines){
            auto it = gl.verbWord.empty() ? ownerOf.end() : ownerOf.find(gl.verbWord);
            if(it != ownerOf.end() && it->second != vd){
                verbObjectDef* owner = it->second;
                if(gl.targetVerb.empty()) gl.targetVerb = vd->name;  // route to this verb's action
                gl.isOwnLine = false;   // a contribution into the owner's Verb; priority sorts it
                owner->grammarLines.push_back(gl);
            } else {
                kept.push_back(gl);
            }
        }
        vd->grammarLines.swap(kept);
    }
}

bool i6Emitter::emitVerbGrammarResolved(const string& verbName, int anchor, bool isMeta, const vector<grammarLine>& lines){
    auto toI6Word = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch;
        return (e.size() == 1) ? ("'" + e + "//'") : ("'" + e + "'");
    };
    auto triggerSetOf = [](const grammarLine& gl){
        vector<string> s; s.push_back(gl.verbWord);
        for(const string& w : gl.additionalVerbWords) s.push_back(w);
        return s;
    };

    // --- Bail conditions (return false → caller uses the Extend-based path) ---
    // A word is "already claimed" if it is in declaredVerbWords at entry: either extern/library-owned
    // (pre-seeded) or owned by an earlier-emitted verb (verbs emit in declaration order, so the first
    // declarer owns the word — exactly I6's one-word-one-verb ownership). Beguile can only resolve a
    // verb to pure `Verb` declarations when it owns every word outright; a claimed word must reach its
    // owning verb via I6 `Extend`, so we defer the whole verb to the Extend-based path.
    for(const grammarLine& gl : lines){
        if(declaredVerbWords.count(gl.verbWord)) return false;
        for(const string& w : gl.additionalVerbWords) if(declaredVerbWords.count(w)) return false;
        // An extend/replace contribution that lists alternate trigger words is ambiguous to route
        // (which group does it join, and does it introduce new words?) — defer to the safe path.
        if(!gl.isOwnLine && !gl.additionalVerbWords.empty()) return false;
    }

    // A resolved group maps a trigger-word set to one emitted I6 `Verb` directive. Groups are seeded
    // by own-block lines; extend/replace lines route to the group owning their (single) trigger word,
    // or form a fresh single-word group when the word is brand new.
    struct Group {
        vector<string> triggerWords;
        set<string> wordSet;
        vector<grammarLine> prefix;   // priority-`first` contributions (matched before own)
        vector<grammarLine> own;      // own-block lines (and fresh-word contributions)
        vector<grammarLine> suffix;   // default / priority-`last` contributions (matched after own)
        bool replaced = false;
        vector<grammarLine> replaceLines;
    };
    vector<Group> groups;
    auto groupForWord = [&](const string& w) -> int {
        for(size_t i = 0; i < groups.size(); i++) if(groups[i].wordSet.count(w)) return (int)i;
        return -1;
    };

    // Partition exactly as the Extend-based path does, so ordering semantics match.
    vector<grammarLine> replaceL, anchorOwn, lessThan, gteNonOwn;
    for(const grammarLine& gl : lines){
        if(gl.isReplaceMode)          replaceL.push_back(gl);
        else if(gl.isOwnLine)         anchorOwn.push_back(gl);
        else if(gl.priority < anchor) lessThan.push_back(gl);
        else                          gteNonOwn.push_back(gl);
    }

    // `replace grammar` on a verb wipes ALL of that verb's grammar — every trigger word, not just the
    // group the replace line happens to name — and the replace lines become the verb's complete new
    // grammar. (It affects only THIS verb; a word another verb also uses is that verb's own business.)
    // So when any replace line is present, discard the verb's normal grammar and treat the replace
    // lines as its whole grammar.
    if(!replaceL.empty()){
        anchorOwn = replaceL;   // the replace lines ARE the verb's new grammar
        lessThan.clear();
        gteNonOwn.clear();
        replaceL.clear();
    }

    // Seed groups from own-block lines. Route by first word; a group's trigger set must be consistent
    // across its own lines (otherwise routing is ambiguous → bail).
    for(const grammarLine& gl : anchorOwn){
        vector<string> ts = triggerSetOf(gl);
        int gi = groupForWord(gl.verbWord);
        if(gi < 0){
            Group g; g.triggerWords = ts; for(const string& w : ts) g.wordSet.insert(w);
            groups.push_back(g); gi = (int)groups.size() - 1;
        } else {
            // First word already grouped: require this line's full trigger set to match that group.
            for(const string& w : ts) if(!groups[gi].wordSet.count(w)) return false;
            if(ts.size() != groups[gi].triggerWords.size()) return false;
        }
        groups[gi].own.push_back(gl);
    }

    // stable-ascending bucket order: for `first` lines this lands lower priority numbers nearer the
    // top of the group's rule list (higher matching priority); for `last` lines, nearer the bottom.
    auto byPriorityAsc = [](vector<grammarLine>& v){
        stable_sort(v.begin(), v.end(),
            [](const grammarLine& a, const grammarLine& b){ return a.priority < b.priority; });
    };
    byPriorityAsc(lessThan);
    byPriorityAsc(gteNonOwn);

    // Route priority-`first` (prefix) and default/`last` (suffix) contributions.
    auto routeExtend = [&](const grammarLine& gl, bool asPrefix) -> bool {
        int gi = groupForWord(gl.verbWord);
        if(gi < 0){
            // Brand-new trigger word → its own single-word group (a fresh `Verb`, priority moot).
            Group g; g.triggerWords = {gl.verbWord}; g.wordSet.insert(gl.verbWord);
            g.own.push_back(gl);
            groups.push_back(g);
            return true;
        }
        (asPrefix ? groups[gi].prefix : groups[gi].suffix).push_back(gl);
        return true;
    };
    for(const grammarLine& gl : lessThan)  if(!routeExtend(gl, /*asPrefix*/true))  return false;
    for(const grammarLine& gl : gteNonOwn) if(!routeExtend(gl, /*asPrefix*/false)) return false;

    // Replace: wipe the target group's rules and install these. A replace on a brand-new word is an
    // add (nothing to wipe) → fresh group.
    for(const grammarLine& gl : replaceL){
        int gi = groupForWord(gl.verbWord);
        if(gi < 0){
            Group g; g.triggerWords = {gl.verbWord}; g.wordSet.insert(gl.verbWord);
            g.own.push_back(gl);   // no wipe; behaves as a fresh Verb
            groups.push_back(g);
        } else {
            groups[gi].replaced = true;
            groups[gi].replaceLines.push_back(gl);
        }
    }

    // --- Emit: one pure `Verb` directive per group ---
    for(Group& g : groups){
        vector<grammarLine> finalLines;
        if(g.replaced){
            finalLines = g.replaceLines;
        } else {
            finalLines.insert(finalLines.end(), g.prefix.begin(), g.prefix.end());
            finalLines.insert(finalLines.end(), g.own.begin(),    g.own.end());
            finalLines.insert(finalLines.end(), g.suffix.begin(), g.suffix.end());
        }
        if(finalLines.empty()) continue;

        string head;
        for(size_t i = 0; i < g.triggerWords.size(); i++){
            if(i) head += " ";
            head += toI6Word(g.triggerWords[i]);
            declaredVerbWords.insert(g.triggerWords[i]);
        }
        out << (isMeta ? format("verb meta {0}\n", head) : format("verb {0}\n", head));
        for(size_t i = 0; i < finalLines.size(); i++){
            const grammarLine& line = finalLines[i];
            out << "    *";
            for(const string& pt : line.patternTokens) out << " " << pt;
            out << format(" -> {0}", line.targetVerb.empty() ? verbName : line.targetVerb);
            if(line.isReverse) out << " reverse";
            if(i + 1 == finalLines.size()) out << ";";
            out << "\n";
        }
    }
    return true;
}

void i6Emitter::emitVerbGrammar(const string& verbName, int anchor, bool isMeta, const vector<grammarLine>& lines){
    // Prefer resolving a fully-Beguile-owned verb down to pure `Verb` declarations (no I6 `Extend`);
    // fall back to the Extend-based path below when the verb touches extern/library-owned words or is
    // otherwise not cleanly resolvable.
    if(emitVerbGrammarResolved(verbName, anchor, isMeta, lines)) return;

    // Separate replace lines from priority-sorted lines. Replace lines emit LAST so I6's
    // `Extend 'w' replace` wipes prior rules for that trigger word.
    vector<grammarLine> replaceLines, normalLines;
    for(const grammarLine& gl : lines){
        if(gl.isReplaceMode) replaceLines.push_back(gl);
        else                 normalLines.push_back(gl);
    }

    // Partition normal contributions relative to the anchor:
    //   anchorOwn  — verb's own block (priority == anchor && isOwnLine)
    //   lessThan   — priority < anchor → I6 `Extend 'w' first` (matches before anchor)
    //   gteNonOwn  — priority >= anchor (non-own) → I6 `Extend 'w'` (matches after anchor — default last)
    vector<grammarLine> anchorOwn, lessThan, gteNonOwn;
    for(const grammarLine& gl : normalLines){
        if(gl.isOwnLine)             anchorOwn.push_back(gl);
        else if(gl.priority < anchor) lessThan.push_back(gl);
        else                          gteNonOwn.push_back(gl);
    }
    // Emit lessThan in DESCENDING priority order, one `Extend 'w' first` directive per priority
    // bucket. Each `Extend first` prepends to the head of I6's rule list, so emitting the highest
    // priority number first and the lowest priority number last leaves the lowest-number bucket
    // at the top of the rule list — exactly the right matching order (lower priority number =
    // higher matching priority). Stable sort within a bucket preserves source order.
    //
    // Emit gteNonOwn in ASCENDING priority order, one `Extend 'w'` directive per priority bucket.
    // Each `Extend` appends to the tail; emitting lower priority numbers first lands them closer
    // to the anchor, with higher priority numbers further down (matched last, as expected).
    auto bucketize = [](vector<grammarLine>& lines, bool descending){
        // Stable-sort within priority; build a list of (priority, lines) buckets in the requested order.
        stable_sort(lines.begin(), lines.end(),
            [descending](const grammarLine& a, const grammarLine& b){
                return descending ? (a.priority > b.priority) : (a.priority < b.priority);
            });
        vector<vector<grammarLine>> buckets;
        for(const grammarLine& gl : lines){
            if(buckets.empty() || buckets.back().back().priority != gl.priority)
                buckets.push_back({});
            buckets.back().push_back(gl);
        }
        return buckets;
    };

    // Own-block lines whose trigger word collides with an existing (extern) verb must
    // emit as Extend rather than Verb (the fan-out path in emitGrammarLines handles this
    // automatically). The First-vs-Last choice for those extends is driven by the verb's
    // own priority against the library verbs' anchor (the BLR `class verb`'s libraryPriority):
    //   priority <  libraryPriority → Extend first (this verb's rules match before the library's)
    //   priority >= libraryPriority → Extend       (appends to the library's rules)
    // Brand-new trigger words on the same own-line are unaffected — emitDirectiveHead
    // emits them as `verb 'w'` regardless of mode.
    int externAnchor = languageService.getClassFieldIntDefault("verb", "librarypriority",
                                                               languageService.getClassFieldIntDefault("verb", "priority", 10));
    extendDirective ownMode = (anchor < externAnchor) ? extendDirective::First : extendDirective::Last;
    if(!anchorOwn.empty()) emitGrammarLines(verbName, anchorOwn, isMeta, ownMode);
    for(auto& bucket : bucketize(lessThan,  /*descending*/true))
        emitGrammarLines(verbName, bucket, isMeta, extendDirective::First);
    for(auto& bucket : bucketize(gteNonOwn, /*descending*/false))
        emitGrammarLines(verbName, bucket, isMeta, extendDirective::Last);
    // Replace lines emit last so all earlier trigger-word occurrences for this verb have
    // already been declared — they'll emit as `Extend 'w' replace`, wiping prior rules.
    if(!replaceLines.empty()) emitGrammarLines(verbName, replaceLines, isMeta, extendDirective::Replace);
}

void i6Emitter::emitVerbObject(verbObjectDef* vd){
    if(vd->isExternal) return;
    // Compile-time fields (meta, priority) have already been lifted by liftAllVerbCompileTimeFields,
    // called once at the start of emit(). Own-block grammar lines have been stamped with the anchor.
    emitObject(vd);   // also fires globalDeclaration emitter if defined on the verb class
    if(vd->grammarLines.empty()) return;
    // A line on a word the library's grammar claims extends that grammar, which must come first:
    // such grammar goes at the end of the program, after the library's grammar is included.
    auto claimedByLibrary = [&](const string& word){
        for(verbObjectDef* v : languageService.verbs)
            if(v->isExternal && find(v->verbWords.begin(), v->verbWords.end(), word) != v->verbWords.end()) return true;
        return false;
    };
    bool extendsLibrary = false;
    for(const grammarLine& gl : vd->grammarLines){
        if(claimedByLibrary(gl.verbWord)) extendsLibrary = true;
        for(const string& w : gl.additionalVerbWords) if(claimedByLibrary(w)) extendsLibrary = true;
    }
    if(extendsLibrary){
        stringstream captured;
        std::swap(out, captured);
        emitVerbGrammar(vd->name, vd->priority, vd->isMeta, vd->grammarLines);
        std::swap(out, captured);
        deferredLibraryGrammar += captured.str();
        deferredGrammarVerbs.insert(vd->name);
        return;
    }
    emitVerbGrammar(vd->name, vd->priority, vd->isMeta, vd->grammarLines);
}

void i6Emitter::emitGrammarRuleListDecl(grammarRuleListDecl* gtd){
    // A grammar object's rules may bind different target verbs per line, so group lines by
    // target verb first. For each verb, look up its lifted anchor + meta and emit via the
    // shared verb-grammar helper so per-rule priority sorts correctly relative to that
    // verb's anchor.
    if(gtd->grammarLines.empty()) return;
    map<string, vector<grammarLine>> byVerb;
    vector<string> verbOrder;
    for(const grammarLine& gl : gtd->grammarLines){
        string target = gl.targetVerb.empty() ? gtd->verbName : gl.targetVerb;
        if(byVerb.find(target) == byVerb.end()) verbOrder.push_back(target);
        byVerb[target].push_back(gl);
    }
    int defaultPriority = languageService.getClassFieldIntDefault("verb", "priority", 10);
    for(const string& targetVerb : verbOrder){
        int anchor = defaultPriority;
        bool isMeta = false;
        string lower = targetVerb;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower){ anchor = v->priority; isMeta = v->isMeta; break; }
        emitVerbGrammar(targetVerb, anchor, isMeta, byVerb[targetVerb]);
    }
}

// Emit a verb synonym directive: `Verb 'w1' 'w2' … = 'anchor';`. The anchor's primary trigger
// word is resolved from the verbObjectDef named by anchorVerb (its first claimed dict word);
// for extern verbs that word is already declared by the library, so the alias is legal here.
void i6Emitter::emitVerbSynonym(verbSynonymDecl* vsd){
    if(vsd->synonymWords.empty()) return;
    auto toI6Word = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch;
        return (e.size() == 1) ? ("'" + e + "//'") : ("'" + e + "'");
    };
    // Resolve the anchor's primary trigger word (verbWords[0]); fall back to the verb name.
    string anchor = vsd->anchorVerb;
    for(verbObjectDef* v : languageService.verbs)
        if(v->name == vsd->anchorVerb){ if(!v->verbWords.empty()) anchor = v->verbWords[0]; break; }

    string line = "verb";
    for(const string& w : vsd->synonymWords)
        line += " " + w;                 // already I6-ready (quoted, with //p / // flags)
    line += " = " + toI6Word(anchor) + ";\n";
    // An alias follows its verb's `Verb` directive, which may have moved to after the library's grammar.
    if(deferredGrammarVerbs.count(vsd->anchorVerb)) deferredLibraryGrammar += line;
    else out << line;
}

// Group grammar lines by verb trigger word; emit one Verb/Extend block per unique trigger word.
// First occurrence of a trigger word → Verb 'word' (always — Extend requires a prior Verb directive).
// Subsequent occurrences emit per `mode`:
//   First   → `extend 'w' first`
//   Last    → `extend 'w'`
//   Replace → `extend 'w' replace`
// Per-line targetVerb overrides verbName (for multi-verb grammar objects).
// When isMeta is true, the first emission of each trigger word emits as `Verb meta 'word'`.
// A library word: one an extern verb claims. Its I6 grouping is unknowable here (an included
// extension may add synonyms), so a line on it always splits with `Extend only`, which is right
// grouped or solo; on a solo word I6 then warns of the emptied verb (see filterVerbSplitWarnings).
bool i6Emitter::isExternWord(const string& w){
    for(verbObjectDef* v : languageService.verbs)
        if(v->isExternal)
            for(const string& vw : v->verbWords)
                if(vw == w) return true;
    return false;
}

void i6Emitter::emitGrammarLines(const string& verbName, const vector<grammarLine>& lines, bool isMeta, extendDirective mode){
    // Group by full trigger set. Multi-trigger lines (additionalVerbWords non-empty) emit as
    // one combined `verb 'w1' 'w2' 'w3' …` directive when all triggers are first-occurrence;
    // single-trigger lines fall through to the per-trigger byWord path. Two lines with the
    // SAME trigger set get consolidated into one directive with multiple `*` pattern entries.
    struct lineEntry { vector<string> patternTokens; string actionName; bool isReverse; bool withI6Synonyms; };
    vector<vector<string>> setOrder;
    map<vector<string>, vector<lineEntry>> bySet;
    for(const grammarLine& line : lines){
        vector<string> triggerSet;
        triggerSet.push_back(line.verbWord);
        for(const string& w : line.additionalVerbWords) triggerSet.push_back(w);
        if(bySet.find(triggerSet) == bySet.end())
            setOrder.push_back(triggerSet);
        string action = line.targetVerb.empty() ? verbName : line.targetVerb;
        bySet[triggerSet].push_back({line.patternTokens, action, line.isReverse, line.withI6Synonyms});
    }

    auto toI6Word = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch;
        return (e.size() == 1) ? ("'" + e + "//'") : ("'" + e + "'");
    };

    // Look up this verb's full verbWords list (declared via BLR / extern body, defaulted at
    // lift time). Used for the Replace-mode special case below.
    string verbNameLower = verbName;
    transform(verbNameLower.begin(), verbNameLower.end(), verbNameLower.begin(), ::tolower);
    const vector<string>* verbWords = nullptr;
    for(verbObjectDef* vd : languageService.verbs)
        if(vd->name == verbNameLower){ verbWords = &vd->verbWords; break; }

    auto isFirstOccurrence = [&](const string& w){
        if(declaredVerbWords.find(w) != declaredVerbWords.end()) return false;
        // Replace mode treats any of this verb's claimed trigger words as pre-declared, so
        // `extend Take { grammar = { {.take, .firmly, noun} }; }` emits `extend 'take' replace`
        // rather than a fresh Verb directive. Extern triggers are already in declaredVerbWords
        // via the lift-time pre-seed; this catches non-extern verbs whose own block hasn't yet
        // emitted by the time the replace directive runs.
        if(mode == extendDirective::Replace && verbWords)
            for(const string& vw : *verbWords)
                if(vw == w) return false;
        return true;
    };

    auto emitDirectiveHead = [&](const string& word, bool isFirst, bool withSynonymsWord){
        if(isFirst){
            declaredVerbWords.insert(word);
            if(isMeta) out << format("verb meta {0}\n", toI6Word(word));
            else       out << format("verb {0}\n",      toI6Word(word));
        } else if(languageService.evictedExternWords.count(word)){
            // Evicted library word being reclaimed by a native verb: peel it off the library verb
            // with `Extend only 'w' replace` (the first emission wipes the library's grammar for w),
            // then any further contribution to the same word appends via plain `Extend only 'w'`.
            if(evictedEmitted.insert(word).second) out << format("extend only {0} replace\n", toI6Word(word));
            else                                   out << format("extend only {0}\n",         toI6Word(word));
        } else if(isExternWord(word) && !withSynonymsWord){
            // DEFAULT for a library word: word-precise. Split it off its verb via
            // `Extend only 'w'` — inherits the library grammar, adds this line, leaves the word's
            // I6 synonyms untouched. Combined with `replace grammar =` it emits `Extend only 'w'
            // replace`, wiping the split word's inherited grammar and installing only these lines.
            // The first split carries the only/replace head; further contributions to the now-split
            // word append with plain `extend`. (The `withI6Synonyms` modifier opts OUT of this, into
            // the whole-group `Extend` below.)
            if(splitEmitted.insert(word).second){
                // Inform 6 keeps `meta` on the word's own Verb, which the split can't change.
                if(isMeta){
                    verbObjectDef* owner = nullptr;
                    for(verbObjectDef* vd : languageService.verbs) if(vd->name == verbNameLower){ owner = vd; break; }
                    string where = owner != nullptr && owner->src.line > 0
                                 ? format("{0}:{1}:{2}: ", owner->src.file, owner->src.line, max(1, owner->src.col)) : string();
                    std::cerr << format("{0}WARNING: meta verb '{1}' takes the library word {2}, which stays a non-meta "
                        "word: commands using it take a turn. Use a word of its own.\n", where, owner != nullptr ? owner->dName() : verbName, toI6Word(word));
                }
                switch(mode){
                    case extendDirective::First:   out << format("extend only {0} first\n",   toI6Word(word)); break;
                    case extendDirective::Last:    out << format("extend only {0}\n",         toI6Word(word)); break;
                    case extendDirective::Replace: out << format("extend only {0} replace\n", toI6Word(word)); break;
                }
            } else {
                switch(mode){
                    case extendDirective::First:   out << format("extend {0} first\n",   toI6Word(word)); break;
                    case extendDirective::Last:    out << format("extend {0}\n",         toI6Word(word)); break;
                    case extendDirective::Replace: out << format("extend {0} replace\n", toI6Word(word)); break;
                }
            }
        } else {
            switch(mode){
                case extendDirective::First:   out << format("extend {0} first\n",   toI6Word(word)); break;
                case extendDirective::Last:    out << format("extend {0}\n",         toI6Word(word)); break;
                case extendDirective::Replace: out << format("extend {0} replace\n", toI6Word(word)); break;
            }
        }
    };

    auto emitPatterns = [&](const vector<lineEntry>& entries){
        for(size_t i = 0; i < entries.size(); i++){
            out << "    *";
            for(const string& pt : entries[i].patternTokens) out << " " << pt;
            out << format(" -> {0}", entries[i].actionName);
            if(entries[i].isReverse) out << " reverse";
            if(i + 1 == entries.size()) out << ";";
            out << "\n";
        }
    };

    for(const vector<string>& triggerSet : setOrder){
        const vector<lineEntry>& entries = bySet[triggerSet];

        // A library word splits by default; it stays whole-group only if a line opted out
        // with `withI6Synonyms` (decided against isExternWord in emitDirectiveHead).
        bool withSynonymsWord = false;
        for(const lineEntry& e : entries) if(e.withI6Synonyms){ withSynonymsWord = true; break; }

        // Combined multi-trigger Verb directive: only legal when ALL triggers are first-
        // occurrence (I6's `Verb 'w1' 'w2' …` doesn't accept already-claimed words without
        // warnings), AND we're emitting a Verb-mode directive (Extend takes a single trigger).
        bool allFirst = true;
        for(const string& w : triggerSet)
            if(!isFirstOccurrence(w)){ allFirst = false; break; }

        if(triggerSet.size() > 1 && allFirst){
            // Single I6 directive listing all triggers.
            string heads;
            for(size_t i = 0; i < triggerSet.size(); i++){
                if(i > 0) heads += " ";
                heads += toI6Word(triggerSet[i]);
                declaredVerbWords.insert(triggerSet[i]);
            }
            if(isMeta) out << format("verb meta {0}\n", heads);
            else       out << format("verb {0}\n",      heads);
            emitPatterns(entries);
        } else {
            // Single-trigger, or multi-trigger with mixed first/existing — fan out per trigger.
            // Each trigger gets the same pattern list (one I6 directive per trigger).
            for(const string& word : triggerSet){
                emitDirectiveHead(word, isFirstOccurrence(word), withSynonymsWord);
                emitPatterns(entries);
            }
        }
    }
}

// A word evicted via `grammar -= {.w}` that a native verb reclaimed was already emitted as
// `Extend only 'w' replace * … -> V;` during that verb's grammar (tracked in evictedEmitted).
// Whatever remains is a pure removal — the author disabled a library command with nobody taking
// the word — so emit an empty `Extend only 'w' replace;`, which strips the word from its library
// verb and leaves it triggering nothing.
void i6Emitter::emitEvictions(){
    out << deferredLibraryGrammar;
    deferredLibraryGrammar.clear();
    auto toI6Word = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch;
        return (e.size() == 1) ? ("'" + e + "//'") : ("'" + e + "'");
    };
    for(const string& w : languageService.evictedExternWords)
        if(!evictedEmitted.count(w))
            out << format("extend only {0} replace;\n", toI6Word(w));
}

i6Emitter emitter;