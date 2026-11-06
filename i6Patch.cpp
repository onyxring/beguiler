#include "platform.h"
// ===============================================================================
// i6Patch.cpp - parses unified diffs and applies them to Inform 6 files (see i6Patch.h).
// ===============================================================================
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iostream>

#include "i6Patch.h"
#include "helpers.h"

string resolveIncludePath(const string& name, const string& ext, const filesystem::path& baseDir, const vector<string>& includePaths, bool outsideLibrary = false);

static string rtrim(const string& s){
    size_t e = s.find_last_not_of(" \t\r");
    return e == string::npos ? "" : s.substr(0, e + 1);
}

bool isI6PatchFile(const string& path){
    string ext = filesystem::path(path).extension().string();
    for(char& c : ext) c = (char)tolower(c);
    return ext == ".patch" || ext == ".diff";
}

// `+++ b/lib/verblib.h<TAB>2026-11-03 ...` → `lib/verblib.h`.
static string headerPath(const string& line){
    string p = line.substr(4);
    size_t tab = p.find('\t');
    if(tab != string::npos) p = p.substr(0, tab);
    p = rtrim(p);
    if(p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
    if(p.rfind("a/", 0) == 0 || p.rfind("b/", 0) == 0) p = p.substr(2);
    return p;
}

// `@@ -12,7 +12,8 @@` → old start 12, old count 7, new count 8. A missing count is 1.
static bool parseHunkHeader(const string& line, int& oldStart, int& oldCount, int& newCount){
    int newStart = 0;
    oldCount = newCount = 1;
    const char* s = line.c_str() + 2;
    auto num = [&](int& out){
        while(*s == ' ') s++;
        if(!isdigit((unsigned char)*s)) return false;
        out = 0;
        while(isdigit((unsigned char)*s)) out = out * 10 + (*s++ - '0');
        return true;
    };
    while(*s == ' ') s++;
    if(*s++ != '-' || !num(oldStart)) return false;
    if(*s == ','){ s++; if(!num(oldCount)) return false; }
    while(*s == ' ') s++;
    if(*s++ != '+' || !num(newStart)) return false;
    if(*s == ','){ s++; if(!num(newCount)) return false; }
    while(*s == ' ') s++;
    return s[0] == '@' && s[1] == '@';
}

bool parseUnifiedDiff(const string& text, vector<i6PatchFileSection>& sections, string& error, int& errorLine){
    vector<string> lines;
    {
        istringstream in(text);
        string l;
        while(getline(in, l)){
            if(!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }
    for(size_t i = 0; i < lines.size(); i++){
        const string& l = lines[i];
        if(l.rfind("--- ", 0) == 0 && i + 1 < lines.size() && lines[i + 1].rfind("+++ ", 0) == 0){
            i6PatchFileSection sec;
            sec.target = headerPath(lines[i + 1]);
            sec.patchLine = (int)i + 2;
            if(sec.target == "/dev/null" || headerPath(l) == "/dev/null"){
                error = "a patch can only change an existing file";
                errorLine = (int)i + 1;
                return false;
            }
            sections.push_back(sec);
            i++;
            continue;
        }
        if(l.rfind("@@", 0) != 0) continue;     // `diff --git`, `index`, and any other commentary
        if(sections.empty()){
            error = "merge chunk before any '---'/'+++' file header";
            errorLine = (int)i + 1;
            return false;
        }
        i6PatchHunk h;
        int oldCount, newCount;
        if(!parseHunkHeader(l, h.oldStart, oldCount, newCount)){
            error = "malformed merge chunk header";
            errorLine = (int)i + 1;
            return false;
        }
        h.patchLine = (int)i + 1;
        int oldSeen = 0, newSeen = 0;
        while((oldSeen < oldCount || newSeen < newCount) && i + 1 < lines.size()){
            const string& b = lines[++i];
            char k = b.empty() ? ' ' : b[0];     // some tools write an empty context line as ""
            string body = b.empty() ? "" : b.substr(1);
            if(k == ' ')      { h.oldLines.push_back(body); h.newLines.push_back(body); oldSeen++; newSeen++; }
            else if(k == '-') { h.oldLines.push_back(body); oldSeen++; }
            else if(k == '+') { h.newLines.push_back(body); newSeen++; }
            else if(k == '\\') continue;         // "\ No newline at end of file"
            else { i--; break; }
        }
        if(oldSeen != oldCount || newSeen != newCount){
            error = "merge chunk is shorter than its header says";
            errorLine = h.patchLine;
            return false;
        }
        while(i + 1 < lines.size() && lines[i + 1].rfind("\\", 0) == 0) i++;
        sections.back().hunks.push_back(std::move(h));
    }
    if(sections.empty()){
        error = "no changes found; expected a unified diff";
        errorLine = 1;
        return false;
    }
    for(auto& s : sections)
        if(s.hunks.empty()){
            error = format("no merge chunks for '{0}'", s.target);
            errorLine = s.patchLine;
            return false;
        }
    return true;
}

static bool matchesAt(const vector<string>& lines, size_t at, const vector<string>& want){
    if(at + want.size() > lines.size()) return false;
    for(size_t k = 0; k < want.size(); k++)
        if(rtrim(lines[at + k]) != rtrim(want[k])) return false;
    return true;
}

// With `failed`, a hunk that matches nowhere is skipped and listed there, and the rest still apply.
static bool applyHunksImpl(vector<string>& lines, const vector<i6PatchHunk>& hunks, string& error, int& errorLine,
                           vector<const i6PatchHunk*>* failed){
    long shift = 0;     // lines added minus lines removed by the hunks already applied
    for(const i6PatchHunk& h : hunks){
        // A hunk with no old lines inserts after line oldStart.
        long start = h.oldLines.empty() ? h.oldStart : h.oldStart - 1;
        long expect = start + shift;
        expect = std::clamp(expect, 0L, (long)lines.size());
        long found = -1;
        if(h.oldLines.empty()) found = expect;
        else {
            long limit = (long)lines.size();
            for(long d = 0; d <= limit && found < 0; d++){
                if(expect - d >= 0 && matchesAt(lines, (size_t)(expect - d), h.oldLines)) found = expect - d;
                else if(d > 0 && expect + d < limit && matchesAt(lines, (size_t)(expect + d), h.oldLines)) found = expect + d;
            }
        }
        if(found < 0 && failed){
            failed->push_back(&h);
            continue;
        }
        if(found < 0){
            error = format("merge chunk at line {0} doesn't match the file", h.oldStart);
            errorLine = h.patchLine;
            return false;
        }
        lines.erase(lines.begin() + found, lines.begin() + found + (long)h.oldLines.size());
        lines.insert(lines.begin() + found, h.newLines.begin(), h.newLines.end());
        shift = found - start + ((long)h.newLines.size() - (long)h.oldLines.size());
    }
    return true;
}

bool applyHunks(vector<string>& lines, const vector<i6PatchHunk>& hunks, string& error, int& errorLine){
    return applyHunksImpl(lines, hunks, error, errorLine, nullptr);
}

static bool readLines(const string& path, vector<string>& lines, string* eol = nullptr){
    ifstream in(path, ios::binary);
    if(!in) return false;
    stringstream ss; ss << in.rdbuf();
    string body = ss.str();
    if(eol) *eol = body.find("\r\n") != string::npos ? "\r\n" : "\n";
    istringstream bin(body);
    string l;
    while(getline(bin, l)){
        if(!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    return true;
}

string i6PatchSet::applyPatchFile(const string& patchPath, const string& sourceDir, const vector<string>& searchPaths,
                                  merge* record){
    std::error_code ec;
    string key = filesystem::weakly_canonical(patchPath, ec).string();
    if(record) record->patchPath = patchPath;
    const string patchName = filesystem::path(patchPath).filename().string();

    ifstream in(patchPath, ios::binary);
    if(!in) return format("can't read '{0}'", patchPath);
    stringstream ss; ss << in.rdbuf();

    vector<i6PatchFileSection> sections;
    string err; int errLine = 0;
    if(!parseUnifiedDiff(ss.str(), sections, err, errLine))
        return format("{0}({1}): {2}", patchName, errLine, err);

    vector<string> resolved;
    for(const i6PatchFileSection& sec : sections){
        string target = resolveIncludePath(sec.target, "", filesystem::path(sourceDir), searchPaths);
        if(target.empty()) target = resolveIncludePath(filesystem::path(sec.target).filename().string(), "", filesystem::path(sourceDir), searchPaths);
        resolved.push_back(target);
        if(record) record->targets.push_back({sec.target, target});
    }
    for(size_t i = 0; i < sections.size(); i++)
        if(resolved[i].empty())
            return format("{0}({1}): file '{2}' not found", patchName, sections[i].patchLine, sections[i].target);
    if(appliedPatches.count(key)) return "";

    // Apply every section on scratch copies first, so a failed patch changes nothing.
    map<string, i6PatchedFile> staged;
    for(size_t i = 0; i < sections.size(); i++){
        const i6PatchFileSection& sec = sections[i];
        const string& target = resolved[i];

        auto it = staged.find(target);
        if(it == staged.end()){
            auto prior = files.find(target);
            if(prior != files.end()) it = staged.emplace(target, prior->second).first;
            else {
                i6PatchedFile pf;
                if(!readLines(target, pf.lines, &pf.eol))
                    return format("{0}({1}): can't read '{2}'", patchName, sec.patchLine, target);
                it = staged.emplace(target, std::move(pf)).first;
            }
        }
        if(!applyHunks(it->second.lines, sec.hunks, err, errLine)){
            string by;
            for(const string& p : it->second.patches) by += (by.empty() ? "" : ", ") + filesystem::path(p).filename().string();
            string msg = format("{0}({1}): {2} '{3}'", patchName, errLine, err, filesystem::path(target).filename().string());
            if(!by.empty()) msg += format(" (already patched by {0})", by);
            return msg;
        }
    }
    for(auto& [target, pf] : staged){
        pf.patches.push_back(patchPath);
        files[target] = std::move(pf);
    }
    appliedPatches.insert(key);
    return "";
}

string i6PatchSet::writeCopies(const string& dir){
    namespace fs = filesystem;
    std::error_code ec;
    if(files.empty()){
        if(fs::exists(dir, ec)) fs::remove_all(dir, ec);
        patchedDir.clear();
        return "";
    }
    fs::create_directories(dir, ec);
    set<string> names;
    for(auto& [original, pf] : files){
        string name = fs::path(original).filename().string();
        string lower = name;
        for(char& c : lower) c = (char)tolower(c);
        if(!names.insert(lower).second)
            return format("two patched Inform 6 files are both named '{0}'", name);
    }
    // A copy left from an earlier build would still hide its original.
    for(auto& entry : fs::directory_iterator(dir, ec)){
        string lower = entry.path().filename().string();
        for(char& c : lower) c = (char)tolower(c);
        if(!names.count(lower)) fs::remove_all(entry.path(), ec);
    }
    for(auto& [original, pf] : files){
        string text;
        for(const string& l : pf.lines) text += l + pf.eol;
        fs::path out = fs::path(dir) / fs::path(original).filename();
        // Rewrite only on change, so the copy's timestamp means something.
        ifstream cur(out, ios::binary);
        if(cur){
            stringstream cs; cs << cur.rdbuf();
            if(cs.str() == text) continue;
        }
        ofstream o(out, ios::binary);
        if(!o) return format("can't write '{0}'", out.string());
        o << text;
    }
    patchedDir = dir;
    return "";
}

string i6PatchSet::copyPathFor(const string& originalPath) const {
    if(patchedDir.empty() || !files.count(originalPath)) return "";
    return (filesystem::path(patchedDir) / filesystem::path(originalPath).filename()).string();
}

// ── Diff ─────────────────────────────────────────────────────────────────────

enum class diffOp { equal, removed, added };

// Myers' O((N+M)D) shortest edit script over the lines between a common prefix and suffix.
static vector<pair<diffOp, int>> diffLines(const vector<string>& a, const vector<string>& b){
    vector<string> ta, tb;
    for(const string& l : a) ta.push_back(rtrim(l));
    for(const string& l : b) tb.push_back(rtrim(l));
    int n = (int)ta.size(), m = (int)tb.size();
    int pre = 0;
    while(pre < n && pre < m && ta[pre] == tb[pre]) pre++;
    int suf = 0;
    while(suf < n - pre && suf < m - pre && ta[n - 1 - suf] == tb[m - 1 - suf]) suf++;
    int N = n - pre - suf, M = m - pre - suf;
    auto A = [&](int i) -> const string& { return ta[pre + i]; };
    auto B = [&](int j) -> const string& { return tb[pre + j]; };

    // rounds[d][k + d] = furthest x on diagonal k after d edits.
    vector<vector<int>> rounds;
    int D = 0;
    for(int d = 0; ; d++){
        vector<int> v(2 * d + 1);
        const vector<int>* prev = d ? &rounds[d - 1] : nullptr;
        auto pv = [&](int k){ return (*prev)[k + d - 1]; };
        bool done = false;
        for(int k = -d; k <= d; k += 2){
            int x;
            if(d == 0) x = 0;
            else if(k == -d || (k != d && pv(k - 1) < pv(k + 1))) x = pv(k + 1);
            else x = pv(k - 1) + 1;
            int y = x - k;
            while(x < N && y < M && A(x) == B(y)){ x++; y++; }
            v[k + d] = x;
            if(x >= N && y >= M) done = true;
        }
        rounds.push_back(std::move(v));
        if(done){ D = d; break; }
    }

    vector<pair<diffOp, int>> rev;     // built backwards; index is into a for equal/removed, b for added
    int x = N, y = M;
    for(int d = D; d > 0; d--){
        const vector<int>& prev = rounds[d - 1];
        auto pv = [&](int k){ return prev[k + d - 1]; };
        int k = x - y;
        int pk = (k == -d || (k != d && pv(k - 1) < pv(k + 1))) ? k + 1 : k - 1;
        int px = pv(pk), py = px - pk;
        while(x > px && y > py){ rev.push_back({diffOp::equal, pre + --x}); y--; }
        if(x == px) rev.push_back({diffOp::added, pre + --y});
        else        rev.push_back({diffOp::removed, pre + --x});
    }
    while(x > 0){ rev.push_back({diffOp::equal, pre + --x}); y--; }

    vector<pair<diffOp, int>> ops;
    for(int i = 0; i < pre; i++) ops.push_back({diffOp::equal, i});
    ops.insert(ops.end(), rev.rbegin(), rev.rend());
    for(int i = n - suf; i < n; i++) ops.push_back({diffOp::equal, i});
    return ops;
}

string makeUnifiedDiff(const string& name, const vector<string>& before, const vector<string>& after){
    const int context = 3;
    vector<pair<diffOp, int>> ops = diffLines(before, after);
    // Old and new line numbers (0-based) before each op.
    vector<int> oldAt(ops.size() + 1), newAt(ops.size() + 1);
    for(size_t i = 0; i < ops.size(); i++){
        oldAt[i + 1] = oldAt[i] + (ops[i].first != diffOp::added);
        newAt[i + 1] = newAt[i] + (ops[i].first != diffOp::removed);
    }
    string out;
    size_t i = 0;
    while(i < ops.size()){
        if(ops[i].first == diffOp::equal){ i++; continue; }
        // A hunk runs from `context` lines before this change to `context` lines after the last change
        // that starts within 2 * context equal lines of the one before it.
        size_t first = i >= (size_t)context ? i - context : 0;
        size_t last = i;
        for(size_t j = i; j < ops.size(); j++){
            if(ops[j].first == diffOp::equal) continue;
            if(j - last > (size_t)(2 * context)) break;
            last = j;
        }
        size_t end = std::min(ops.size(), last + 1 + context);
        int oldCount = oldAt[end] - oldAt[first], newCount = newAt[end] - newAt[first];
        int oldStart = oldCount ? oldAt[first] + 1 : oldAt[first];
        int newStart = newCount ? newAt[first] + 1 : newAt[first];
        out += format("@@ -{0},{1} +{2},{3} @@\n", oldStart, oldCount, newStart, newCount);
        for(size_t j = first; j < end; j++){
            auto [op, idx] = ops[j];
            if(op == diffOp::equal)        out += " " + before[idx] + "\n";
            else if(op == diffOp::removed) out += "-" + before[idx] + "\n";
            else                           out += "+" + after[idx] + "\n";
        }
        i = end;
    }
    if(out.empty()) return "";
    return format("--- a/{0}\n+++ b/{0}\n", name) + out;
}

// ── Patch tools ──────────────────────────────────────────────────────────────

static string jsonString(const string& s){
    string o = "\"";
    for(char c : s){
        switch(c){
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if((unsigned char)c < 0x20) o += format("\\u{0:04x}", (int)(unsigned char)c);
                else o += c;
        }
    }
    return o + "\"";
}


// beguiler --make-patch <original> <edited> [<original> <edited> …] -o <patch>
//   Writes the changes from each original to its edited copy as one patch, after the comment lines at
//   the top of the patch it replaces. Prints "no changes" and writes nothing when there are none.
// beguiler --apply-patch <patch> <original> -o <copy>
//   Writes the original with the patch's hunks for it applied, skipping any that no longer match, and
//   prints {"applied": n, "failed": ["<hunk>", …]} as JSON.
int runPatchTool(int argc, char* argv[]){
    if(argc < 2) return -1;
    string mode = argv[1];
    if(mode != "--make-patch" && mode != "--apply-patch") return -1;
    vector<string> files;
    string outPath;
    for(int i = 2; i < argc; i++){
        string a = argv[i];
        if(a == "-o" && i + 1 < argc) outPath = argv[++i];
        else files.push_back(a);
    }
    if(outPath.empty()){ cerr << "Error: " << mode << " needs -o <file>." << endl; return 1; }

    if(mode == "--make-patch"){
        if(files.empty() || files.size() % 2){ cerr << "Error: --make-patch takes pairs of <original> <edited>." << endl; return 1; }
        string patch;
        for(size_t i = 0; i < files.size(); i += 2){
            vector<string> before, after;
            if(!readLines(files[i], before)){ cerr << "Error: can't read '" << files[i] << "'." << endl; return 1; }
            if(!readLines(files[i + 1], after)){ cerr << "Error: can't read '" << files[i + 1] << "'." << endl; return 1; }
            patch += makeUnifiedDiff(filesystem::path(files[i]).filename().string(), before, after);
        }
        if(patch.empty()){ cout << "no changes" << endl; return 0; }
        // Comments at the top of the patch being replaced stay at the top of the new one.
        string header;
        vector<string> previous;
        if(readLines(outPath, previous))
            for(const string& l : previous){
                if(l.rfind("--- ", 0) == 0 || l.rfind("diff ", 0) == 0 || l.rfind("@@", 0) == 0) break;
                header += l + "\n";
            }
        ofstream o(outPath, ios::binary);
        if(!o){ cerr << "Error: can't write '" << outPath << "'." << endl; return 1; }
        o << header << patch;
        return 0;
    }

    if(files.size() != 2){ cerr << "Error: --apply-patch takes <patch> <original>." << endl; return 1; }
    vector<string> patchText;
    ifstream pin(files[0], ios::binary);
    if(!pin){ cerr << "Error: can't read '" << files[0] << "'." << endl; return 1; }
    stringstream ps; ps << pin.rdbuf();
    vector<i6PatchFileSection> sections;
    string err; int errLine = 0;
    if(!parseUnifiedDiff(ps.str(), sections, err, errLine)){
        cerr << "Error: " << filesystem::path(files[0]).filename().string() << "(" << errLine << "): " << err << endl;
        return 1;
    }
    vector<string> lines;
    string eol;
    if(!readLines(files[1], lines, &eol)){ cerr << "Error: can't read '" << files[1] << "'." << endl; return 1; }
    auto lower = [](string s){ for(char& c : s) c = (char)tolower(c); return s; };
    string want = lower(filesystem::path(files[1]).filename().string());
    vector<const i6PatchHunk*> failed;
    int applied = 0;
    for(const i6PatchFileSection& sec : sections){
        if(lower(filesystem::path(sec.target).filename().string()) != want) continue;
        size_t before = failed.size();
        applyHunksImpl(lines, sec.hunks, err, errLine, &failed);
        applied += (int)sec.hunks.size() - (int)(failed.size() - before);
    }
    ofstream o(outPath, ios::binary);
    if(!o){ cerr << "Error: can't write '" << outPath << "'." << endl; return 1; }
    for(const string& l : lines) o << l << eol;
    string json = format("{\"applied\": {0}, \"failed\": [", applied);
    for(size_t i = 0; i < failed.size(); i++){
        const i6PatchHunk& h = *failed[i];
        string text = format("@@ -{0} @@ (patch line {1})\n", h.oldStart, h.patchLine);
        // Show the hunk as removed/added lines, with its context.
        size_t a = 0, b = 0;
        while(a < h.oldLines.size() || b < h.newLines.size()){
            if(a < h.oldLines.size() && b < h.newLines.size() && h.oldLines[a] == h.newLines[b]){ text += " " + h.oldLines[a] + "\n"; a++; b++; }
            else if(a < h.oldLines.size() && (b >= h.newLines.size() || std::find(h.newLines.begin() + b, h.newLines.end(), h.oldLines[a]) == h.newLines.end())){ text += "-" + h.oldLines[a++] + "\n"; }
            else text += "+" + h.newLines[b++] + "\n";
        }
        json += (i ? ", " : "") + jsonString(text);
    }
    cout << json << "]}" << endl;
    return 0;
}
