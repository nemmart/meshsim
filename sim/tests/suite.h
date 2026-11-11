// suite.h (WORKER20, P2, row C1): v1.3's suite harness `T` (sim/ref/meshsim/test/T.java) in C++, for the suites ported
// from Java. Every line it prints, and every count, is Java's T's, so a ported suite's log can equal v1.3's
// (`run_tests.sh`'s results/<step>.log; V135-INVENTORY's "log-equal").
//
//   Java                                  C++
//   T.group(name)                         suite::group(name)
//   T.pending(what, until)                suite::pending(what, until)      "PENDING (<until>): <what>"
//   T.check(ok, what)                     suite::check(ok, fmt, ...)       "FAIL [<group>] <what>"
//   T.eq(expect, got, what)               suite::eq(expect, got, fmt, ...) "...<what>: expected <e> got <g>"
//   T.eqo(expect, got, what)              suite::eqo(expect, got, fmt, ...) (strings; a null const char* prints "null")
//   T.throwsISE(r, what)                  suite::throws(fn, fmt, ...)      "...<what>: no exception"
//   T.ub(r, what, names...)               suite::ub(fn, {names}, fmt, ...)  "...: ran on, no undefined-behavior stop"
//   T.done(suite)                         return suite::done(name)         "<suite>: <p> passed, <f> failed"; 0 or 1
//
// `what` is a printf format and its arguments, formatted only when a line is printed (Java builds the string eagerly;
// the output is the same). eq's values are Java's longs: int64_t, printed signed (an unsigned 64-bit value above 2^63
// prints negative, as in Java). Everything goes to stdout, as System.out.
//
// THE STOPS. Java throws (IllegalStateException for misuse, UndefinedBehavior for UB) and T catches; meshsim v1.35 has
// no exceptions (D4): misuse and UB are FATAL (src/base/fatal.h). So `throws` and `ub` run `fn` in a forked child,
// stderr captured: it passes when the child dies by a FATAL ("FATAL file:line: <message>" on stderr), and for `ub` the
// message names every one of `names`. A child that dies any other way (a sanitizer, a signal) fails with that reason,
// never counts as the stop. A child's changes are lost: `fn` runs on a copy of the process (build what it needs inside,
// or use state built before; a machine split over threads can't be stepped in the child, since fork copies one thread).
// `ub` returns the FATAL's message (without the "FATAL file:line: " prefix), "" if there was none.
//
// HELPERS. suite::str(fmt, ...) is snprintf into a std::string; suite::jlist(values, n) is Java's Arrays.toString of a
// long[] ("[0, 2, 3]"; "[]" when empty), for eqo and printed lines.
// Java's own formatting of doubles (`"" + d`, and String.format's HALF_UP rounding) differs from printf's; a helper comes
// with the first suite that prints one.
//
// This is test-side code (the configuration regime, D6): it may allocate. Not for the cycle loop.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>

namespace suite {

extern long passed, failed;    // T.passed, T.failed

void group(const std::string& name);
void pending(const std::string& what, const std::string& until);

bool check(bool ok, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
bool eq(int64_t expect, int64_t got, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
bool eqo(const std::string& expect, const std::string& got, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
bool eqo(const char* expect, const char* got, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// The stops, run in a forked child (above). `fn` is called as fn().
namespace detail {
using Thunk = void (*)(void*);
bool throws(Thunk t, void* fn, const std::string& what);
std::string ub(Thunk t, void* fn, std::initializer_list<const char*> names, const std::string& what);
std::string vstr(const char* fmt, __builtin_va_list ap);
template <class F> void call(void* f) { (*static_cast<F*>(f))(); }
}  // namespace detail

template <class F>
bool throws(F&& fn, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
template <class F>
bool throws(F&& fn, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    std::string what = detail::vstr(fmt, ap);
    __builtin_va_end(ap);
    using G = std::remove_reference_t<F>;
    return detail::throws(&detail::call<G>, (void*)&fn, what);
}

template <class F>
std::string ub(F&& fn, std::initializer_list<const char*> names, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
template <class F>
std::string ub(F&& fn, std::initializer_list<const char*> names, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    std::string what = detail::vstr(fmt, ap);
    __builtin_va_end(ap);
    using G = std::remove_reference_t<F>;
    return detail::ub(&detail::call<G>, (void*)&fn, names, what);
}

// "<suite>: <passed> passed, <failed> failed"; returns 0 if nothing failed, else 1 (main returns it, as System.exit).
int done(const std::string& suite);

// Back to the start (a fresh process's counts and group): for tests of this harness only.
void reset();

std::string str(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string jlist(const int64_t* v, size_t n);

}  // namespace suite
