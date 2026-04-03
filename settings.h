#include <string>
using namespace std;

// Beguile compiler version — single source of truth for both the preprocessor
// symbol (#if beguilerVersion) and the emitted I6 Constant.
// Encoding: major*1000 + minor*10 + patch  (1.0.0 = 1000, 1.1.0 = 1010)
constexpr int BEGUILER_VERSION = 1000;

struct settingsStruct{
    std::string inFile;
    std::string tmpFile;
    std::string outFile;
    std::string outputPath;
    std::string switches;
    std::string informName;   // set by -inform= CLI flag; empty means not set by CLI
    std::string informPath;
    std::string libPath;   // path to the beguile language extensions folder (beguilib), alongside the compiler binary
    char   pathSep;
    bool   debugMode = false;  // set by --debug flag; passes -d to I6 and emits .inf.sym
};
extern settingsStruct settings;

