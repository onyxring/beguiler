#pragma once
// ===============================================================================
// i6Patch.h - unified-diff patches of Inform 6 files.
//
// `#includeI6 "x.patch"` patches an Inform 6 file instead of including one. The patch is an
// ordinary unified diff (`diff -u`, `git diff`); its hunks are applied to the file its `+++`
// header names, found through the include paths. The patched copy is written to the output
// folder, which goes first on the include path, so every include of the original, nested ones
// too, reads the copy.
// ===============================================================================
#include <string>
#include <vector>
#include <map>
#include <set>

using namespace std;

struct i6PatchHunk {
    int oldStart = 0;           // 1-based line in the original the hunk expects to match
    int patchLine = 0;          // line of the `@@` header in the patch file
    vector<string> oldLines;    // context and removed lines
    vector<string> newLines;    // context and added lines
};

struct i6PatchFileSection {
    string target;              // path from the `+++` header, `a/`/`b/` prefix and timestamp removed
    int patchLine = 0;          // line of the `+++` header
    vector<i6PatchHunk> hunks;
};

// Parses a unified diff. False on malformed input, with the message and the patch line.
bool parseUnifiedDiff(const string& text, vector<i6PatchFileSection>& sections, string& error, int& errorLine);

// Applies hunks in order. Each hunk must match exactly (trailing whitespace ignored) at its
// stated line, shifted by the earlier hunks, or at the nearest place it matches.
// False on the first hunk that matches nowhere, with the message and the hunk's patch line.
bool applyHunks(vector<string>& lines, const vector<i6PatchHunk>& hunks, string& error, int& errorLine);

// The Inform 6 files the build patches, keyed by the original's canonical path.
struct i6PatchedFile {
    vector<string> lines;
    string eol = "\n";
    vector<string> patches;     // patch files applied, in order
};

class i6PatchSet {
    public:
        map<string, i6PatchedFile> files;
        // Each #mergeI6 met, for the editor: where it is, the patch it found, and the original of every
        // file the patch names (empty where one wasn't found). Recorded even when the patch fails.
        struct merge { string sourceFile; int line = 0; string patchPath; vector<pair<string, string>> targets; };
        vector<merge> merges;
        set<string> appliedPatches;     // canonical paths; a patch included twice applies once
        string patchedDir;              // set once the copies are written

        // Reads the patch and applies it. On failure returns the message, prefixed with the patch
        // line it concerns; empty on success.
        string applyPatchFile(const string& patchPath, const string& sourceDir, const vector<string>& searchPaths,
                              merge* record = nullptr);
        // Writes the patched copies into `dir`, removing anything else there. Empty on success.
        string writeCopies(const string& dir);
        // The copy's path for an original, or empty.
        string copyPathFor(const string& originalPath) const;
};

bool isI6PatchFile(const string& path);

// A unified diff of `before` into `after` with three lines of context; empty when they don't differ.
// Lines that differ only in trailing whitespace are the same, as they are when a patch is applied.
string makeUnifiedDiff(const string& name, const vector<string>& before, const vector<string>& after);

// The patch tools behind the editor's patch commands (beguiler --make-patch, --apply-patch).
// Returns the exit code, or -1 when the arguments name no patch tool.
int runPatchTool(int argc, char* argv[]);
