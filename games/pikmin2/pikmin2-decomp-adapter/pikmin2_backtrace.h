#pragma once

#ifdef __EMSCRIPTEN__
inline int backtrace(void**, int) { return 0; }
inline char** backtrace_symbols(void* const*, int) { return nullptr; }
inline void backtrace_symbols_fd(void* const*, int, int) {}
#else
#include <execinfo.h>
#endif
