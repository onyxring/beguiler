#include <string>
using namespace std;

struct settingsStruct{
    std::string inFile;
    std::string tmpFile;
    std::string outFile;
    std::string switches;
    std::string informName="inform";
    std::string informPath;
    char   pathSep;
};
extern settingsStruct settings;

