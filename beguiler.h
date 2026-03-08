#include <string>

using namespace std;

class beguiler {
    public:
        void go(int, char*[]);                      //entry point.  Main is just a shell which passes its args to this class member
    private:
        bool parseArgs(int, char*[]);               //parse command line args and place them into the global settings struct
        std::string getPath(std::string filename);  //determine the path of a given filename
        bool writeFile(std::string);                //write the output file when successfully compiled
};
