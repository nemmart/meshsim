// The CPU detection and default placement (WORKER4, D21), on fake /sys trees of known layouts (as tools/fake_sysfs.sh
// makes them; tools/test_machine.sh checks machine.sh against the same lists).
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include "harness.h"
#include "topology.h"

using namespace sim;

namespace {
namespace fs = std::filesystem;

void put(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << s << "\n";
}
// The layouts of tools/fake_sysfs.sh.
std::string make(const std::string& layout) {
    const std::string d = "/tmp/sim-fake-sysfs-" + layout + "-" + std::to_string(getpid());
    std::error_code ec;
    fs::remove_all(d, ec);
    const std::string cpu = d + "/devices/system/cpu";
    auto sib = [&](int c, const std::string& s) { put(cpu + "/cpu" + std::to_string(c) + "/topology/thread_siblings_list", s); };
    if (layout == "hybrid") {
        put(cpu + "/online", "0-15");
        for (int c = 0; c < 12; c += 2) { sib(c, std::to_string(c) + "-" + std::to_string(c + 1)); sib(c + 1, std::to_string(c) + "-" + std::to_string(c + 1)); }
        for (int c = 12; c < 16; c++) sib(c, std::to_string(c));
        put(d + "/devices/cpu_core/cpus", "0-11");
        put(d + "/devices/cpu_atom/cpus", "12-15");
    } else if (layout == "server") {
        put(cpu + "/online", "0-15");
        for (int c = 0; c < 8; c++) { sib(c, std::to_string(c) + "," + std::to_string(c + 8)); sib(c + 8, std::to_string(c) + "," + std::to_string(c + 8)); }
    } else {
        put(cpu + "/online", "0-3");
        for (int c = 0; c < 4; c++) sib(c, std::to_string(c));
    }
    return d;
}
Topology detect(const std::string& layout, const char* allowed = "0-15") {
    const std::string d = make(layout);
    setenv("SIM_SYSFS", d.c_str(), 1);
    setenv("SIM_ALLOWED_CPUS", allowed, 1);
    Topology t = Topology::detect();
    unsetenv("SIM_SYSFS");
    unsetenv("SIM_ALLOWED_CPUS");
    std::error_code ec;
    fs::remove_all(d, ec);
    return t;
}

TEST(cpu_lists_parse_and_print) {
    CHECK(cpu_list(parse_cpu_list("0-3,8,10-11\n")) == "0,1,2,3,8,10,11");
    CHECK(parse_cpu_list("").empty());
}
TEST(a_hybrid_chip_places_one_per_p_core_then_siblings_then_e_cores) {
    Topology t = detect("hybrid");
    CHECK(cpu_list(t.p_one) == "0,2,4,6,8,10");
    CHECK(cpu_list(t.e) == "12,13,14,15");
    CHECK(cpu_list(t.place) == "0,2,4,6,8,10,1,3,5,7,9,11,12,13,14,15");
    CHECK(cpu_list(t.placement(3)) == "0,2,4");
    CHECK(cpu_list(t.placement(18)) == "0,2,4,6,8,10,1,3,5,7,9,11,12,13,14,15,0,2");
}
TEST(a_two_socket_server_places_one_per_core_first) {
    Topology t = detect("server");
    CHECK(t.e.empty());
    CHECK(cpu_list(t.p_one) == "0,1,2,3,4,5,6,7");
    CHECK(cpu_list(t.place) == "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15");
}
TEST(a_plain_chip_is_all_p_cores) {
    Topology t = detect("plain");
    CHECK(cpu_list(t.place) == "0,1,2,3");
}
TEST(only_the_cpus_this_process_may_use) {
    Topology t = detect("hybrid", "2-9,13");
    CHECK(cpu_list(t.place) == "2,4,6,8,3,5,7,9,13");
}
TEST(the_environment_overrides_detection) {
    setenv("SIM_P_CPUS", "4-5", 1);
    setenv("SIM_E_CPUS", "7", 1);
    Topology t = detect("hybrid");
    unsetenv("SIM_P_CPUS");
    unsetenv("SIM_E_CPUS");
    CHECK(cpu_list(t.place).rfind("4,5,7,", 0) == 0);
}
TEST(this_machine_has_a_record) {
    Topology t = Topology::detect();
    CHECK(!t.place.empty());
    CHECK(t.record().rfind("machine model=", 0) == 0);
}
}  // namespace
