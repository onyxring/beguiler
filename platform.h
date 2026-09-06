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
