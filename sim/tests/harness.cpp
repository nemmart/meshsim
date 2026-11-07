#include "harness.h"

#include <regex.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "base/fatal.h"

namespace test {
namespace {

struct Case {
    const char* name;
    Fn fn;
    const char* pattern;
    bool debug_only;
};
Case g_cases[512];
int g_ncases = 0;
bool g_failed = false;

bool run_death(const Case& c, std::string& out) {
    int fd[2];
    if (pipe(fd) != 0) {
        out = "pipe failed";
        return false;
    }
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid < 0) {
        out = "fork failed";
        return false;
    }
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 2);
        close(fd[1]);
        c.fn();
        std::fprintf(stderr, "(the test returned normally)\n");
        std::fflush(stderr);
        _exit(0);
    }
    close(fd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(fd[0], buf, sizeof buf)) > 0) out.append(buf, size_t(n));
    close(fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT) {
        out += WIFSIGNALED(status) ? "(died of signal " + std::to_string(WTERMSIG(status)) + ", not SIGABRT)\n"
                                   : "(exited with " + std::to_string(WEXITSTATUS(status)) + ", did not die)\n";
        return false;
    }
    regex_t re;
    if (regcomp(&re, c.pattern, REG_EXTENDED | REG_NOSUB) != 0) {
        out += std::string("(bad pattern: ") + c.pattern + ")\n";
        return false;
    }
    const bool match = regexec(&re, out.c_str(), 0, nullptr, 0) == 0;
    regfree(&re);
    if (!match) out += std::string("(stderr does not match /") + c.pattern + "/)\n";
    return match;
}

}  // namespace

Registrar::Registrar(const char* name, Fn fn, const char* pattern, bool debug_only) {
    if (g_ncases == int(sizeof g_cases / sizeof g_cases[0])) {
        std::fprintf(stderr, "harness: too many tests\n");
        std::abort();
    }
    g_cases[g_ncases++] = Case{name, fn, pattern, debug_only};
}

void fail(const char* file, int line, const char* fmt, ...) {
    g_failed = true;
    std::fprintf(stderr, "  %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

}  // namespace test

int main(int argc, char** argv) {
    using namespace test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0, skipped = 0;
    for (int i = 0; i < g_ncases; ++i) {
        const Case& c = g_cases[i];
        if (filter && !std::strstr(c.name, filter)) continue;
        if (c.debug_only && !sim::kDebug) {
            ++skipped;
            continue;
        }
        bool ok;
        if (c.pattern) {
            std::string out;
            ok = run_death(c, out);
            if (!ok) std::fprintf(stderr, "  child's stderr:\n%s", out.c_str());
        } else {
            g_failed = false;
            c.fn();
            ok = !g_failed;
        }
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
        (ok ? passed : failed)++;
    }
    std::printf("%s: %d passed, %d failed, %d skipped (debug-only)\n", argv[0], passed, failed, skipped);
    return failed ? 1 : 0;
}
