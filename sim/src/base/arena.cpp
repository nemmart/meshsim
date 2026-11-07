#include "base/arena.h"

#include <sys/mman.h>

#include <cstdlib>
#include <cstring>

namespace sim {

namespace {
constexpr size_t kHuge = size_t(2) << 20;
}

void* alloc_big(size_t bytes, const char* name) {
    const size_t align = bytes >= kHuge ? kHuge : kLineBytes;
    const size_t size = (bytes + align - 1) / align * align;
    void* p = nullptr;
    if (size == 0 || posix_memalign(&p, align, size) != 0 || p == nullptr)
        FATAL("alloc_big '%s': %zu bytes failed", name, bytes);
    if (align == kHuge) (void)madvise(p, size, MADV_HUGEPAGE);  // advice only: fine if the kernel says no
    std::memset(p, 0, size);
    return p;
}

void free_big(void* p) { std::free(p); }

}  // namespace sim
