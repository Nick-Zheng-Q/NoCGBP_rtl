// nocbp_verilator/common/debug.h
// Central debug output control for C++ testbenches.
//
// Usage:
//   #include "debug.h"
//   NOCBP_DBG("value=%d\n", 42);
//
// Debug output is disabled by default. Enable by defining NOCBP_DEBUG to a
// non-zero value before including this header, or by passing
// -DNOCBP_DEBUG=1 to the compiler (Verilator / g++).

#ifndef NOCBP_DEBUG_H
#define NOCBP_DEBUG_H

#include <cstdio>

#ifndef NOCBP_DEBUG
#define NOCBP_DEBUG 0
#endif

#define NOCBP_DBG(...)                                              \
  do {                                                              \
    if (NOCBP_DEBUG) ::std::fprintf(stderr, __VA_ARGS__);           \
  } while (0)

#endif // NOCBP_DEBUG_H
