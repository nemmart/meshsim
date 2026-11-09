// replay_t.h: ref's test/T.java for the replayer's ported suites (WORKER15): checks counted per group, failures
// printed with the group's name; `dies` for T.ub and T.throwsISE (ref's exceptions are FATAL here: a forked child must
// abort with every given text in its stderr).
#pragma once

#include <csignal>
#include <cstdio>
#include <functional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace rtest {

struct T {
    static inline int passed = 0, failed = 0;
    static inline std::string current;
    static void group(const std::string& n) { current = n; }
    static void check(bool ok, const std::string& what) {
        if (ok) passed++;
        else { failed++; std::fprintf(stderr, "FAIL [%s] %s\n", current.c_str(), what.c_str()); }
    }
    static void eq(int64_t expect, int64_t got, const std::string& what) {
        check(expect == got, what + ": expected " + std::to_string(expect) + " got " + std::to_string(got));
    }
    // T.ub / T.throwsISE: fn must abort (FATAL) with every one of `names` in its stderr.
    static void dies(const std::function<void()>& fn, const std::string& what, std::initializer_list<const char*> names = {}) {
        int fd[2];
        if (pipe(fd) != 0) { check(false, what + ": pipe"); return; }
        std::fflush(nullptr);
        pid_t pid = fork();
        if (pid == 0) {
            close(fd[0]); dup2(fd[1], 2); close(fd[1]);
            fn();
            _exit(0);
        }
        close(fd[1]);
        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = read(fd[0], buf, sizeof buf)) > 0) out.append(buf, size_t(n));
        close(fd[0]);
        int st = 0;
        waitpid(pid, &st, 0);
        if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGABRT) { check(false, what + ": ran on, no stop"); return; }
        for (const char* nm : names)
            if (out.find(nm) == std::string::npos) { check(false, what + ": the stop doesn't name '" + nm + "': " + out); return; }
        passed++;
    }
};

}  // namespace rtest
