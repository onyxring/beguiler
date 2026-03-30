#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <cstdlib>

#include "settings.h"
#include "beguiler.h"
#include "helpers.h"
#include "bglParser.h"
#include "bglLanguageService.h"
#include "blorb.h"

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

        pos = cur;
    }
}

bool beguiler::go(int argc, char* argv[]) {

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
        cout<<(settings.informPath+debugSwitch+" "+settings.switches+" "+settings.tmpFile+" "+settings.outFile).c_str()<<endl<<endl;
        if(system((settings.informPath+debugSwitch+" "+settings.switches+" "+settings.tmpFile+" "+settings.outFile).c_str())){
            cerr<<"Error running I6!"<<endl;
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
            blorb.build(settings.outFile, blorbOut, blorbAssets,
                        "", beguilerSettings.author, isGlulx);
        }
    }

    cout<<endl;
    return false;
};

bool beguiler::parseArgs(int argc, char* argv[]) {
    if(argc==1){
        cout << "   Usage: beguiler [--debug] [-G|-z3|-z5|-z6|-z8] [-E1|-E2] [-inform=none|<execname>] [-o <outputdir>] <sourcefilepath> [<outputfilepath>]" << endl;
        cout << "   This program should be placed in the same folder as the I6 compiler."<<endl;
        cout << "   The Beguile source  will be processed and the resulting I6 source passed to Inform to generate the final game.\n" << endl;
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
            } else if(arg.substr(1,10)=="i6include="){
                beguilerSettings.i6IncludePaths.push_back(arg.substr(11));
            } else if(arg == "-G" || arg == "-g") {
                beguilerSettings.target = "glulx";
            } else if(arg == "-z3" || arg == "-z3") {
                beguilerSettings.target = "z3";
            } else if(arg == "-z5" || arg == "-z5") {
                beguilerSettings.target = "z5";
            } else if(arg == "-z6" || arg == "-z6") {
                beguilerSettings.target = "z6";
            } else if(arg == "-z8" || arg == "-z8") {
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
    emitter.writeSourceMap(filename + ".map");
    return false;

}


