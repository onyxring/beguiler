#include <string>
#include "parser.h"

class orbit {
    public:
        void go(int, char*[]);                      //entry point.  Main is just a shell which passes its args to this class member
    private:
        bool parseArgs(int, char*[]);               //parse command line args and place them into the settings struct
        bool parse(std::string);                    //parse the file
        std::string getPath(std::string filename);
        bool writeFile(std::string);                //write the output file
};

extern parser beguiler;