// tracer_play (WORKER14): the Tracer's speed alone, on the calls a real run makes.
//
//   tracer_play convert LOG OUT     a TracerConf log (oracle/ref/TracerConf.java; a file, .gz or a FIFO) to a compact call
//                                   trace: the store as the run starts, then the calls grouped by cycle as the log
//                                   groups them (identical cycles run-length coded), the results and observations left
//                                   out. One Tracer (T1's rig).
//   tracer_play run TRACE [REPS]    replays the trace on the C++ Tracer, timed: per repetition a fresh store, memory
//                                   system and Tracer (configuration, not timed), then every call in order (timed),
//                                   per segment (a segment starts at its ownership edit) and in all; then the same
//                                   decoding with no Tracer call (the harness's own cost). No comparison: test_tracer
//                                   has shown these calls equal to ref's, cycle by cycle (Quest 163).
// ref's side is oracle/bench/TracerPlay.java on the same trace file. Timing: clang release (D18), pinned, machine recorded
// (D21; the task body).
//
// THE TRACE (little-endian; v = unsigned LEB128, z = zigzag LEB128, raw = 8 bytes):
//   "W14T" v1
//   'S' first:v nextFree:v n:v, then n pages: addr:v kind:byte (0 zero, 1 data then 512 raw)
//   'N' icacheLines:v
//   'D' a cycle's calls (the next id): n:v, then n ops: op:byte d:z args (below)
//   'G' id:v gap:z n:v      n cycles from t0 = the previous group's end + gap, each making D[id]'s calls at t0 + i + d
//   'E' rows:v recHash:raw   the last observation's statistics row count and record hash (ref's): each timed pass
//                             must end on them, so a trace that lost a call can't be timed
// ops: 1 st, 2 u, 3 sy, 4 a src:v off:v size:v lo:raw hi:raw, 5 ww w:v val:v, 6 wr w:v, 7 rn, 8 er, 9 ev,
//      10 co bits:v, 11 re, 12 wb, 13 os q:v n:v, 14 or q:v, 15 ot q:v, 16 O root:v depth:v n:v pages:v..., 17 mc, 18 cs
#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "units/file_store.h"
#include "units/mem_system.h"
#include "units/tracer.h"

using units::tracer::FileStore;
using units::tracer::MemSystem;
using units::tracer::Tracer;

namespace {

[[noreturn]] void die(const char* fmt, const char* a = "", long long b = 0) {
    std::fprintf(stderr, "tracer_play: ");
    std::fprintf(stderr, fmt, a, b);
    std::fprintf(stderr, "\n");
    std::exit(2);
}

// ---------------------------------------------------------------- writing
struct Out {
    FILE* f;
    void byte(int b) { std::fputc(b, f); }
    void v(uint64_t x) {
        do {
            uint8_t b = x & 0x7F;
            x >>= 7;
            if (x) b |= 0x80;
            std::fputc(b, f);
        } while (x);
    }
    void z(int64_t x) { v((uint64_t(x) << 1) ^ uint64_t(x >> 63)); }
    void raw(uint64_t x) { for (int i = 0; i < 8; i++) std::fputc(int((x >> (8 * i)) & 0xFF), f); }
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> r;
    size_t a = 0;
    while (true) {
        size_t b = s.find(sep, a);
        r.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) return r;
        a = b + 1;
    }
}
uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }
int64_t dec(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 10); }

// One cycle's tokens (results dropped) to a 'D' record.
void def(Out& o, const std::string& sig) {
    const auto toks = split(sig, ';');
    o.byte('D');
    o.v(toks.size());
    for (const auto& tk0 : toks) {
        std::string tk = tk0;
        if (tk.back() == '!' || tk.back() == '%') die("a fatal refusal in the log: %s", tk.c_str());
        const size_t eq = tk.find('=');
        if (eq != std::string::npos) tk = tk.substr(0, eq);
        auto a = split(tk, ',');
        std::string name = a[0];
        int64_t d = 0;
        const size_t at = name.find('@');
        if (at != std::string::npos) { d = dec(name.substr(at + 1)); name = name.substr(0, at); }
        static const std::map<std::string, int> ops = {{"st", 1}, {"u", 2}, {"sy", 3}, {"a", 4}, {"ww", 5}, {"wr", 6},
            {"rn", 7}, {"er", 8}, {"ev", 9}, {"co", 10}, {"re", 11}, {"wb", 12}, {"os", 13}, {"or", 14}, {"ot", 15},
            {"O", 16}, {"mc", 17}, {"cs", 18}, {"H", 0}};
        auto it = ops.find(name);
        if (it == ops.end()) die("an unknown token %s", tk0.c_str());
        o.byte(it->second);
        o.z(d);
        switch (it->second) {
            case 4: o.v(uint64_t(dec(a[1]))); o.v(hx(a[2])); o.v(uint64_t(dec(a[3]))); o.raw(hx(a[4])); o.raw(hx(a[5])); break;
            case 5: o.v(uint64_t(dec(a[1]))); o.v(hx(a[2])); break;
            case 6: case 14: case 15: o.v(uint64_t(dec(a[1]))); break;
            case 10: o.v(hx(a[1])); break;
            case 13: o.v(uint64_t(dec(a[1]))); o.v(uint64_t(dec(a[2]))); break;
            case 16:
                o.v(uint64_t(dec(a[1]))); o.v(uint64_t(dec(a[2]))); o.v(a.size() - 3);
                for (size_t k = 3; k < a.size(); k++) o.v(uint64_t(dec(a[k])));
                break;
            default: break;
        }
    }
}

