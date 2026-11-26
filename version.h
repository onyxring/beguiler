#pragma once
#include <string>

// The compiler's version, read from the release tag this source was built from.
struct beguilerVersion {
    int major = 0, minor = 0, patch = 0;
    std::string display;                                              // "0.1.0-beta", "0.1.0-beta+6 (b9a54ca)"
    int number() const { return major * 1000 + minor * 10 + patch; }  // 1.1.0 = 1010
};

// Parses the output of `git describe --tags --dirty`; false when the text is not one.
bool parseVersionDescription(const std::string& described, beguilerVersion& out);

const beguilerVersion& compilerVersion();
