#include "base/fatal.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "base/alloc_guard.h"

namespace sim {

void fatal_at(const char* file, int line, const char* fmt, ...) {
    end_run_for_fatal();  // whatever reporting does, the allocation hook must not fire on it
    std::fprintf(stderr, "FATAL %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::abort();
}

}  // namespace sim
