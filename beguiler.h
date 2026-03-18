#include <string>

using namespace std;

class beguiler {
    public:
        bool go(int, char*[]);                      //entry point.  Returns true on error (non-zero exit code).
    private:
        bool parseArgs(int, char*[]);               //parse command line args and place them into the global settings struct
        std::string getPath(std::string filename);  //determine the path of a given filename
        bool writeFile(std::string);                //write the output file when successfully compiled
};
