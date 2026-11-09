// test_tracer.cpp (WORKER14): the C++ Tracer against `ref`'s, cycle by cycle.
//
// tests/tracerconf/*.log.gz come from tests/ref/TracerConf.java (tests/ref/tracerconf.sh --fixtures): `ref`'s Tracer
// driven through its Peripheral interface, every call and its result recorded, and its state observed after every cycle.
// This test replays the calls on the C++ Tracer (with its own FileStore and MemSystem, restored from the log's store
// dumps) and requires every result, every store check and every observation to be equal. A call `ref` refused with
// UndefinedBehavior must be FATAL here: it is made in a forked child, and the parent goes on without it.
//
// cases.log.gz: TracerTest's ten cases, run by ref's own TracerRig (TracerConf's copies, the Tracer replaced by a
// recording proxy); TracerTest's 180 assertions pass on ref's side when the fixture is made.
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <type_traits>
#include <memory>
#include <string>
#include <vector>

#include "base/alloc_guard.h"
#include "harness.h"
#include "statefp.h"
#include "units/file_store.h"
#include "units/mem_system.h"
#include "units/tracer.h"

using units::tracer::FileStore;
using units::tracer::MemSystem;
using units::tracer::Tracer;

namespace {

const char* const kObsNames[Tracer::OBS_N] = {
    "errors", "running", "stopped", "rowsDone", "busyUntil", "waitUntil", "statUntil", "presented", "expansionOffset",
    "latch", "rqOcc", "rqHalf", "eqOcc", "expDiscard", "q0ptr", "xferLeft", "codeN", "logN", "rows", "instrs", "iMiss",
    "dMiss", "writebacks", "lruHits", "lruMisses", "probeReads", "stat0", "stat1", "stat2", "stat3", "stat4", "stat5",
    "stat6", "stat7", "stall.ifetch", "stall.dmiss", "stall.bus", "stall.probe", "stall.advice", "stall.newexp",
    "stall.exp", "oq1.occ", "oq1.pushed", "oq2.occ", "oq2.pushed", "oq3.occ", "oq3.pushed", "errorLog", "violations",
    "recHash", "ev0", "ev1", "ev2", "ev3", "ev4", "ev5", "ev6", "ev7", "ev8", "ev9", "ev10"};
constexpr int kCounterLo = 26, kCounterHi = 40;  // stat[] and stalls[]: masked within a group, compared at its end

uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }
int64_t dec(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 10); }
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t a = 0;
    while (true) {
        size_t b = s.find(sep, a);
        v.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return v;
}

// A Tracer call under the allocation hook (D6: the cycle loop and every peripheral call allocate nothing).
template <class F>
auto guarded(F f) {
    sim::begin_run();
    if constexpr (std::is_void_v<decltype(f())>) {
        f();
        sim::end_run();
    } else {
        auto r = f();
        sim::end_run();
        return r;
    }
}

struct Rig {
    std::map<int64_t, std::string> fp_due;  // cycle -> ref's five unit hashes, compared after that cycle
    std::string fe;                         // the end's check: "c h0 .. h4"
    std::unique_ptr<FileStore> fs;
    std::unique_ptr<MemSystem> ms;
    std::unique_ptr<Tracer> tr;
    bool dead = false;
    std::map<int, std::vector<std::string>> dumps;  // dump k: its F line and page lines
};

uint64_t store_hash(const FileStore& fs) {
    uint64_t h = 17;
    for (int64_t a = fs.first(); a < fs.next_free(); a += FileStore::PAGE_UNITS) {
        h = h * 31 + uint64_t(a);
        const uint64_t* p = fs.raw_page(int32_t(a));
        for (int i = 0; i < FileStore::PAGE_WORDS; i++) h = h * 31 + p[i];
    }
    return h;
}

class Replay {
public:
    explicit Replay(const char* name) : name_(name) {}
    bool failed = false;
    int64_t groups = 0, cycles = 0, calls = 0, ubs = 0;

    void fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char msg[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        ::test::fail(__FILE__, __LINE__, "%s line %zu: %s", name_, line_no_, msg);
        failed = true;
    }

