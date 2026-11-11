// Port conformance against ref (WORKER3; PORTS.md 10): every committed fixture (sim/tests/portconf, seed 1 of each
// configuration at 2,000 cycles, ref's observations made by PortConfDriver) run on the framework's port of the same kind;
// the observation lines must be identical. The full matrix (10^5 cycles, three seeds) is `oracle/conformance.sh`.
#include <string>

#include "harness.h"
#include "portconf.h"

namespace {
void conform(const char* name) {
    const std::string dir = "tests/portconf/";
    bool ok = false;
    std::string script = portconf::read_command("gzip -dc " + dir + name + ".script.gz", &ok);
    if (!ok) { ::test::fail(__FILE__, __LINE__, "%s: can't read the script (run from sim/)", name); return; }
    std::string ref = portconf::body(portconf::read_command("gzip -dc " + dir + name + ".obs.gz", &ok));
    if (!ok) { ::test::fail(__FILE__, __LINE__, "%s: can't read ref's observations", name); return; }
    portconf::Script s;
    std::string err = portconf::parse(script, s);
    if (!err.empty()) { ::test::fail(__FILE__, __LINE__, "%s: %s", name, err.c_str()); return; }
    bool skipped = false;
    std::string ours = portconf::run(s, &skipped);
    if (skipped) { std::printf("     %s: the reserved link is M2's (PORTS.md 2a), not run\n", name); return; }
    std::string d = portconf::first_difference(ours, ref);
    if (!d.empty()) ::test::fail(__FILE__, __LINE__, "%s differs from ref at %s", name, d.c_str());
}
}  // namespace

#define CONFORM(id, name) TEST(conforms_##id) { conform(name); }
CONFORM(link_1, "link-1")
CONFORM(link_2, "link-2")
CONFORM(link_3, "link-3")
CONFORM(link_8, "link-8")
CONFORM(link_1_2, "link-1-2")      // W10: a Link that holds (depth C - L), ref's WordDelayLine(name, L, C)
CONFORM(link_3_4, "link-3-4")
CONFORM(link_3_11, "link-3-11")
CONFORM(link_3_35, "link-3-35")
CONFORM(link_8_12, "link-8-12")
CONFORM(fifo_1, "fifo-1")
CONFORM(fifo_2, "fifo-2")
CONFORM(fifo_4, "fifo-4")
CONFORM(fifo_16, "fifo-16")
CONFORM(credit_1, "credit-1")
CONFORM(credit_2, "credit-2")
CONFORM(credit_4, "credit-4")
CONFORM(credit_16, "credit-16")
CONFORM(hold, "hold")
CONFORM(reg, "reg")
CONFORM(rlink_1_2, "rlink-1-2")
CONFORM(rlink_2_4, "rlink-2-4")
CONFORM(rlink_3_8, "rlink-3-8")
