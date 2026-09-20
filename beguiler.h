#include <string>
#include <vector>
#include <filesystem>

#include "blorb.h"

using namespace std;

// Locals that beguiler::go()'s pipeline stages share, in the order the stages fill them.
struct CompileJob {
    Blorb                   blorb;        // phase 1 asset scan and phase 2 blorb packaging
    std::vector<BlorbAsset> blorbAssets;  // assets found by the phase 1 scan
    std::filesystem::path   srcPath;      // the source file being compiled
    std::filesystem::path   outDir;       // holds the story file and every intermediate artifact
};

class beguiler {
    public:
        bool go(int, char*[]);                      //entry point.  Returns true on error (non-zero exit code).
    private:
        // --- go() pipeline stages, in the order go() runs them ---
        // Runs the language server instead of compiling when --lsp is present; true when it ran.
        bool tryRunLspServer(int argc, char* argv[]);
        // Pre-scan for blorb settings and the .inf ICL target, then run the phase 1 asset scan.
        void preScanSourceSettings(CompileJob& job);
        // The two parse passes (stub pre-scan, then the main pass); true on a parse error.
        bool parseSource();
        // Whole-program checks over the finished parse tree, then settings defaults and fallbacks.
        void runPostParseChecks();
        // Derives and persists a deterministic IFID when blorb is on and none was supplied.
        void generateBlorbIfid();
        // Resolves settings.informName / settings.informPath — the I6 binary to hand off to.
        void resolveInformBinary(char* argv[]);
        // Resolves the output directory, the transpiled .inf path and the story file path.
        void resolveOutputPaths(CompileJob& job);
        // Writes the .inf, checks no raw property-class access survived, writes the .bgldbg bundle.
        bool emitOutput(CompileJob& job);
        // Opt-in `economy`: computes optimal I6 abbreviations, injects them and adds -e.
        void applyEconomyAbbreviations();
        // Runs the I6 compiler over the .inf, back-mapping its diagnostics; true on failure.
        bool runInform6();
        // Phase 2: packages the story file and assets into a .gblorb/.zblorb.
        void buildBlorb(CompileJob& job);

        bool parseArgs(int, char*[]);               //parse command line args and place them into the global settings struct
        void resolveLibPath(int, char*[]);          //set settings.libPath from -lib= flag or fall back to binary-adjacent beguiLib/
        std::string getPath(std::string filename);  //determine the path of a given filename
        bool writeFile(std::string);                //write the output file when successfully compiled
        void extractBlorbSettings(const std::string& filename); // pre-scan source for blorb settings before full parse
        // .inf-mode pre-pass: read the user's `!%` ICL header and set the Beguile target
        // (plus target_zcode/target_glulx compile-time symbols) so BLR's TARGET_ZCODE
        // conditional include picks the right branch. Recognized flags: `-G` → glulx,
        // `-z` → z-target (default version 5), `-vN` (N=3/5/8) → zN. No-op for non-.inf.
        // Must run before parser.preScanFile loads BLR.
        void extractInfTargetFromIcl(const std::string& filename);
};
