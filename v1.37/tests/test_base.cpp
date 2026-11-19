// FATAL, DCHECK, the allocation hook, and the harness itself.
#include <cstdlib>
#include <new>
#include <string>
#include <cstdio>

#include "base/alloc_guard.h"
#include "base/fatal.h"
#include "harness.h"

namespace {
void* volatile g_sink;  // so the compiler can't elide an allocation
}

// ---- FATAL and DCHECK
DEATH_TEST(fatal_prints_context_and_aborts, "^FATAL .*test_base\\.cpp:[0-9]+: widget 7 broke: twice\n") {
    FATAL("widget %d broke: %s", 7, "twice");
}
DEATH_TEST(fatal_without_arguments, "FATAL .*: plain message") { FATAL("plain message"); }
DEBUG_DEATH_TEST(dcheck_fires_in_debug, "check failed: 1 \\+ 1 == 3: arith 42") { DCHECK(1 + 1 == 3, "arith %d", 42); }
TEST(dcheck_is_silent_when_true) { DCHECK(1 + 1 == 2, "never"); }
TEST(dcheck_not_evaluated_in_release) {
    int evaluated = 0;
    if (!sim::kDebug) DCHECK((++evaluated, false), "release must not evaluate this");
    CHECK_EQ(evaluated, 0);
}

// ---- the allocation hook: outside a run anything goes
TEST(hook_quiet_outside_a_run) {
    CHECK(!sim::in_run());
    g_sink = std::malloc(16);
    std::free(g_sink);
    g_sink = new int(3);
    delete static_cast<int*>(g_sink);
    sim::begin_run();
    CHECK(sim::in_run());
    sim::end_run();
    CHECK(!sim::in_run());
    g_sink = new char[100];
    delete[] static_cast<char*>(g_sink);
}

// ---- inside a run, every form is fatal
DEATH_TEST(hook_operator_new, "operator new of 4 bytes inside a run") {
    sim::begin_run();
    g_sink = new int(1);
}
DEATH_TEST(hook_operator_new_array, "operator new\\[\\] of 40 bytes inside a run") {
    sim::begin_run();
    g_sink = new int[10];
}
DEATH_TEST(hook_operator_new_nothrow, "operator new of 8 bytes inside a run") {
    sim::begin_run();
    g_sink = new (std::nothrow) long(1);
}
DEATH_TEST(hook_operator_new_aligned, "operator new of 128 bytes inside a run") {
    struct alignas(128) Big {
        char c[128];
    };
    sim::begin_run();
    g_sink = new Big;
}
DEATH_TEST(hook_operator_delete, "operator delete of 0 bytes inside a run") {
    g_sink = new int(1);
    sim::begin_run();
    delete static_cast<int*>(g_sink);
}
DEATH_TEST(hook_malloc, "malloc of 24 bytes inside a run") {
    sim::begin_run();
    g_sink = std::malloc(24);
}
DEATH_TEST(hook_calloc, "calloc of 48 bytes inside a run") {
    sim::begin_run();
    g_sink = std::calloc(6, 8);
}
DEATH_TEST(hook_realloc, "realloc of 64 bytes inside a run") {
    void* p = std::malloc(8);
    sim::begin_run();
    g_sink = std::realloc(p, 64);
}
DEATH_TEST(hook_free, "free of 0 bytes inside a run") {
    g_sink = std::malloc(8);
    sim::begin_run();
    std::free(g_sink);  // through the volatile sink: a malloc/free pair the compiler can see is elided
}
DEATH_TEST(hook_aligned_alloc, "aligned_alloc of 64 bytes inside a run") {
    sim::begin_run();
    g_sink = aligned_alloc(64, 64);
}
DEATH_TEST(hook_posix_memalign, "posix_memalign of 256 bytes inside a run") {
    void* p = nullptr;
    sim::begin_run();
    if (posix_memalign(&p, 64, 256) != 0) p = nullptr;
    g_sink = p;
}
DEATH_TEST(hook_stl_container, "operator new of [0-9]+ bytes inside a run") {
    sim::begin_run();
    std::string s(1000, 'x');  // the STL in the cycle loop is caught too
    g_sink = s.data();
}
DEATH_TEST(hook_per_thread_flag_is_cleared_by_fatal, "first\n.*$") {
    // FATAL clears the flag before reporting, so nothing in the report itself trips the hook
    sim::begin_run();
    FATAL("first");
}
DEBUG_DEATH_TEST(begin_run_twice, "begin_run inside a run") {
    sim::begin_run();
    sim::begin_run();
}
DEBUG_DEATH_TEST(end_run_outside, "end_run outside a run") { sim::end_run(); }

// ---- the harness itself: a death test's pattern is matched against the child's whole stderr
DEATH_TEST(harness_captures_earlier_stderr, "before\n.*FATAL .*after") {
    std::fprintf(stderr, "before\n");
    FATAL("after");
}
