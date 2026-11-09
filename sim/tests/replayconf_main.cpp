// replayconf: a ReplayConf script (tests/ref/ReplayConf.java) replayed on units::Replayer; the observations on stdout.
//   replayconf SCRIPT [MAX_ROWS]      (SCRIPT plain or .gz)
#include <cstdio>
#include <cstdlib>
#include <string>

#include "portconf.h"
#include "replayconf.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: replayconf SCRIPT [MAX_ROWS]\n"); return 2; }
    std::string path = argv[1];
    bool ok = false;
    std::string script = portconf::read_command((path.size() > 3 && path.substr(path.size() - 3) == ".gz" ? "gzip -dc " : "cat ") + path, &ok);
    if (!ok) { std::fprintf(stderr, "replayconf: cannot read %s\n", argv[1]); return 2; }
    static units::Replayer r(0);
    std::string obs = replayconf::run(script, argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1u << 20, &r);
    std::fwrite(obs.data(), 1, obs.size(), stdout);
    return 0;
}
