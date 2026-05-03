#include <string>

using namespace std;

class beguiler {
    public:
        bool go(int, char*[]);                      //entry point.  Returns true on error (non-zero exit code).
    private:
        bool parseArgs(int, char*[]);               //parse command line args and place them into the global settings struct
        std::string getPath(std::string filename);  //determine the path of a given filename
        bool writeFile(std::string);                //write the output file when successfully compiled
        void extractBlorbSettings(const std::string& filename); // pre-scan source for blorb settings before full parse
        // .inf-mode pre-pass: read the user's `!%` ICL header and set the Beguile target
        // (plus target_zcode/target_glulx compile-time symbols) so BLR's TARGET_ZCODE
        // conditional include picks the right branch. Recognized flags: `-G` → glulx,
        // `-z` → z-target (default version 5), `-vN` (N=3/5/8) → zN. No-op for non-.inf.
        // Must run before parser.preScanFile loads BLR.
        void extractInfTargetFromIcl(const std::string& filename);
};
