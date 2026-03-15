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

using namespace std;
namespace fs = std::filesystem;

settingsStruct settings;

void beguiler::go(int argc, char* argv[]) {

   cout << "Beguiler: The Beguile-Inform Transpiler" << endl<<"version .1a"<<endl;
    if(parseArgs(argc, argv)) return;
    parser.preScanFile(settings.inFile);  // pass 1: register all type/object stubs for forward-reference resolution
    if(parser.parseFile(settings.inFile)) return;

    // Default target to Glulx if neither CLI nor #beguilerSettings set it
    if(beguilerSettings.target.empty()) beguilerSettings.target = "glulx";

    if(writeFile(settings.tmpFile)) return;

    cout<<endl<<"Transpile successful. ";
    if(settings.informName=="none"){
        cout<<"Skipping I6 handoff."<<endl;
        return;
    }else{
        cout<<"Handing off to I6..."<<endl;
        cout<<(settings.informPath+" "+settings.switches+" "+settings.tmpFile+" "+settings.outFile).c_str()<<endl<<endl;
        if(system((settings.informPath+" "+settings.switches+" "+settings.tmpFile+" "+settings.outFile).c_str())){
            cerr<<"Error running I6!"<<endl;
            return;
        }
    }

    cout<<endl;
};

bool beguiler::parseArgs(int argc, char* argv[]) {
    if(argc==1){
        cout << "   Usage: beguile [Inform6 switches] [-inform=none|<execname>] <sourcefilepath> [<outputfilepath>]" << endl;
        cout << "   This program should be placed in the same folder as the I6 compiler."<<endl;
        cout << "   The sourcefile will be processed and the resulting source passed to Inform to generate the final game.\n" << endl;
        return true;
    }

    for(int i=1; i<argc; i++){
        string arg = argv[i];
        if(arg[0]=='-'){ //switch
            if(arg.substr(1,7)=="inform="){
                settings.informName = arg.substr(8);
            } else if(arg == "-G" || arg == "-g") {
                beguilerSettings.target = "glulx";
            } else if(arg == "-v3" || arg == "-z3") {
                beguilerSettings.target = "z3";
            } else if(arg == "-v5" || arg == "-z5") {
                beguilerSettings.target = "z5";
            } else if(arg == "-v6" || arg == "-z6") {
                beguilerSettings.target = "z6";
            } else if(arg == "-v8" || arg == "-z8") {
                beguilerSettings.target = "z8";
            } else if(arg.size() >= 3 && arg[1] == 'E' && isdigit(arg[2])) {
                beguilerSettings.errorFormat = arg.substr(2);
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

    if(settings.outFile=="") {
        string extension = "ulx";
        fs::path p(settings.inFile);
        const string& t = beguilerSettings.target;
        if(t == "z3") extension = "z3";
        else if(t == "z5") extension = "z5";
        else if(t == "z6") extension = "z6";
        else if(t == "z8") extension = "z8";
        settings.outFile = format("{0}.{1}", p.stem().c_str(), extension);
    }

    #if defined(_WIN32) || defined(_WIN64)
        settings.pathSep = '\\';
    #else
        settings.pathSep = '/';
    #endif

    settings.tmpFile = fs::absolute(settings.inFile).string()+".transpiled.inf";
    settings.informPath = getPath(argv[0])+settings.informName;
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