int convert(const char* in_path, const char* out_path) {
    const std::string p = in_path;
    const bool gz = p.size() > 3 && p.compare(p.size() - 3, 3, ".gz") == 0;
    FILE* in = gz ? popen(("gzip -dc " + p).c_str(), "r") : std::fopen(in_path, "r");
    FILE* of = std::fopen(out_path, "wb");
    if (!in || !of) die("cannot open %s", in_path);
    Out o{of};
    std::fwrite("W14T", 1, 4, of);
    o.v(1);
    std::map<std::string, uint64_t> ids;
    int64_t next_t = 0, groups = 0, cycles = 0;
    int rig = -1;
    bool store_done = false, ended = false;
    std::vector<std::string> pages;
    std::string store_head, last_obs;
    auto flush_store = [&]() {
        if (store_done || store_head.empty()) return;
        const auto f = split(store_head, ' ');
        o.byte('S'); o.v(uint64_t(dec(f[3]))); o.v(uint64_t(dec(f[4]))); o.v(pages.size());
        for (const auto& pl : pages) {
            const auto q = split(pl, ' ');
            o.v(uint64_t(dec(q[2])));
            if (q[0] == "Z") { o.byte(0); continue; }
            o.byte(1);
            for (int w = 0; w < 512; w++) o.raw(hx(q[size_t(3 + w)]));
        }
        store_done = true;
        pages.clear();
    };
    char* buf = nullptr;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&buf, &cap, in)) >= 0) {
        std::string line(buf, size_t(len));
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (line.compare(0, 5, "# end") == 0) ended = true;
        if (line.empty() || line[0] == '#') continue;
        const size_t s1 = line.find(' ');
        const std::string kind = line.substr(0, s1);
        if (kind == "o") { last_obs = line; continue; }
        if (kind == "fp" || kind == "fe") continue;
        const auto f = split(line.substr(0, line.find(' ', s1 + 1)), ' ');
        const int id = int(dec(f[1]));
        if (rig >= 0 && id != rig) die("more than one Tracer in the log (%s)", "", id);
        if (kind == "N") { rig = id; o.byte('N'); o.v(uint64_t(dec(split(line, ' ')[3]))); continue; }
        if (kind == "F") {
            if (store_done) die("a store dump after the first: not a T1 log");
            store_head = line;
            continue;
        }
        if (kind == "P" || kind == "Z") { pages.push_back(line); continue; }
        if (kind != "c") die("an unknown line kind %s", kind.c_str());
        flush_store();
        const auto c = split(line, ' ');
        const int64_t t0 = dec(c[2]), n = dec(c[3]);
        std::string sig = c.size() > 4 ? c[4] : "";
        // the results are not part of the key: the same calls make one definition
        std::string key;
        for (const auto& tk : split(sig, ';')) {
            if (!key.empty()) key += ';';
            key += tk.substr(0, tk.find('='));
        }
        auto it = ids.find(key);
        if (it == ids.end()) {
            def(o, sig);
            it = ids.emplace(key, ids.size()).first;
        }
        o.byte('G'); o.v(it->second); o.z(t0 - next_t); o.v(uint64_t(n));
        next_t = t0 + n;
        groups++;
        cycles += n;
    }
    std::free(buf);
    o.byte('E');
    {   // OBS fields (TracerConf.java): [2 + 18] rows, [2 + 49] the record's hash
        const auto ob = split(last_obs, ' ');
        if (ob.size() < 52) die("no observation in the log");
        o.v(hx(ob[20]));
        o.raw(hx(ob[51]));
    }
    const int rc = gz ? pclose(in) : std::fclose(in);
    std::fclose(of);
    if (!ended) die("the log has no '# end' line: ref's run was cut off");
    std::printf("convert: %lld groups, %lld cycles, %zu cycle kinds%s\n", (long long)groups, (long long)cycles, ids.size(),
                rc ? " (reading the log failed)" : "");
    return rc ? 1 : 0;
}

