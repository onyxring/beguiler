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
    if(parser.parseFile(settings.inFile)) return;

    // Warn if no beguilerSettings block was declared
    bool hasSettings = false;
    for(typeDef* g : languageService.globals)
        if(typeid(*g) == typeid(beguilerSettingsDef)) { hasSettings = true; break; }
    if(!hasSettings)
        cout << "warning: no beguilerSettings declared; defaulting target to Glulx.\n";

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
        if(argv[i][0]=='-'){ //switch
            if(string(argv[i]).substr(1,7)=="inform="){
                settings.informName=argv[i]+8;
                continue;
            }
            settings.switches=settings.switches+" "+argv[i];
        }
        else{
            if(settings.inFile=="")
                settings.inFile=argv[i];
            else {
                if(settings.outFile=="")
                    settings.outFile=argv[i];
                else{
                    cerr << "Too many file names specified." << endl;
                    return true;
                }
            }
        }
    }

    if(settings.outFile=="") {
        string extension="ulx";

        fs::path p(settings.inFile);

        if(settings.switches.contains("-v3")) extension="z3";
        if(settings.switches.contains("-v5")) extension="z5";
        if(settings.switches.contains("-v6")) extension="z6";
        if(settings.switches.contains("-v8")) extension="z8";

        settings.outFile=format("{0}.{1}",p.stem().c_str(),extension);
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
    stringstream bodyText;
    std::ofstream outFileStream(filename);

    if (!outFileStream.is_open()) {
        std::cerr << "Error creating file "<< filename << "."<<std::endl;
        return true; // Indicate an error
    }

    emitter.to(bodyText);
    emitter.emit(languageService.globals);
    outFileStream << bodyText.str();
    outFileStream.close();
    return false;

}


