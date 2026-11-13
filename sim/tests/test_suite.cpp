// test_suite.cpp: suite.h's lines and counts equal v1.3's T on the same sequence (oracle/ref/TSelf.java,
// fixture tests/suite/tself.out from oracle/ref/tself.sh), and its stops count only a FATAL.
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "base/fatal.h"
#include "harness.h"
#include "suite.h"

namespace {

// TSelf's sequence, line for line.
int tself() {
    suite::reset();
    suite::check(true, "never printed");
    suite::check(false, "a failure before any group");
    suite::group("checks");
    suite::check(1 + 1 == 2, "passes");
    suite::check(false, "fails at %d with a value %d", 42, -7);
    suite::group("eq");
    suite::eq(5, 5, "equal");
    suite::eq(5, 6, "unequal");
    suite::eq(-1, 0xFFFFFFFFll, "a word against -1");
    suite::eq(INT64_MIN, INT64_MAX, "the extremes");
    suite::eq(int32_t(0x80000000u), 0x80000000ll, "an int widened");
    suite::group("eqo");
    const int64_t l[] = {0, 2, 3, 5, 8};
    suite::eqo("[0, 2, 3, 5, 8]", suite::jlist(l, 5), "a list, equal");
    suite::eqo("[0, 1]", suite::jlist(l, 0), "a list against the empty one");
    suite::eqo("x", "y", "strings");
    suite::eqo((const char*)nullptr, (const char*)nullptr, "two nulls");
    suite::eqo((const char*)nullptr, "z", "null against a string");
    suite::group("stops");
    suite::throws([] { FATAL("bad put"); }, "throws");
    suite::throws([] {}, "runs on");
    suite::ub([] { FATAL("undefined behavior at cycle 5, tile 3, director: a mode-0 packet at 0x4 not aligned to its size "
                         "(DECISIONS-v1.25 s8: meshsim stops; the hardware does nothing special)"); },
              {"director", "aligned"}, "names both");
    suite::ub([] {}, {"x"}, "ub runs on");
    suite::ub([] { FATAL("undefined behavior at cycle 9, tile 1, read port: a misaligned read (DECISIONS-v1.25 s8: meshsim "
                         "stops; the hardware does nothing special)"); },
              {"read port", "gather", "nothing"}, "names the wrong unit");
    suite::pending("a part of this suite", "phase 2");
    suite::group("");
    suite::check(false, "the empty group");
    return suite::done("TSelf");
}

// Runs fn in a child with stdout captured; returns what it printed and "exit <status>", as tself.sh records Java's.
std::string captured(int (*fn)()) {
    std::fflush(stdout);
    int fd[2];
    if (pipe(fd) != 0) return "pipe failed";
    const pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 1);
        close(fd[1]);
        const int rc = fn();
        std::fflush(stdout);
        _exit(rc);
    }
    close(fd[1]);
    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd[0], buf, sizeof buf)) > 0) out.append(buf, size_t(n));
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return out + "exit " + std::to_string(WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st)) + "\n";
}

std::string file(const char* path) {
    std::string s;
    FILE* f = std::fopen(path, "rb");
    if (!f) return "(missing " + std::string(path) + ")";
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

int other_deaths() {
    suite::reset();
    suite::group("deaths");
    suite::throws([] { std::raise(SIGKILL); }, "a signal is not a stop");
    suite::throws([] { std::fprintf(stderr, "FATAL looks like one\n"); std::exit(3); }, "an exit is not a stop");
    std::string m = suite::ub([] { FATAL("tile %d: UB", 7); }, {"tile 7"}, "the message");
    suite::eqo("tile 7: UB", m, "ub returns the message");
    return suite::done("deaths");
}

}  // namespace

TEST(suite_lines_equal_javas_T) {
    const std::string got = captured(tself), want = file("tests/suite/tself.out");
    if (got != want) std::fprintf(stderr, "---- got\n%s---- want (tests/suite/tself.out)\n%s", got.c_str(), want.c_str());
    CHECK(got == want);
}

TEST(suite_stops_count_only_a_fatal) {
    const std::string got = captured(other_deaths);
    const std::string want =
        "FAIL [deaths] a signal is not a stop: died of signal 9 without a FATAL\n"
        "FAIL [deaths] an exit is not a stop: exited with status 3 without a FATAL: FATAL looks like one\n"
        "deaths: 2 passed, 2 failed\nexit 1\n";
    if (got != want) std::fprintf(stderr, "---- got\n%s---- want\n%s", got.c_str(), want.c_str());
    CHECK(got == want);
}