// ---------------------------------------------------------------- reading
struct Op {
    uint8_t op;
    int64_t d;
    uint64_t a, b, c;
    uint64_t lo, hi;
    std::vector<int64_t> pages;  // O only
};
struct In {
    const uint8_t* p;
    const uint8_t* end;
    uint64_t v() {
        uint64_t x = 0;
        int s = 0;
        while (true) {
            const uint8_t b = *p++;
            x |= uint64_t(b & 0x7F) << s;
            if (!(b & 0x80)) return x;
            s += 7;
        }
    }
    int64_t z() { const uint64_t u = v(); return int64_t(u >> 1) ^ -int64_t(u & 1); }
    uint64_t raw() { uint64_t x = 0; for (int i = 0; i < 8; i++) x |= uint64_t(p[i]) << (8 * i); p += 8; return x; }
};

double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

struct Trace {
    std::vector<uint8_t> bytes;
    std::vector<std::vector<Op>> defs;  // every cycle kind (configuration: decoded before the timed pass)
    std::vector<uint8_t> groups;        // the 'G' records' payloads only, in order (id:v gap:z n:v)
    int icache_lines = 128;
    std::unique_ptr<FileStore> store;   // the store as the run starts
    uint64_t end_rows = 0, end_rec_hash = 0;
};

void read_def(In& in, std::vector<Op>& ops) {
    const uint64_t n = in.v();
    ops.resize(n);
    for (auto& o : ops) {
        o.op = *in.p++;
        o.d = in.z();
        o.a = o.b = o.c = o.lo = o.hi = 0;
        switch (o.op) {
            case 4: o.a = in.v(); o.b = in.v(); o.c = in.v(); o.lo = in.raw(); o.hi = in.raw(); break;
            case 5: case 13: o.a = in.v(); o.b = in.v(); break;
            case 6: case 10: case 14: case 15: o.a = in.v(); break;
            case 16: { o.a = in.v(); o.b = in.v(); const uint64_t k = in.v(); for (uint64_t i = 0; i < k; i++) o.pages.push_back(int64_t(in.v())); break; }
            default: break;
        }
    }
}

// Loads the file, builds the store, and collects every definition (one pre-pass, so the timed pass only decodes groups).
void load(const char* path, Trace& tr) {
    FILE* f = std::fopen(path, "rb");
    if (!f) die("cannot open %s", path);
    std::fseek(f, 0, SEEK_END);
    tr.bytes.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(tr.bytes.data(), 1, tr.bytes.size(), f) != tr.bytes.size()) die("short read on %s", path);
    std::fclose(f);
    if (tr.bytes.size() < 5 || std::memcmp(tr.bytes.data(), "W14T", 4) != 0) die("%s is not a trace", path);
    In in{tr.bytes.data() + 4, tr.bytes.data() + tr.bytes.size()};
    if (in.v() != 1) die("trace version");
    while (in.p < in.end) {
        const uint8_t k = *in.p++;
        if (k == 'S') {
            const int64_t first = int64_t(in.v()), next = int64_t(in.v()), n = int64_t(in.v());
            tr.store = std::make_unique<FileStore>(int32_t(first));
            tr.store->grow_to(next);
            for (int64_t i = 0; i < n; i++) {
                const int32_t a = int32_t(in.v());
                if (*in.p++ == 1) { uint64_t* pg = tr.store->raw_page(a); for (int w = 0; w < 512; w++) pg[w] = in.raw(); }
            }
        } else if (k == 'N') {
            tr.icache_lines = int(in.v());
        } else if (k == 'D') {
            tr.defs.emplace_back();
            read_def(in, tr.defs.back());
        } else if (k == 'G') {
            const uint8_t* g = in.p;
            in.v(); in.z(); in.v();
            tr.groups.insert(tr.groups.end(), g, in.p);
        } else if (k == 'E') {
            tr.end_rows = in.v();
            tr.end_rec_hash = in.raw();
            break;
        } else {
            die("a bad record in the trace (%s %lld)", "", k);
        }
    }
    if (!tr.store) die("no store in the trace");
    tr.bytes.clear();
    tr.bytes.shrink_to_fit();
}

struct Result {
    double seconds = 0;
    int64_t cycles = 0, calls = 0;
    std::vector<std::pair<int64_t, double>> segs;  // (cycles, seconds) per segment
    uint64_t sink = 0;
};

