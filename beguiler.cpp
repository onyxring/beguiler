#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <regex>

#include "settings.h"
#include "beguiler.h"
#include "helpers.h"
#include "bglParser.h"
#include "bglLanguageService.h"
#include "blorb.h"
#include "lspServer.h"

using namespace std;
namespace fs = std::filesystem;

settingsStruct settings;

// Simple pre-scan to extract blorb settings before the full parse.
// Looks for #beguilerSettings { ... generateBlorb = true/false ... blorbAssetPath = "..." ... }
// using basic string search — no lexer needed.
void beguiler::extractBlorbSettings(const string& filename) {
    ifstream f(filename);
    if(!f.is_open()) return;
    string content((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());

    // Find #beguilerSettings block by scanning for the keyword then braces
    size_t pos = 0;
    while(pos < content.size()) {
        size_t found = content.find("#beguilerSettings", pos);
        if(found == string::npos) break;
        size_t open = content.find('{', found);
        if(open == string::npos) break;
        // Find matching close brace
        int depth = 1;
        size_t cur = open + 1;
        while(cur < content.size() && depth > 0) {
            if(content[cur] == '{') depth++;
            else if(content[cur] == '}') depth--;
            cur++;
        }
        string rawBlock = content.substr(open + 1, cur - open - 2);
        // Strip // comment lines before searching
        string block;
        istringstream lines(rawBlock);
        string line;
        while(getline(lines, line)){
            size_t s = line.find_first_not_of(" \t");
            if(s != string::npos && line.size() > s + 1 && line[s] == '/' && line[s+1] == '/')
                continue; // skip comment lines
            block += line + "\n";
        }
        // Lowercase the block for case-insensitive key matching
        string blockLower = block;
        transform(blockLower.begin(), blockLower.end(), blockLower.begin(), ::tolower);

        // Extract: generateBlorb = true/false
        auto extractBool = [&](const string& key, bool& target) {
            size_t k = blockLower.find(key);
            if(k == string::npos) return;
            size_t eq = block.find('=', k + key.size());
            if(eq == string::npos) return;
            size_t valStart = block.find_first_not_of(" \t\r\n", eq + 1);
            if(valStart == string::npos) return;
            if(block.substr(valStart, 4) == "true")  target = true;
            if(block.substr(valStart, 5) == "false") target = false;
        };
        // Extract: key = "value"  (search on lowercased block, extract value from original)
        auto extractStr = [&](const string& key, string& target) {
            if(!target.empty()) return;
            size_t k = blockLower.find(key);
            if(k == string::npos) return;
            size_t eq = block.find('=', k + key.size());
            if(eq == string::npos) return;
            size_t q1 = block.find('"', eq + 1);
            if(q1 == string::npos) return;
            size_t q2 = block.find('"', q1 + 1);
            if(q2 == string::npos) return;
            target = block.substr(q1 + 1, q2 - q1 - 1);
        };

        extractBool("generateblorb",   beguilerSettings.blorbEnabled);
        extractStr( "blorbassetpath", beguilerSettings.blorbAssetPath);

        // Extract target for compile-time #if symbols (target = Glulx / Z3 / Z5 / Z8)
        {
            size_t k = blockLower.find("target");
            if(k != string::npos){
                size_t eq = blockLower.find('=', k + 6);
                if(eq != string::npos){
                    size_t vs = blockLower.find_first_not_of(" \t\r\n", eq + 1);
                    if(vs != string::npos){
                        size_t ve = blockLower.find_first_of(" \t\r\n;}", vs);
                        string val = blockLower.substr(vs, ve - vs);
                        if(val == "glulx")
                            parser.defineSymbol("target_glulx");
                        else if(val == "z3" || val == "z5" || val == "z8")
                            parser.defineSymbol("target_zcode", val.substr(1)); // "3", "5", or "8"
                    }
                }
            }
        }

        pos = cur;
    }
}

bool beguiler::go(int argc, char* argv[]) {

    // LSP mode: run as language server instead of compiler
    for(int i = 1; i < argc; i++)
        if(string(argv[i]) == "--lsp") {
            // Initialize library path — same as parseArgs() does for normal compilation
            const char* envLib = getenv("BEGUILE_LIB");
            settings.libPath = envLib ? string(envLib) : getPath(argv[0]) + "beguilib";
            LspServer lsp;
            lsp.run();
            return false;
        }

   cout << "Beguiler 0.1b : The Beguile-Inform6 Transpiler (" << __DATE__ << ")" << endl;
    if(parseArgs(argc, argv)) return true;

    // Pre-scan for blorb settings so asset scan can run before preScanFile
    extractBlorbSettings(settings.inFile);

    // Phase 1: asset scan — runs before preScanFile so _blorbAssets.bgl exists during parse
    Blorb blorb;
    vector<BlorbAsset> blorbAssets;
    if(beguilerSettings.blorbEnabled) {
        fs::path srcDir = fs::path(settings.inFile).parent_path();
        string assetDir = beguilerSettings.blorbAssetPath.empty()
            ? (srcDir / "assets").string()
            : (fs::path(beguilerSettings.blorbAssetPath).is_absolute()
                ? beguilerSettings.blorbAssetPath
                : (srcDir / beguilerSettings.blorbAssetPath).string());
        blorbAssets = blorb.scanAssets(assetDir);
        string enumFile = (srcDir / "_blorbAssets.bgl").string();
        blorb.writeEnumFile(blorbAssets, enumFile, fs::path(settings.inFile).filename().string());
    }

    parser.preScanFile(settings.inFile);  // pass 1: register all type/object stubs for forward-reference resolution
    if(parser.parseFile(settings.inFile)) return true;

    // Apply defaults declared on beguilerSettingsType schema members for any unset fields
    parser.applySchemaDefaults();
    // Safety fallbacks if schema was not loaded
    if(beguilerSettings.target.empty())      beguilerSettings.target = "glulx";
    if(beguilerSettings.framePoolSize == -1) beguilerSettings.framePoolSize = 64;

    // IFID: may already be set from user's #beguilerSettings or from _blorbAssets.bgl.
    // If still empty and blorb is enabled, generate deterministically from source identity
    // and persist to _blorbAssets.bgl for stability across builds.
    if(beguilerSettings.ifid.empty() && beguilerSettings.blorbEnabled){
        // Deterministic UUID v5-style: hash source filename + author + story
        string seed = fs::path(settings.inFile).filename().string()
                    + "|" + beguilerSettings.author
                    + "|" + beguilerSettings.title;
        // Simple FNV-1a hash to produce 128 bits (4 x 32-bit)
        auto fnv = [](const string& s, uint32_t init) -> uint32_t {
            uint32_t h = init;
            for(char c : s){ h ^= (uint32_t)(unsigned char)c; h *= 0x01000193; }
            return h;
        };
        uint32_t a = fnv(seed, 0x811c9dc5);
        uint32_t b = fnv(seed, 0x050c5d1f);
        uint32_t c = fnv(seed, 0x2166f3a9);
        uint32_t d = fnv(seed, 0x7a3b1e4c);
        b = (b & 0xFFFF0FFF) | 0x00005000; // version 5 (name-based)
        c = (c & 0x3FFFFFFF) | 0x80000000; // variant 1
        char buf[37];
        snprintf(buf, sizeof(buf), "%08X-%04X-%04X-%04X-%04X%08X",
            a, (b >> 16) & 0xFFFF, b & 0xFFFF,
            (c >> 16) & 0xFFFF, c & 0xFFFF, d);
        beguilerSettings.ifid = buf;

        // Persist to _blorbAssets.bgl so it survives across builds
        fs::path srcDir = fs::path(settings.inFile).parent_path();
        string enumFile = (srcDir / "_blorbAssets.bgl").string();
        // Read existing content, prepend the ifid setting if not already present
        string existing;
        { ifstream in(enumFile); if(in.is_open()) existing = string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>()); }
        if(existing.find("#beguilerSettings") == string::npos || existing.find("ifid =") == string::npos){
            string ifidBlock = "#beguilerSettings { ifid = \"" + beguilerSettings.ifid + "\"; }\n";
            ofstream out(enumFile);
            out << ifidBlock << existing;
        }
    }

    // Resolve the I6 binary.  Precedence (highest to lowest):
    //   1. CLI -inform=name   (settings.informName non-empty)
    //   2. #beguilerSettings informPath = "..."  (full path from file)
    //   3. #beguilerSettings informName = "..."  (filename from file)
    //   4. Default: "inform" (relative to compiler binary)
    if(!settings.informName.empty()){
        // CLI wins — build path from the CLI-supplied name
        if(fs::path(settings.informName).is_absolute())
            settings.informPath = settings.informName;
        else
            settings.informPath = getPath(argv[0]) + settings.informName;
    } else if(!beguilerSettings.informBinaryPath.empty()){
        settings.informPath = beguilerSettings.informBinaryPath;
        settings.informName = fs::path(settings.informPath).filename().string();
    } else {
        settings.informName = beguilerSettings.informName.empty() ? "inform" : beguilerSettings.informName;
        settings.informPath = getPath(argv[0]) + settings.informName;
    }

    // Merge beguilerSettings outputPath into settings (CLI -o wins)
    if(settings.outputPath.empty() && !beguilerSettings.outputPath.empty())
        settings.outputPath = beguilerSettings.outputPath;
    if(settings.outputPath.empty())
        settings.outputPath = "output";

    // Compute output file path
    if(settings.outFile.empty()) {
        string extension = "ulx";
        const string& t = beguilerSettings.target;
        if(t == "z3") extension = "z3";
        else if(t == "z5") extension = "z5";
        else if(t == "z6") extension = "z6";
        else if(t == "z8") extension = "z8";
        fs::path p(settings.inFile);
        fs::path outDir = settings.outputPath.empty() ? p.parent_path()
                        : fs::path(settings.outputPath).is_absolute() ? fs::path(settings.outputPath)
                        : p.parent_path() / fs::path(settings.outputPath);
        if(!fs::exists(outDir))
            fs::create_directories(outDir);
        settings.outFile = (outDir / (p.stem().string() + "." + extension)).string();
    }

    if(writeFile(settings.tmpFile)) return true;
    if(settings.debugMode){
        emitter.writeDebugBundle(settings.inFile + ".bgldbg");
    }

    cout<<"Compilation successful. ";
    if(settings.informName=="none"){
        cout<<"Skipping I6 handoff."<<endl;
        return false;
    }else{
        cout<<"Handing off to I6..."<<endl;
        string debugSwitch = settings.debugMode ? " -k" : "";  // -k: write debug info to gameinfo.dbg
        string i6Cmd = settings.informPath + debugSwitch + " " + settings.switches + " " +
                       settings.tmpFile + " " + settings.outFile;
        cout << i6Cmd << endl << endl;
        cout.flush(); cerr.flush();

        // Run I6 via popen and rewrite its diagnostics so .inf line numbers map back to .bgl
        // sources. The source map (i6Line → bglFile + bglLine) is built during emission and
        // lives on the emitter regardless of --debug. Lines that don't match I6's diagnostic
        // pattern pass through verbatim.
        auto resolveBglSource = [&](int i6Line, string& outFile, int& outLine) -> bool {
            // sourceMap is appended in emission order (ascending by i6Line). Find the last
            // entry whose i6Line is <= the target — that gives the most-specific source for
            // any line that falls inside an emitted block.
            bool found = false;
            for(auto& [il, bf, bl] : emitter.sourceMap){
                if(il > i6Line) break;
                outFile = bf; outLine = bl;
                found = true;
            }
            return found;
        };
        // Format: `<file>(<line>): <Severity>: ...` (note: I6 may emit double spaces after `:`).
        regex i6DiagRe(R"(^(.+\.inf)\((\d+)\):\s+(Error|Warning|Fatal error):\s+(.*)$)");

        // Capture stderr too so I6 errors come through (I6 mostly writes to stdout, but be safe).
        string popenCmd = i6Cmd + " 2>&1";
        FILE* pipe = popen(popenCmd.c_str(), "r");
        if(!pipe){
            cerr << "Error running I6!" << endl;
            return true;
        }
        char buf[4096];
        bool sawError = false;
        while(fgets(buf, sizeof(buf), pipe)){
            string line(buf);
            // Strip trailing newline for processing; restore on output.
            while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            smatch m;
            if(regex_match(line, m, i6DiagRe)){
                string infFile = m[1];
                int infLine   = stoi(m[2]);
                string severity = m[3];
                string message  = m[4];
                // ERROR uppercase / warning lowercase — the visual contrast surfaces
                // severity at a glance. Fatal errors collapse to ERROR for tooling consistency.
                string sevTag;
                if(severity == "Error" || severity == "Fatal error"){ sevTag = "ERROR"; sawError = true; }
                else                                                   sevTag = "warning";
                cout << infFile << ":" << infLine << ":1: " << sevTag << ": " << message << "\n";
                string bglFile; int bglLine = 0;
                if(resolveBglSource(infLine, bglFile, bglLine))
                    cout << "  ↳ " << bglFile << ":" << bglLine << ":1\n";  // ↳
                // Unmappable .inf lines simply omit the continuation — the absence is
                // self-evident next to mapped errors that have one.
            } else {
                cout << line << "\n";
            }
        }
        int rc = pclose(pipe);
        if(rc != 0 || sawError){
            cerr << "Error running I6!" << endl;
            return true;
        }
        // -k writes gameinfo.dbg to cwd; move it alongside the other debug files
        if(settings.debugMode){
            fs::path dbgDest = fs::path(settings.tmpFile).parent_path() / (fs::path(settings.tmpFile).filename().string() + ".dbg");
            fs::rename("gameinfo.dbg", dbgDest);
        }

        // Phase 2: blorb packaging
        if(beguilerSettings.blorbEnabled) {
            fs::path outPath(settings.outFile);
            bool isGlulx = (beguilerSettings.target == "glulx");
            string ext = isGlulx ? ".gblorb" : ".zblorb";
            string blorbOut = (outPath.parent_path() / (outPath.stem().string() + ext)).string();
            Blorb::Metadata meta;
            meta.ifid           = beguilerSettings.ifid;
            meta.title          = beguilerSettings.title;
            meta.author         = beguilerSettings.author;
            meta.headline       = beguilerSettings.headline;
            meta.genre          = beguilerSettings.genre;
            meta.description    = beguilerSettings.description;
            meta.language       = beguilerSettings.language;
            meta.series         = beguilerSettings.series;
            meta.seriesNumber   = beguilerSettings.seriesNumber;
            meta.firstPublished = beguilerSettings.firstPublished;
            meta.forgiveness    = beguilerSettings.forgiveness;
            blorb.build(settings.outFile, blorbOut, blorbAssets, "", isGlulx, meta);
        }
    }

    cout<<endl;
    return false;
};