    void run(const std::string& text) {
        size_t a = 0;
        std::string pending_c;
        size_t pending_c_no = 0;
        while (a < text.size() && !failed) {
            size_t b = text.find('\n', a);
            if (b == std::string::npos) b = text.size();
            const std::string line = text.substr(a, b - a);
            a = b + 1;
            line_no_++;
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == 'c') {
                pending_c = line;
                pending_c_no = line_no_;
                continue;
            }
            if (line[0] == 'o') {
                if (pending_c.empty()) { fail("an o line without its group"); return; }
                const size_t here = line_no_;
                line_no_ = pending_c_no;
                group(pending_c, line);
                line_no_ = here;
                pending_c.clear();
                continue;
            }
            const auto f = split(line, ' ');
            Rig& r = rigs_[int(dec(f[1]))];
            if (f[0] == "N") {
                r.fs = nullptr;  // the store comes with dump 0
                r.tr = std::make_unique<Tracer>(int(dec(f[2])), int(dec(f[3])), 1 << 12);
                r.tr->cold_mem_on_state_write = true;
                cur_dump_ = nullptr;
            } else if (f[0] == "F") {
                cur_dump_ = &r.dumps[int(dec(f[2]))];
                cur_dump_->push_back(line);
            } else if (f[0] == "fp") {
                r.fp_due[dec(f[2])] = line.substr(line.find(' ', line.find(' ', 3) + 1) + 1);
            } else if (f[0] == "fe") {
                r.fe = line.substr(line.find(' ', 3) + 1);
            } else if (f[0] == "P" || f[0] == "Z") {
                if (!cur_dump_) { fail("a page line outside a dump"); return; }
                cur_dump_->push_back(line);
            } else {
                fail("an unknown line: %.40s", line.c_str());
                return;
            }
        }
        if (!pending_c.empty() && !failed) fail("a group without its observation at the end");
        for (auto& [id, r] : rigs_) {
            if (failed) return;
            if (!r.fp_due.empty() && !r.dead) { fail("rig %d: a fingerprint at cycle %lld never reached", id, (long long)r.fp_due.begin()->first); return; }
            if (!r.fe.empty() && !r.dead) {
                const size_t sp = r.fe.find(' ');
                if (!check_fp(r, dec(r.fe.substr(0, sp)), r.fe.substr(sp + 1))) return;
            }
        }
    }
    int64_t fps = 0;

