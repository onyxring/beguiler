#include <string>
using namespace std;

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
};
extern settingsStruct settings;

