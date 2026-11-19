#include "base/alloc_guard.h"

#include <cstddef>
#include <cstdlib>
#include <new>

#include "base/fatal.h"

namespace sim {
namespace {
thread_local bool g_in_run = false;  // constant-initialised: reading it never allocates
}

void begin_run() {
    DCHECK(!g_in_run, "begin_run inside a run");
    g_in_run = true;
}
void end_run() {
    DCHECK(g_in_run, "end_run outside a run");
    g_in_run = false;
}
bool in_run() { return g_in_run; }
void end_run_for_fatal() { g_in_run = false; }

}  // namespace sim

// ---- the C allocator, for our own object files (-Wl,--wrap=...)
extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
void* __real_aligned_alloc(size_t, size_t);
int __real_posix_memalign(void**, size_t, size_t);

#define SIM_GUARD(what, n)                                                                       \
    if (__builtin_expect(sim::in_run(), 0)) FATAL("%s of %zu bytes inside a run", what, (size_t)(n))

void* __wrap_malloc(size_t n) {
    SIM_GUARD("malloc", n);
    return __real_malloc(n);
}
void* __wrap_calloc(size_t k, size_t n) {
    SIM_GUARD("calloc", k * n);
    return __real_calloc(k, n);
}
void* __wrap_realloc(void* p, size_t n) {
    SIM_GUARD("realloc", n);
    return __real_realloc(p, n);
}
void __wrap_free(void* p) {
    if (p) SIM_GUARD("free", 0);
    __real_free(p);
}
void* __wrap_aligned_alloc(size_t a, size_t n) {
    SIM_GUARD("aligned_alloc", n);
    return __real_aligned_alloc(a, n);
}
int __wrap_posix_memalign(void** p, size_t a, size_t n) {
    SIM_GUARD("posix_memalign", n);
    return __real_posix_memalign(p, a, n);
}
}

// ---- operator new and delete, every form
namespace {
void* guarded_new(size_t n, const char* what) {
    SIM_GUARD(what, n);
    void* p = __real_malloc(n ? n : 1);
    if (!p) FATAL("%s of %zu bytes: out of memory", what, n);
    return p;
}
void* guarded_new_aligned(size_t n, std::align_val_t a, const char* what) {
    SIM_GUARD(what, n);
    size_t al = static_cast<size_t>(a);
    if (al < sizeof(void*)) al = sizeof(void*);
    void* p = nullptr;
    if (__real_posix_memalign(&p, al, n ? n : 1) != 0) FATAL("%s of %zu bytes: out of memory", what, n);
    return p;
}
void guarded_delete(void* p, const char* what) {
    if (!p) return;
    SIM_GUARD(what, 0);
    __real_free(p);
}
}  // namespace

void* operator new(size_t n) { return guarded_new(n, "operator new"); }
void* operator new[](size_t n) { return guarded_new(n, "operator new[]"); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return guarded_new(n, "operator new"); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return guarded_new(n, "operator new[]"); }
void* operator new(size_t n, std::align_val_t a) { return guarded_new_aligned(n, a, "operator new"); }
void* operator new[](size_t n, std::align_val_t a) { return guarded_new_aligned(n, a, "operator new[]"); }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return guarded_new_aligned(n, a, "operator new");
}
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return guarded_new_aligned(n, a, "operator new[]");
}
void operator delete(void* p) noexcept { guarded_delete(p, "operator delete"); }
void operator delete[](void* p) noexcept { guarded_delete(p, "operator delete[]"); }
void operator delete(void* p, size_t) noexcept { guarded_delete(p, "operator delete"); }
void operator delete[](void* p, size_t) noexcept { guarded_delete(p, "operator delete[]"); }
void operator delete(void* p, const std::nothrow_t&) noexcept { guarded_delete(p, "operator delete"); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { guarded_delete(p, "operator delete[]"); }
void operator delete(void* p, std::align_val_t) noexcept { guarded_delete(p, "operator delete"); }
void operator delete[](void* p, std::align_val_t) noexcept { guarded_delete(p, "operator delete[]"); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { guarded_delete(p, "operator delete"); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { guarded_delete(p, "operator delete[]"); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { guarded_delete(p, "operator delete"); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    guarded_delete(p, "operator delete[]");
}