private:
    // A store dump: dump 0 (the rig's first) builds the store, when the rig's first group runs (its pages follow its F
    // line); a later one (an F,k token) must agree with every page the store has and adds the pages the harness
    // allocated since.
    bool ensure_store(Rig& r) {
        if (r.fs) return true;
        auto it = r.dumps.find(0);
        if (it == r.dumps.end()) { fail("no store dump before the rig's first call"); return false; }
        const auto f = split(it->second[0], ' ');
        r.fs = std::make_unique<FileStore>(int32_t(dec(f[3])));
        r.ms = std::make_unique<MemSystem>(r.fs.get());
        r.tr->mem = r.ms.get();
        return load_dump(r, 0, /*fresh=*/true);
    }
    bool load_dump(Rig& r, int k, bool fresh) {
        auto it = r.dumps.find(k);
        if (it == r.dumps.end()) { fail("store dump %d not found", k); return false; }
        const auto& lines = it->second;
        const auto f = split(lines[0], ' ');
        const int64_t first = dec(f[3]), next = dec(f[4]);
        if (first != r.fs->first()) { fail("dump %d: first page 0x%llx, ours 0x%x", k, (long long)first, r.fs->first()); return false; }
        const int64_t old_next = fresh ? first : r.fs->next_free();
        r.fs->grow_to(next);
        int64_t expect = first;
        for (size_t i = 1; i < lines.size(); i++) {
            const auto p = split(lines[i], ' ');
            const int64_t addr = dec(p[2]);
            if (addr != expect) { fail("dump %d: page 0x%llx missing", k, (long long)expect); return false; }
            expect += FileStore::PAGE_UNITS;
            uint64_t page[FileStore::PAGE_WORDS] = {};
            if (p[0] == "P")
                for (int w = 0; w < FileStore::PAGE_WORDS; w++) page[w] = hx(p[size_t(3 + w)]);
            uint64_t* ours = r.fs->raw_page(int32_t(addr));
            if (addr < old_next) {
                for (int w = 0; w < FileStore::PAGE_WORDS; w++)
                    if (ours[w] != page[w]) {
                        fail("dump %d: page 0x%llx word %d: ref 0x%llx, ours 0x%llx", k, (long long)addr, w,
                             (unsigned long long)page[w], (unsigned long long)ours[w]);
                        return false;
                    }
            } else {
                std::memcpy(ours, page, sizeof page);
            }
        }
        if (expect != next) { fail("dump %d: pages end at 0x%llx, next free 0x%llx", k, (long long)expect, (long long)next); return false; }
        r.fs->track_sum(true);  // the dump wrote the pages directly: rescan, then keep it at every write-back
        return true;
    }

    // Runs `fn` in a child that must die by SIGABRT with `want` in its stderr ("undefined behaviour" for ref's
    // UndefinedBehavior, "FATAL" for its IllegalStateException).
    template <class Fn>
    bool expect_ub(Fn fn, const char* want = "undefined behaviour") {
        int pipefd[2];
        if (pipe(pipefd) != 0) { fail("pipe: %s", std::strerror(errno)); return false; }
        std::fflush(nullptr);
        const pid_t pid = fork();
        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], 2);
            fn();
            _exit(3);
        }
        close(pipefd[1]);
        std::string err;
        char buf[4096];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof buf)) > 0) err.append(buf, size_t(n));
        close(pipefd[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGABRT || err.find(want) == std::string::npos) {
            fail("a call ref stopped on (%s) was not FATAL here (status %d): %.200s", want, status, err.c_str());
            return false;
        }
        ubs++;
        return true;
    }

    // One token's call at cycle t (its group's cycle). Returns false on a mismatch.
    bool call(Rig& r, const std::string& tok0, int64_t t) {
        std::string tok = tok0;
        const bool ub = !tok.empty() && (tok.back() == '!' || tok.back() == '%');
        const char* want = !tok.empty() && tok.back() == '%' ? "FATAL" : "undefined behaviour";
        if (ub) tok.pop_back();
        std::string lhs = tok, res;
        const size_t eq = tok.find('=');
        if (eq != std::string::npos) { lhs = tok.substr(0, eq); res = tok.substr(eq + 1); }
        auto args = split(lhs, ',');
        std::string name = args[0];
        const size_t at = name.find('@');
        if (at != std::string::npos) { t += dec(name.substr(at + 1)); name = name.substr(0, at); }
        Tracer& tr = *r.tr;
        calls++;
        auto mismatch = [&](const std::string& ours) {
            fail("cycle %lld, %s: ref %s, ours %s", (long long)t, tok0.c_str(), res.c_str(), ours.c_str());
            return false;
        };
        char b[80];
        if (ub) {
            if (name == "st") { r.dead = true; return expect_ub([&] { tr.step(t); }, want); }
            if (name == "os") return expect_ub([&] { tr.out_start(t, int(dec(args[1])), dec(args[2])); });
            if (name == "a") return expect_ub([&] { tr.accept(t, int(dec(args[1])), uint32_t(hx(args[2])), int(dec(args[3])), hx(args[4]), hx(args[5])); tr.step(t + 2); tr.step(t + 3); });
            if (name == "u") { r.dead = true; return expect_ub([&] { tr.run(t); tr.step(t + 1); }); }
            fail("a UB token %s not handled", tok0.c_str());
            return false;
        }
        if (name == "st") { guarded([&] { tr.step(t); }); return true; }
        if (name == "u") { guarded([&] { tr.run(t); }); return true; }
        if (name == "sy") { guarded([&] { tr.sync(t); }); return true; }
        if (name == "a") {
            const int src = int(dec(args[1])), size = int(dec(args[3]));
            const uint32_t off = uint32_t(hx(args[2]));
            const uint64_t lo = hx(args[4]), hi = hx(args[5]);
            guarded([&] { tr.accept(t, src, off, size, lo, hi); });
            return true;
        }
        if (name == "ww") {
            const int w = int(dec(args[1]));
            const uint32_t x = uint32_t(hx(args[2]));
            const int v = guarded([&] { return tr.write_word(t, w, x); });
            return v == dec(res) ? true : mismatch(std::to_string(v));
        }
        if (name == "wr") {
            uint32_t v = 0;
            const int w = int(dec(args[1]));
            const bool def = guarded([&] { return tr.word(t, w, v); });
            std::snprintf(b, sizeof b, "%x", v);
            const std::string ours = def ? b : "-";
            return ours == res ? true : mismatch(ours);
        }
        if (name == "rn") { const int v = guarded([&] { return tr.running(t); }); return v == dec(res) ? true : mismatch(std::to_string(v)); }
        if (name == "er") { const int v = guarded([&] { return tr.errors(t); }); return uint64_t(uint32_t(v)) == hx(res) ? true : mismatch(std::to_string(v)); }
        if (name == "re") { const int v = guarded([&] { return tr.rows_ended(t); }); return v == dec(res) ? true : mismatch(std::to_string(v)); }
        if (name == "ev") {
            const uint32_t v = guarded([&] { return tr.events().value(t); });
            std::snprintf(b, sizeof b, "%x", v);
            return v == hx(res) ? true : mismatch(b);
        }
        if (name == "co") {
            const uint32_t bits = uint32_t(hx(args[1]));
            guarded([&] { return tr.consume_events(bits); });
            return true;
        }
        if (name == "wb") { const int n = guarded([&] { return tr.write_back_dirty_untimed(); }); return n == dec(res) ? true : mismatch(std::to_string(n)); }
        if (name == "os") {
            const int q = int(dec(args[1]));
            const int64_t n = dec(args[2]);
            guarded([&] { tr.out_start(t, q, n); });
            return true;
        }
        if (name == "or") { const int q = int(dec(args[1]));
            const int v = guarded([&] { return tr.out_ready(t, q); }); return v == dec(res) ? true : mismatch(std::to_string(v)); }
        if (name == "ot") {
            uint64_t w[2] = {};
            const int q = int(dec(args[1]));
            if (!tr.out_ready(t, q)) return mismatch("(nothing ready)");
            guarded([&] { tr.out_take(t, q, w); });
            std::snprintf(b, sizeof b, "%llx,%llx", (unsigned long long)w[0], (unsigned long long)w[1]);
            return res == b ? true : mismatch(b);
        }
        if (name == "H") {
            const uint64_t h = store_hash(*r.fs);
            std::snprintf(b, sizeof b, "%llx", (unsigned long long)h);
            return h == hx(res) ? true : mismatch(b);
        }
        if (name == "F") return load_dump(r, int(dec(args[1])), false);
        fail("an unknown token %s", tok0.c_str());
        return false;
    }

    // The five units' fingerprints at the end of cycle c against ref's (AUDIT-M3 6.6); tr.files kept incrementally
    // must equal its scan.
    bool check_fp(Rig& r, int64_t c, const std::string& want) {
        const auto h = split(want, ' ');
        if (h.size() != size_t(Tracer::FP_UNITS)) { fail("a fingerprint of %zu units", h.size()); return false; }
        if (r.fs->files_sum() != r.fs->scan_sum()) { fail("cycle %lld: tr.files kept incrementally differs from its scan", (long long)c); return false; }
        for (int u = 0; u < Tracer::FP_UNITS; u++) {
            fp::FpOut o;
            r.tr->fingerprint(u, o, c);
            if (o.done() != hx(h[size_t(u)])) {
                fail("cycle %lld: fingerprint unit %s: ref %s, ours %llx (dump both sides to find the word)", (long long)c,
                     Tracer::FP_NAMES[u], h[size_t(u)].c_str(), (unsigned long long)o.done());
                return false;
            }
        }
        fps++;
        return true;
    }

    bool compare_obs(Rig& r, const std::vector<std::string>& o, bool counters, int64_t t) {
        int64_t ours[Tracer::OBS_N];
        r.tr->observe(ours);
        for (int i = 0; i < Tracer::OBS_N; i++) {
            if (!counters && i >= kCounterLo && i <= kCounterHi) continue;
            const bool decimal = i >= Tracer::OBS_N - 11;
            const uint64_t want = decimal ? uint64_t(dec(o[size_t(2 + i)])) : hx(o[size_t(2 + i)]);
            if (uint64_t(ours[i]) != want) {
                fail("after cycle %lld: %s ref 0x%llx, ours 0x%llx", (long long)t, kObsNames[i], (unsigned long long)want,
                     (unsigned long long)ours[i]);
                return false;
            }
        }
        return true;
    }

    void group(const std::string& cline, const std::string& oline) {
        const auto c = split(cline, ' ');
        const auto o = split(oline, ' ');
        if (o.size() != size_t(2 + Tracer::OBS_N)) { fail("an observation of %zu fields", o.size() - 2); return; }
        Rig& r = rigs_[int(dec(c[1]))];
        if (!r.tr) { fail("a group for a Tracer never announced"); return; }
        if (r.dead) { fail("a group for a Tracer that stopped on UB"); return; }
        if (!ensure_store(r)) return;
        const int64_t t0 = dec(c[2]), n = dec(c[3]);
        const auto toks = split(c.size() > 4 ? c[4] : std::string(), ';');
        groups++;
        for (int64_t i = 0; i < n; i++) {
            for (const auto& tk : toks)
                if (!tk.empty() && !call(r, tk, t0 + i)) return;
            cycles++;
            if (r.dead) return;  // the step raised UB in ref: its state is no reference any more
            if (!r.fp_due.empty() && r.fp_due.begin()->first == t0 + i) {
                if (!check_fp(r, t0 + i, r.fp_due.begin()->second)) return;
                r.fp_due.erase(r.fp_due.begin());
            }
            if (!compare_obs(r, o, i == n - 1, t0 + i)) return;
        }
    }

    const char* name_;
    size_t line_no_ = 0;
    std::map<int, Rig> rigs_;
    std::vector<std::string>* cur_dump_ = nullptr;
};

