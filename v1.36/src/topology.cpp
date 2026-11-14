// topology.cpp: see topology.h.
#include "topology.h"

#include <sched.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "base/fatal.h"

namespace sim {

std::vector<int> parse_cpu_list(const std::string& s) {
    std::vector<int> v;
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, ',')) {
        while (!part.empty() && (part.back() == '\n' || part.back() == ' ')) part.pop_back();
        if (part.empty()) continue;
        const size_t dash = part.find('-');
        char* end = nullptr;
        const long a = std::strtol(part.c_str(), &end, 10);
        long b = a;
        if (dash != std::string::npos) b = std::strtol(part.c_str() + dash + 1, &end, 10);
        if (a < 0 || b < a || b > 65535) FATAL("cpu list '%s': can't read '%s'", s.c_str(), part.c_str());
        for (long k = a; k <= b; k++) v.push_back(int(k));
    }
    return v;
}
std::string cpu_list(const std::vector<int>& v) {
    std::string s;
    for (int c : v) s += (s.empty() ? "" : ",") + std::to_string(c);
    return s;
}

namespace {
std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::vector<int> keep(const std::vector<int>& v, const std::vector<int>& allowed) {
    std::vector<int> out;
    for (int c : v) if (std::find(allowed.begin(), allowed.end(), c) != allowed.end()) out.push_back(c);
    return out;
}
bool has(const std::vector<int>& v, int c) { return std::find(v.begin(), v.end(), c) != v.end(); }
}  // namespace

Topology Topology::detect() {
    const char* root = std::getenv("SIM_SYSFS");
    const std::string sys = root && *root ? root : "/sys";
    Topology t;
    std::vector<int> online = parse_cpu_list(read_file(sys + "/devices/system/cpu/online"));
    std::vector<int> allowed;
    if (const char* a = std::getenv("SIM_ALLOWED_CPUS"); a && *a) allowed = parse_cpu_list(a);
    else {
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof set, &set) == 0)
            for (int c = 0; c < CPU_SETSIZE; c++) if (CPU_ISSET(c, &set)) allowed.push_back(c);
    }
    if (online.empty()) online = allowed;
    t.all = allowed.empty() ? online : keep(online, allowed);
    if (const char* p = std::getenv("SIM_P_CPUS"); p && *p) t.p = parse_cpu_list(p);
    else if (std::string s = read_file(sys + "/devices/cpu_core/cpus"); !s.empty()) t.p = keep(parse_cpu_list(s), t.all);
    else t.p = t.all;
    if (const char* e = std::getenv("SIM_E_CPUS"); e && *e) t.e = parse_cpu_list(e);
    else if (std::string s = read_file(sys + "/devices/cpu_atom/cpus"); !s.empty()) t.e = keep(parse_cpu_list(s), t.all);
    for (int c : t.p) {                                // one per P core: the first of its siblings among the P CPUs
        std::vector<int> sib = parse_cpu_list(read_file(sys + "/devices/system/cpu/cpu" + std::to_string(c) + "/topology/thread_siblings_list"));
        sib = keep(sib, t.p);
        const int first = sib.empty() ? c : sib[0];
        if (!has(t.p_one, first)) t.p_one.push_back(first);
    }
    t.place = t.p_one;
    for (const auto* l : {&t.p, &t.e, &t.all})
        for (int c : *l) if (!has(t.place, c)) t.place.push_back(c);
    return t;
}

std::vector<int> Topology::placement(unsigned n) const {
    std::vector<int> v;
    for (unsigned k = 0; k < n && !place.empty(); k++) v.push_back(place[k % place.size()]);
    return v;
}

std::string Topology::record() const {
    std::string model = "?";
    {
        std::stringstream ss(read_file("/proc/cpuinfo"));
        std::string line;
        while (std::getline(ss, line))
            if (line.rfind("model name", 0) == 0) { model = line.substr(line.find(':') + 2); break; }
    }
    auto khz = [&](int c) {
        std::string s = read_file("/sys/devices/system/cpu/cpu" + std::to_string(c) + "/cpufreq/cpuinfo_max_freq");
        while (!s.empty() && s.back() == '\n') s.pop_back();
        return s.empty() ? std::string("?") : s;
    };
    std::string mem = "?";
    {
        std::stringstream ss(read_file("/proc/meminfo"));
        std::string line;
        if (std::getline(ss, line)) { mem = line.substr(line.find(':') + 1); mem.erase(0, mem.find_first_not_of(' ')); }
    }
    utsname u{};
    uname(&u);
    std::string r = "machine model=\"" + model + "\" cpus=" + cpu_list(all) + " p_cores=" + std::to_string(p_one.size()) +
                    " p_cpus=" + cpu_list(p) + " p_max_khz=" + (p.empty() ? "?" : khz(p[0]));
    if (!e.empty()) r += " e_cpus=" + cpu_list(e) + " e_max_khz=" + khz(e[0]);
    r += " place=" + cpu_list(place) + " mem=\"" + mem + "\" kernel=\"" + u.sysname + " " + u.release + "\"";
#if defined(__clang__)
    r += " compiler=\"clang " __clang_version__ "\"";
#else
    r += " compiler=\"gcc " __VERSION__ "\"";
#endif
    return r;
}

}  // namespace sim
