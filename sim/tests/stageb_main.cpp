// stageb: v1.3's `java StageB`, its output line for line (tests/stageb.h).
//   stageb synthetic [--image DIR] [--oldmap DIR] [--shuffle S]
//       run_tests.sh's step stageb: Stage A's synthetic windows on the tile, against Stage A's ideal controller
//   stageb DELIVERY VARIANT PROGRAM [WINDOW ...] [--lone] [--norow N] [--profile] [--image DIR] [--shuffle S]
//       a delivery's windows (all the manifest lists, or those named) on the tile; VARIANT state, state-lru or
//       state-pick; the step stageb-<guest> (DELIVERY=), as v1.3's run_tests.sh runs it
// --image: replay_ctl's directory (default tests/m3-images: ref's sw/replay/build); --oldmap: the old queue map's
// (default fw/images/oldmap); --lone, --norow, --profile: ref's -Dstageb.lone, -Dstageb.norow, -Dstageb.profile;
// --shuffle S, --step-all, --instance-major: the framework's debug options (the output must not change). Tables: $SIM_REPLAY_TABLES or data/tables/replayer, unless
// the delivery has its own (ref's tablesFor). Run from sim/.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "stageb.h"

int main(int argc, char** argv) {
    stageb::Options o;
    std::string image_dir = "tests/m3-images", oldmap_dir = "fw/images/oldmap";
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) FATAL("stageb: %s needs a value", a.c_str()); return argv[++i]; };
        if (a == "--lone") o.lone = true;
        else if (a == "--norow") o.norow = std::atoll(next().c_str());
        else if (a == "--profile") o.profile = true;
        else if (a == "--image") image_dir = next();
        else if (a == "--oldmap") oldmap_dir = next();
        else if (a == "--shuffle") o.shuffle = std::atoll(next().c_str());
        else if (a == "--step-all") o.step_all = true;
        else if (a == "--instance-major") o.instance_major = true;
        else pos.push_back(a);
    }
    if (pos.empty()) { std::fprintf(stderr, "usage: stageb synthetic | stageb DELIVERY VARIANT PROGRAM [WINDOW ...] [options]\n"); return 2; }
    if (pos[0] == "synthetic") return stageb::synthetic(image_dir, oldmap_dir, o);
    if (pos.size() < 3) { std::fprintf(stderr, "usage: stageb DELIVERY VARIANT PROGRAM [WINDOW ...]\n"); return 2; }
    const std::string dir = pos[0], variant = pos[1], prog = pos[2];
    std::vector<long long> only;
    for (size_t i = 3; i < pos.size(); i++) only.push_back(std::atoll(pos[i].c_str()));
    std::vector<stagea::Program> progs;
    std::string err;
    if (!stagea::read_manifest(dir + "/manifest.json", progs, err)) FATAL("stageb: %s", err.c_str());
    const std::string pd = dir + "/" + prog;
    mach::Mach::Image im = stageb::image(image_dir);
    std::unique_ptr<units::tracer::ReplayTables> tb;
    int windows = 0, failed = 0;
    for (const stagea::Program& p : progs) {
        if (p.name != prog) continue;
        for (const stagea::Entry& we : p.windows) {
            if (!only.empty() && std::find(only.begin(), only.end(), we.index) == only.end()) continue;
            windows++;
            if (!tb) tb = std::make_unique<units::tracer::ReplayTables>(units::tracer::ReplayTables::load(stagea::tables_dir(dir, pd)));   // ref: per window
            if (!stageb::run_window(im, *tb, pd, we, variant, prog, o).ok) failed++;
        }
    }
    std::printf("StageB %s %s: %d windows, %d failed\n", prog.c_str(), variant.c_str(), windows, failed);
    return failed == 0 && windows > 0 ? 0 : 1;
}