std::string read_cmd(const std::string& cmd, bool* ok) {
    std::string r;
    FILE* f = popen(cmd.c_str(), "r");
    *ok = f != nullptr;
    if (!f) return r;
    char b[1 << 16];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) r.append(b, n);
    *ok = pclose(f) == 0;
    return r;
}
std::string read_gz(const std::string& path, bool* ok) { return read_cmd("gzip -dc " + path, ok); }

Replay replay_file(const std::string& path, const char* name) {
    bool ok = false;
    const std::string text = read_gz(path, &ok);
    Replay rp(name);
    if (!ok) { ::test::fail(__FILE__, __LINE__, "cannot read %s", path.c_str()); rp.failed = true; return rp; }
    rp.run(text);
    std::printf("  %s: %lld groups, %lld cycles, %lld calls, %lld fatal refusals, %lld fingerprints%s\n", name,
                (long long)rp.groups, (long long)rp.cycles, (long long)rp.calls, (long long)rp.ubs, (long long)rp.fps,
                rp.failed ? "" : ", all equal to ref");
    return rp;
}

}  // namespace

TEST(tracer_test_cases_equal_ref_every_cycle) {
    Replay rp = replay_file("tests/tracerconf/cases.log.gz", "cases.log.gz");
    CHECK(!rp.failed);
    CHECK(rp.cycles > 1000000);
    CHECK_EQ(rp.ubs, int64_t(3));
    CHECK(rp.fps > 1000);
}

