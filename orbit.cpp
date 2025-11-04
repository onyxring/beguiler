#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <chrono>

#include "settings.h"
#include "orbit.h"
#include <cstdlib>

using namespace std;
namespace fs = std::filesystem;

settingsStruct settings;
parser bglParser;

void orbit::go(int argc, char* argv[]) {
   cout << "Beguiler: The Beguile-Inform Transpiler" << endl<<"version .1a"<<endl;
    if(parseArgs(argc, argv)) return; 
    if(bglParser.parseFile(settings.inFile)) return;
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

bool orbit::parseArgs(int argc, char* argv[]) {
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


    #if defined(_WIN32) || defined(_WIN64)
        settings.pathSep = '\\';
    #else
        settings.pathSep = '/';  
    #endif

    //get the current time in milliseconds.  And use it to create a unique temp file name.
    //auto duration = std::chrono::system_clock::now().time_since_epoch();
    //auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    //string filePath=getPath(settings.inFile);
    //settings.tmpFile = filePath+"_"+to_string(milliseconds)+".inf";

    settings.tmpFile = settings.inFile+".transpiled.inf";
    settings.informPath = getPath(argv[0])+settings.informName;

    return false;
}
string orbit::getPath(string filename){
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
bool orbit::writeFile(string filename) {
    // std::ofstream outFileStream(filename);
    // if (!outFileStream.is_open()) {
    //     std::cerr << "Error creating file "<< filename << "."<<std::endl;
    //     return true; // Indicate an error
    // }
    // outFileStream << bglParser.results.bodyText.str();
    // outFileStream.close();
    // return false;

    bglParser.emit.generateI6(bglParser.parseTree);
}

