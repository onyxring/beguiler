#include <string>
using namespace std;

struct settingsStruct{
    std::string inFile;
    std::string tmpFile;
    std::string outFile;
    std::string switches;
    std::string informName="inform";
    std::string informPath;
    std::string libPath;   // path to the beguile standard library folder (beguilib), alongside the compiler binary
    char   pathSep;
};
extern settingsStruct settings;

