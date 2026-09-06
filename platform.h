#pragma once
// Platform shims, so one build command line works everywhere.
//
// These used to be supplied as -D defines on the build line, which is why the README
// carried a different command per platform. They belong here: the divergence is a
// property of the source, not of how you invoke the compiler.
//
//   isnumber      BSD extension (macOS). Replaced at its one call site with isdigit.
//   strncasecmp   POSIX. The MS CRT spells it _strnicmp.
//   popen/pclose  POSIX. The MS CRT spells them _popen/_pclose.

// Standard headers that libc++ happens to pull in transitively but libstdc++ does not.
// Relying on that difference is how a tree builds on macOS and fails on Linux with
// "'uint32_t' was not declared". Included here so every translation unit gets them.
#include <cstdint>     // uint32_t, int64_t
#include <cstdlib>     // exit, getenv
#include <cstring>     // strlen, memcpy
#include <cctype>      // isdigit, isalpha
#include <stdexcept>   // runtime_error
#include <numeric>     // accumulate
#include <string>

#ifdef _WIN32
  #include <cstdio>
  #include <cstring>
  #ifndef strncasecmp
    #define strncasecmp _strnicmp
  #endif
  #ifndef popen
    #define popen  _popen
    #define pclose _pclose
  #endif
#else
  #include <cstdio>
  #include <strings.h>   // strncasecmp
#endif