// One timed pass. calls_on false: the same decoding and dispatch with no Tracer call (the harness's cost).
template <bool kCalls>
Result pass(const Trace& tr) {
    FileStore fs(*tr.store);  // configuration: copies, not timed
    MemSystem ms(&fs);
    Tracer t(66, tr.icache_lines);
    t.mem = &ms;
    t.cold_mem_on_state_write = true;
    Result r;
    In in{tr.groups.data(), tr.groups.data() + tr.groups.size()};
    int64_t next_t = 0, seg_cycles = 0;
    uint64_t w2[2];
    uint32_t v32;
    const double t0 = now();
    double seg_t0 = t0;
    while (in.p < in.end) {
        const std::vector<Op>& ops = tr.defs[in.v()];
        const int64_t g0 = next_t + in.z(), n = int64_t(in.v());
        next_t = g0 + n;
        for (int64_t i = 0; i < n; i++) {
            const int64_t tc = g0 + i;
            for (const Op& o : ops) {
                const int64_t t_ = tc + o.d;
                if constexpr (!kCalls) { r.sink += uint64_t(t_) ^ o.a; continue; }
                switch (o.op) {
                    case 1: t.step(t_); break;
                    case 2: t.run(t_); break;
                    case 3: t.sync(t_); break;
                    case 4: t.accept(t_, int(o.a), uint32_t(o.b), int(o.c), o.lo, o.hi); break;
                    case 5: r.sink += uint64_t(t.write_word(t_, int(o.a), uint32_t(o.b))); break;
                    case 6: r.sink += t.word(t_, int(o.a), v32) ? v32 : 0; break;
                    case 7: r.sink += t.running(t_); break;
                    case 8: r.sink += uint64_t(t.errors(t_)); break;
                    case 9: r.sink += t.events().value(t_); break;
                    case 10: t.consume_events(uint32_t(o.a)); break;
                    case 11: r.sink += t.rows_ended(t_); break;
                    case 12: r.sink += uint64_t(t.write_back_dirty_untimed()); break;
                    case 13: t.out_start(t_, int(o.a), int64_t(o.b)); break;
                    case 14: r.sink += t.out_ready(t_, int(o.a)); break;
                    case 15: t.out_take(t_, int(o.a), w2); r.sink += w2[0]; break;
                    case 16: {  // a segment starts: the harness's ownership edit (not the Tracer's time)
                        const double x = now();
                        if (seg_cycles) r.segs.push_back({seg_cycles, x - seg_t0});
                        FileStore::File f;
                        f.root = int32_t(o.a);
                        f.depth = int(o.b);
                        fs.set_ownership(f, o.pages);
                        seg_t0 = now();
                        r.seconds -= seg_t0 - x;  // the edit is not timed
                        seg_cycles = 0;
                        break;
                    }
                    case 17: ms.cold(); break;
                    case 18: for (auto& s : t.stalls) s = 0; break;
                    default: break;
                }
                r.calls++;
            }
            r.cycles++;
            seg_cycles++;
        }
    }
    const double t1 = now();
    r.seconds += t1 - t0;
    if (seg_cycles) r.segs.push_back({seg_cycles, t1 - seg_t0});
    r.sink += uint64_t(t.rows);
    if (kCalls) {
        uint64_t h = 17;
        for (uint64_t x : t.rec) h = h * 31 + x;
        if (uint64_t(t.rows) != tr.end_rows || h != tr.end_rec_hash)
            die("the pass did not end where ref's run did (rows %lld): the trace lost a call", "", (long long)t.rows);
    }
    return r;
}

int run(const char* path, int reps) {
    Trace tr;
    const double l0 = now();
    load(path, tr);
    std::printf("trace %s: groups %.1f MB, %zu cycle kinds, store %zu pages, loaded in %.2f s\n", path,
                double(tr.groups.size()) / 1e6, tr.defs.size(), tr.store->pages_allocated(), now() - l0);
    std::vector<double> per;
    for (int k = 0; k < reps; k++) {
        const Result r = pass<true>(tr);
        per.push_back(1e9 * r.seconds / double(r.cycles));
        std::printf("rep %d cycles=%lld calls=%lld seconds=%.3f ns_per_cycle=%.2f sink=%llx segs=", k, (long long)r.cycles,
                    (long long)r.calls, r.seconds, 1e9 * r.seconds / double(r.cycles), (unsigned long long)r.sink);
        for (const auto& s : r.segs) std::printf("%.2f,", 1e9 * s.second / double(s.first));
        std::printf("\n");
    }
    const Result e = pass<false>(tr);
    std::printf("harness cycles=%lld seconds=%.3f ns_per_cycle=%.2f (decoding and dispatch, no Tracer call)\n",
                (long long)e.cycles, e.seconds, 1e9 * e.seconds / double(e.cycles));
    std::sort(per.begin(), per.end());
    std::printf("tracer_play median ns_per_cycle=%.2f of %d reps\n", per[per.size() / 2], reps);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::strcmp(argv[1], "convert") == 0) return convert(argv[2], argv[3]);
    if (argc >= 3 && std::strcmp(argv[1], "run") == 0) return run(argv[2], argc > 3 ? std::atoi(argv[3]) : 3);
    std::fprintf(stderr, "usage: tracer_play convert LOG OUT | run TRACE [REPS]\n");
    return 2;
}
