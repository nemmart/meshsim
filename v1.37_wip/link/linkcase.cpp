// linkcase: plays kept cases (cases/*.case). Each has a config, a list of events, and "run" lines: a set of rules
// and what must come of it, "ok" or "fail" with a piece of the property's text. Exit 1 if any run comes out otherwise.
//   linkcase [-v] [-t] <file>...     -v: every run's outcome; -t: what happens in each, line by line
#include <cstdio>
#include <cstring>

#include "bench.h"

using namespace hl;

int main(int argc, char** argv) {
    int wrong = 0, runs = 0;
    bool verbose = false, trace = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) { verbose = true; continue; }
        if (!strcmp(argv[i], "-t")) { trace = true; verbose = true; continue; }
        Case c; std::string err;
        if (!parse_case(argv[i], c, err)) { printf("%s: %s\n", argv[i], err.c_str()); wrong++; continue; }
        if (c.runs.empty()) { printf("%s: no run line\n", argv[i]); wrong++; }
        for (const Run& r : c.runs) {
            Link1 end(c.cfg);
            if (trace) printf("---- %s under %s\n", argv[i], r.rules.c_str());
            const std::string bad = play(c, r.rules, &end, trace ? stdout : nullptr);
            const bool as_expected = r.ok ? bad.empty() : (!bad.empty() && bad.find(r.text) != std::string::npos);
            runs++;
            if (!as_expected) wrong++;
            if (!as_expected || verbose)
                printf("%s %s: %s [%s]%s\n", as_expected ? "ok  " : "WRONG", argv[i], r.rules.c_str(), bad.empty() ? "every property held" : bad.c_str(),
                       as_expected ? "" : r.ok ? " (expected ok)" : " (expected that failure)");
        }
    }
    printf("linkcase: %d runs, %d wrong\n", runs, wrong);
    return wrong != 0;
}
