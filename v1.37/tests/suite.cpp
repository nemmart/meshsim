// suite.cpp: Java's T in C++ (suite.h).
#include "suite.h"

#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace suite {

long passed = 0, failed = 0;
static std::string current;

namespace detail {
std::string vstr(const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    std::string s(n > 0 ? size_t(n) : 0, '\0');
    if (n > 0) std::vsnprintf(s.data(), size_t(n) + 1, fmt, ap);
    return s;
}
}  // namespace detail

std::string str(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string s = detail::vstr(fmt, ap);
    va_end(ap);
    return s;
}

static void fail_line(const std::string& what) {
    failed++;
    std::printf("FAIL [%s] %s\n", current.c_str(), what.c_str());
}

void group(const std::string& name) { current = name; }
void pending(const std::string& what, const std::string& until) { std::printf("PENDING (%s): %s\n", until.c_str(), what.c_str()); }

bool check(bool ok, const char* fmt, ...) {
    if (ok) { passed++; return true; }
    va_list ap;
    va_start(ap, fmt);
    std::string what = detail::vstr(fmt, ap);
    va_end(ap);
    fail_line(what);
    return false;
}

bool eq(int64_t expect, int64_t got, const char* fmt, ...) {
    if (expect == got) { passed++; return true; }
    va_list ap;
    va_start(ap, fmt);
    std::string what = detail::vstr(fmt, ap);
    va_end(ap);
    fail_line(what + ": expected " + std::to_string(expect) + " got " + std::to_string(got));
    return false;
}

static bool eqo_v(bool same, const std::string& e, const std::string& g, const char* fmt, va_list ap) {
    if (same) { passed++; return true; }
    fail_line(detail::vstr(fmt, ap) + ": expected " + e + " got " + g);
    return false;
}

bool eqo(const std::string& expect, const std::string& got, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const bool r = eqo_v(expect == got, expect, got, fmt, ap);
    va_end(ap);
    return r;
}

bool eqo(const char* expect, const char* got, const char* fmt, ...) {
    const bool same = (!expect || !got) ? expect == got : std::strcmp(expect, got) == 0;
    va_list ap;
    va_start(ap, fmt);
    const bool r = eqo_v(same, expect ? expect : "null", got ? got : "null", fmt, ap);
    va_end(ap);
    return r;
}

// ---- the stops ------------------------------------------------------------------------------------------------------
namespace {
struct Child {
    bool fatal = false;        // died of SIGABRT with a FATAL line on stderr
    bool returned = false;     // fn returned (exit status 0)
    std::string message;       // the FATAL's message, without "FATAL file:line: " and the newline
    std::string other;         // how it died otherwise
};

Child run_child(detail::Thunk t, void* fn) {
    Child r;
    std::fflush(stdout);
    std::fflush(stderr);
    int fd[2];
    if (pipe(fd) != 0) { r.other = "pipe failed"; return r; }
    const pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); r.other = "fork failed"; return r; }
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 2);
        close(fd[1]);
        const int null = open("/dev/null", O_WRONLY);   // the child's stdout is dropped (its lines aren't the suite's)
        if (null >= 0) { std::fflush(stdout); dup2(null, 1); close(null); }
        t(fn);
        std::fflush(stderr);
        _exit(0);                                // no leak check, no atexit: the child only reports how it ended
    }
    close(fd[1]);
    std::string err;
    char buf[4096];
    for (;;) {
        const ssize_t n = read(fd[0], buf, sizeof buf);
        if (n > 0) { err.append(buf, size_t(n)); continue; }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    close(fd[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) { r.returned = true; return r; }
    const size_t at = err.find("FATAL ");
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT && at != std::string::npos) {
        // "FATAL file:line: message\n": the message starts after the first ": " following "FATAL "
        const size_t colon = err.find(": ", at);
        size_t end = err.find('\n', at);
        if (end == std::string::npos) end = err.size();
        r.fatal = true;
        r.message = colon != std::string::npos && colon < end ? err.substr(colon + 2, end - colon - 2) : err.substr(at, end - at);
        return r;
    }
    if (WIFSIGNALED(st)) r.other = str("died of signal %d without a FATAL", WTERMSIG(st));
    else r.other = str("exited with status %d without a FATAL", WEXITSTATUS(st));
    const size_t nl = err.find('\n');
    if (!err.empty()) r.other += ": " + err.substr(0, nl == std::string::npos ? err.size() : nl);
    return r;
}
}  // namespace

namespace detail {
bool throws(Thunk t, void* fn, const std::string& what) {
    const Child c = run_child(t, fn);
    if (c.fatal) { passed++; return true; }
    fail_line(what + (c.returned ? ": no exception" : ": " + c.other));
    return false;
}

std::string ub(Thunk t, void* fn, std::initializer_list<const char*> names, const std::string& what) {
    const Child c = run_child(t, fn);
    if (c.returned) { fail_line(what + ": ran on, no undefined-behavior stop"); return ""; }
    if (!c.fatal) { fail_line(what + ": " + c.other); return ""; }
    for (const char* n : names)
        if (c.message.find(n) == std::string::npos) {
            fail_line(what + ": the stop doesn't name '" + n + "': " + c.message);
            return c.message;
        }
    passed++;
    return c.message;
}
}  // namespace detail

int done(const std::string& suite) {
    std::printf("%s: %ld passed, %ld failed\n", suite.c_str(), passed, failed);
    std::fflush(stdout);
    return failed == 0 ? 0 : 1;
}

void reset() { passed = failed = 0; current.clear(); }

std::string jlist(const int64_t* v, size_t n) {
    std::string s = "[";
    for (size_t i = 0; i < n; i++) { if (i) s += ", "; s += std::to_string(v[i]); }
    return s + "]";
}

}  // namespace suite
