#include <string>
using namespace std;

#include "version.h"

struct settingsStruct{
    std::string inFile;
    std::string tmpFile;
    std::string outFile;
    std::string outputPath;
    std::string switches;
    std::string informName;   // set by -inform= CLI flag; empty means not set by CLI
    std::string informPath;
    std::string libPath;   // path to the beguile language extensions folder (beguilib), alongside the compiler binary
    bool   libPathFromCommandLine = false;   // -lib= named it, so a beguiLibPath setting doesn't replace it
    char   pathSep;
    bool   debugMode = false;  // set by --debug: writes the `<source>.bgldbg` debug bundle, and runs
                               // I6 with -k, whose gameinfo.dbg is moved to
                               // `<source>.transpiled.inf.dbg` beside it (docs/spec 20.1)
};
extern settingsStruct settings;