// TracerConf's seeded guests (tracerconf.sh GENS): the mix family (loops, calls, every width, RVC, MUL, DIV and its
// advice, templates, errors that stop), the write buffer queued far past its depth (wb), and ref's ring overflow
// (wbover: FATAL in both at the same call).
#define GEN_FIXTURE(test_name, file, min_cycles, fatal)                       \
    TEST(test_name) {                                                         \
        Replay rp = replay_file("tests/tracerconf/" file, file);              \
        CHECK(!rp.failed);                                                    \
        CHECK(rp.cycles > (min_cycles));                                      \
        CHECK_EQ(rp.ubs, int64_t(fatal));                                     \
        CHECK(rp.fps > 10);                                                   \
    }
GEN_FIXTURE(tracerconf_mix_seed1, "mix-s1.log.gz", 10000, 0)
GEN_FIXTURE(tracerconf_mix_seed3, "mix-s3.log.gz", 10000, 0)
GEN_FIXTURE(tracerconf_mix_seed4, "mix-s4.log.gz", 3000, 0)
GEN_FIXTURE(tracerconf_mix_seed5_unimp, "mix-s5.log.gz", 5000, 0)
GEN_FIXTURE(tracerconf_mix_seed9_store_hole, "mix-s9.log.gz", 3000, 0)
GEN_FIXTURE(tracerconf_mix_seed16, "mix-s16.log.gz", 10000, 0)
GEN_FIXTURE(tracerconf_mix_seed21_range, "mix-s21.log.gz", 5000, 0)
GEN_FIXTURE(tracerconf_wb_seed1_buffer_past_depth, "wb-s1.log.gz", 10000, 0)
GEN_FIXTURE(tracerconf_wbover_seed1_ring_overflow, "wbover-s1.log.gz", 10000, 1)

// Long runs: TRACERCONF_LONG=DIR test_tracer long (tracerconf.sh SEEDS OUT writes them).
TEST(long_runs_from_tracerconf_long) {
    const char* dir = std::getenv("TRACERCONF_LONG");
    if (!dir) return;
    bool ok = false;
    const std::string list = read_cmd(std::string("ls ") + dir + "/*.log.gz", &ok);
    size_t a = 0, n = 0;
    while (a < list.size()) {
        size_t b = list.find('\n', a);
        if (b == std::string::npos) b = list.size();
        const std::string path = list.substr(a, b - a);
        a = b + 1;
        if (path.empty()) continue;
        Replay rp = replay_file(path, path.c_str());
        CHECK(!rp.failed);
        n++;
    }
    std::printf("  long runs: %zu logs, all equal to ref\n", n);
    CHECK(n > 0);
}
