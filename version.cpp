#include "version.h"
#include "gitArchive.h"
#include <cstdlib>
#include <regex>
#if __has_include("build/gitDescribe.h")
    #include "build/gitDescribe.h"
#endif
using namespace std;

// The release a build falls back to when it comes from neither a clone nor an exported archive.
static const int fallbackMajor = 0, fallbackMinor = 1, fallbackPatch = 0;

bool parseVersionDescription(const string& described, beguilerVersion& out){
    static const regex shape(R"(^v?(\d+)\.(\d+)\.(\d+)(-.+?)?(?:-(\d+)-g([0-9a-f]+))?(-dirty)?$)");
    smatch m;
    if(!regex_match(described, m, shape)) return false;
    out.major = stoi(m[1]); out.minor = stoi(m[2]); out.patch = stoi(m[3]);
    out.display = m[1].str() + "." + m[2].str() + "." + m[3].str() + m[4].str();
    if(m[5].matched) out.display += "+" + m[5].str() + " (" + m[6].str() + ")";
    if(m[7].matched) out.display += ", modified";
    return true;
}

const beguilerVersion& compilerVersion(){
    static const beguilerVersion version = []{
        beguilerVersion v;
        // A fixed version for the regression baselines, which hold the version in their output.
        if(const char* fixed = getenv("BEGUILER_VERSION"); fixed && parseVersionDescription(fixed, v)) return v;
        #ifdef BEGUILER_GIT_DESCRIBE
            if(parseVersionDescription(BEGUILER_GIT_DESCRIBE, v)) return v;
        #endif
        if(parseVersionDescription(BEGUILER_ARCHIVE_DESCRIBE, v)) return v;
        v.major = fallbackMajor; v.minor = fallbackMinor; v.patch = fallbackPatch;
        v.display = to_string(v.major) + "." + to_string(v.minor) + "." + to_string(v.patch) + " (unidentified build)";
        return v;
    }();
    return version;
}