bool beguiler::parseArgs(int argc, char* argv[]) {
    if(argc==1){
        cout << "Usage: beguiler [options] <sourcefile.bgl> [<outputfile>]\n\n";
        cout << "Options:\n";
        cout << "  -o <dir>              Output directory for compiled files\n";
        cout << "  -G                    Target Glulx (default)\n";
        cout << "  -z3, -z5, -z8        Target Z-machine version 3, 5, or 8\n";
        cout << "  -E1, -E2             Error format: E1=Microsoft, E2=Macintosh\n";
        cout << "  -inform=<name>       I6 compiler binary name (use 'none' to skip I6)\n";
        cout << "  -includepaths=<dir>  Add a directory to the include search path\n";
        cout << "  --debug              Enable debug mode (emit .bgldbg debug info)\n";
        cout << "\n";
        cout << "Most options can also be set via #beguilerSettings in the source file.\n";
        cout << "The compiler should be placed in the same folder as the I6 compiler,\n";
        cout << "or use -inform= to specify the I6 binary location.\n\n";
        return true;
    }

    for(int i=1; i<argc; i++){
        string arg = argv[i];
        if(arg[0]=='-'){ //switch
            if(arg == "-o") {
                if(++i >= argc) { cerr << "Missing argument for -o." << endl; return true; }
                settings.outputPath = argv[i];
            } else if(arg.substr(1,7)=="inform="){
                settings.informName = arg.substr(8);
            } else if(arg.substr(1,13)=="includepaths="){
                beguilerSettings.includePaths.push_back(arg.substr(14));
            } else if(arg == "-G" || arg == "-g") {
                beguilerSettings.target = "glulx";
            } else if(arg == "-z3" || arg == "-Z3") {
                beguilerSettings.target = "z3";
            } else if(arg == "-z5" || arg == "-Z5") {
                beguilerSettings.target = "z5";
            } else if(arg == "-z6" || arg == "-Z6") {
                beguilerSettings.target = "z6";
            } else if(arg == "-z8" || arg == "-Z8") {
                beguilerSettings.target = "z8";
            } else if(arg.size() >= 3 && arg[1] == 'E' && isdigit(arg[2])) {
                beguilerSettings.errorFormat = arg.substr(2);
            } else if(arg == "--debug") {
                settings.debugMode = true;
            } else {
                settings.switches = settings.switches + " " + arg;
            }
        }
        else{
            if(settings.inFile=="")
                settings.inFile=arg;
            else {
                if(settings.outFile=="")
                    settings.outFile=arg;
                else{
                    cerr << "Too many file names specified." << endl;
                    return true;
                }
            }
        }
    }

    #if defined(_WIN32) || defined(_WIN64)
        settings.pathSep = '\\';
    #else
        settings.pathSep = '/';
    #endif

    settings.tmpFile = fs::absolute(settings.inFile).string()+".transpiled.inf";
    const char* envLib = getenv("BEGUILE_LIB");
    settings.libPath = envLib ? string(envLib) : getPath(argv[0])+"beguilib";

    return false;
}
string beguiler::getPath(string filename){
    string filePath="";
    size_t i = filename.rfind(settings.pathSep, filename.length());
    if (i != string::npos)
        filePath=filename.substr(0, i);
    else
        filePath=".";

    if(filePath[filePath.length()-1]!=settings.pathSep) filePath+=settings.pathSep;
    return filePath;
}

//When we're all done, commit the final transpiled text to the output file.
bool beguiler::writeFile(string filename) {
    std::ofstream outFileStream(filename);

    if (!outFileStream.is_open()) {
        std::cerr << "Error creating file "<< filename << "."<<std::endl;
        return true; // Indicate an error
    }

    emitter.emit(languageService.globals);
    outFileStream << emitter.out.str();
    outFileStream.close();
    return false;

}


