// portconf SCRIPT: the C++ side of port conformance as a tool (oracle/conformance.sh): writes the observation lines of a
// PortConfGen script run on the framework's port, to compare with PortConfDriver's on ref (PORTS.md 10).
#include <cstdio>
#include <fstream>
#include <sstream>

#include "portconf.h"

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: portconf SCRIPT\n"); return 2; }
    std::ifstream f(argv[1]);
    if (!f) { std::fprintf(stderr, "portconf: can't read %s\n", argv[1]); return 2; }
    std::stringstream ss;
    ss << f.rdbuf();
    portconf::Script s;
    std::string err = portconf::parse(ss.str(), s);
    if (!err.empty()) { std::fprintf(stderr, "portconf: %s: %s\n", argv[1], err.c_str()); return 2; }
    bool skipped = false;
    std::string out = portconf::run(s, &skipped);
    if (skipped) { std::fprintf(stderr, "portconf: %s: the reserved link is M2's\n", argv[1]); return 3; }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}
