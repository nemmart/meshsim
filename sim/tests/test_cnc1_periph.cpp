// C&C 1's peripheral (WORKER16, G7; units/cnc1_periph.h) against ref's Cnc1Peripheral, driven by the same seeded script
// (tests/ref/Cnc1Conf.java; fixtures tests/cnc1conf/, regenerated and checked by tests/ref/cnc1conf.sh --check):
//   - the chip lines as the chip delivers them: READY pulses and tiles' error lines as row words (Link(ready_latency)),
//     C&C 2's and 3's error lines (Link(error_latency)), C&C 1's own (Link(2): the register block's line word, put at
//     level 0 of t + 1, latency 1);
//   - C&C 1's core and slot as W15's slot drives a peripheral (AUDIT-M3 3.1): at level 0 of t, begin(t), the consume of
//     t - 1, the store of t - 1 (write_word(t - 1, ...)), then the loads sampling at t, then step(t), end(t);
//   - GO through the peripheral's own 64 Links (all must arrive in one cycle).
// The observations (every load's value, every GO's store cycle, the toast pin and the UART's bytes at the end) must equal
// ref's line for line, under the debug options too (shuffle, both orders, step-all). D33: ref's driver applies stores
// of words 40-45 one cycle later (Cnc1Conf's header).
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "portconf.h"
#include "units/cnc1_periph.h"

using namespace units;
using sim::Ctx;
using sim::Ports;
using sim::Wake;
using sim::Word;

namespace {

const std::string kDir = "tests/cnc1conf/";
constexpr unsigned kL = 8;                                     // ready_latency = error_latency (Config's defaults)

struct Ev { int64_t t; char kind; uint32_t a, b; };          // R k; E id,l; U l; S w,v; L w
struct Script { int64_t cycles = 0; std::vector<Ev> ev; };

Script parse(const std::string& text) {
    Script s;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.empty() || line[0] == '#') continue;
        char* q = nullptr;
        const int64_t t = std::strtoll(line.c_str(), &q, 10);
        s.cycles = t;
        while (*q == ' ') {
            q++;
            Ev ev{t, *q, 0, 0};
            q += 2;
            const int base = ev.kind == 'S' ? 10 : 10;
            ev.a = uint32_t(std::strtoul(q, &q, base));
            if (*q == ',') { q++; ev.b = uint32_t(std::strtoul(q, &q, ev.kind == 'S' ? 16 : 10)); }
            s.ev.push_back(ev);
        }
    }
    return s;
}

// The chip's side: the lines.
struct Lines {
    sim::LinkOut rows[8], cc_line[3];
    const Ev* ev; uint32_t n, i = 0;
    bool level[128] = {};
    Lines(Ports& p, const Ev* e, uint32_t count) : ev(e), n(count) {
        for (unsigned r = 0; r < 8; r++) p.out(rows[r], "row", 1);
        for (unsigned c = 0; c < 3; c++) p.out(cc_line[c], "cc-line", 1);
    }
    Wake step(Ctx& c) {
        const int64_t t = int64_t(c.now());
        uint64_t row[8] = {};
        for (; i < n && ev[i].t == t; i++) {
            const Ev& e = ev[i];
            if (e.kind == 'R') row[e.a / 8] |= 1ull << (e.a % 8) | 1ull << (8 + e.a % 8);
            else if (e.kind == 'E' && e.a < 64) { level[e.a] = e.b; row[e.a / 8] |= 1ull << (8 + e.a % 8); }
            else if (e.kind == 'E') c.put(cc_line[e.a - 64], Word(e.b) << 1);
            else if (e.kind == 'U') c.put(cc_line[0], Word(e.a) << 1);
        }
        for (unsigned r = 0; r < 8; r++) {
            if (!row[r]) continue;
            for (unsigned k = 0; k < 8; k++) if (level[8 * r + k]) row[r] |= 1ull << (16 + k);
            c.put(rows[r], row[r]);
        }
        return c.next();
    }
    uint64_t digest() const { return i; }
};

struct Obs { int64_t t; int w; uint32_t v; };                 // w -1: GO

// C&C 1's core and slot around the peripheral.
struct Stand {
    Cnc1Peripheral p;
    const Ev* ev; uint32_t n, i = 0;
    bool st_due = false; int64_t st_t = 0; uint32_t st_w = 0, st_v = 0;
    bool cons_due = false; uint32_t cons_bits = 0;
    Obs* obs; uint32_t cap, nobs = 0;
    Stand(Ports& pp, const Ev* e, uint32_t count, Obs* o, uint32_t c) : ev(e), n(count), obs(o), cap(c) { p.ports(pp); }
    void note(int64_t t, int w, uint32_t v) { if (nobs >= cap) std::abort(); obs[nobs++] = {t, w, v}; }
    Wake step(Ctx& c) {
        const int64_t t = int64_t(c.now());
        p.begin(c, t);
        if (cons_due) { p.consume_events(cons_bits); cons_due = false; }
        if (st_due) { p.write_word(st_t, int(st_w), st_v); st_due = false; }
        for (; i < n && ev[i].t == t; i++) {
            const Ev& e = ev[i];
            if (e.kind == 'S') {
                if (e.a == 7 || e.a < 32) std::abort();       // the script's stores are the peripheral's words
                st_due = true; st_t = t; st_w = e.a; st_v = e.b;
            } else if (e.kind == 'L') {
                uint32_t v = 0;
                if (e.a == 5 || e.a == 6) { p.sync(t); v = p.events().value(t); if (e.a == 5) { cons_due = true; cons_bits = v & 0x3F; } }
                else if (!p.word(t, int(e.a), v)) v = 0;
                note(t, int(e.a), v);
            }
        }
        p.step(t);
        p.end(c, t);
        return c.next();
    }
    uint64_t digest() const { int64_t d[Cnc1Peripheral::DIGEST_N]; p.digest_words(d); return sim::hash_fields(d, i, st_due, cons_due); }
};

struct GoObs {
    sim::LinkIn go[64];
    Obs* obs; uint32_t cap, nobs = 0; bool split = false;
    GoObs(Ports& p, Obs* o, uint32_t c) : obs(o), cap(c) { for (unsigned k = 0; k < 64; k++) p.in(go[k], "go", 0); }
    Wake step(Ctx& c) {
        unsigned got = 0;
        for (unsigned k = 0; k < 64; k++) if (c.ready(go[k])) { c.take(go[k]); got++; }
        if (got && got != 64) split = true;
        if (got) { if (nobs >= cap) std::abort(); obs[nobs++] = {int64_t(c.now()) - 2, -1, 0}; }
        return Wake::none();
    }
    uint64_t digest() const { return nobs; }
};

std::string run(const Script& s, sim::Options opt) {
    sim::Config cfg(opt);
    const uint32_t cap = uint32_t(s.cycles) + 16;
    std::vector<Obs> lo(cap), go(cap);
    auto& ln = cfg.add<Lines>("lines", s.ev.data(), uint32_t(s.ev.size()));
    auto& st = cfg.add<Stand>("cc1.slot", s.ev.data(), uint32_t(s.ev.size()), lo.data(), cap);
    auto& gb = cfg.add<GoObs>("go", go.data(), cap);
    for (unsigned r = 0; r < 8; r++) cfg.link(ln.rows[r], st.p.rows[r], kL);
    cfg.link(ln.cc_line[0], st.p.cc_line[0], 2);
    for (unsigned c = 1; c < 3; c++) cfg.link(ln.cc_line[c], st.p.cc_line[c], kL);
    for (unsigned k = 0; k < 64; k++) cfg.link(st.p.go[k], gb.go[k], 1);
    sim::Sim sim = cfg.build();
    sim.run(uint64_t(s.cycles) + 4);
    const Stand& sp = sim.unit<Stand>("cc1.slot");
    const GoObs& g = sim.unit<GoObs>("go");
    if (g.split) return "GO's 64 links arrived in different cycles\n";
    std::vector<Obs> all(lo.begin(), lo.begin() + sp.nobs);
    all.insert(all.end(), go.begin(), go.begin() + g.nobs);
    std::stable_sort(all.begin(), all.end(), [](const Obs& a, const Obs& b) { return a.t < b.t; });
    std::string out;
    char b[96];
    for (const Obs& o : all) {
        if (o.t > s.cycles) continue;
        if (o.w < 0) std::snprintf(b, sizeof b, "%" PRId64 " GO\n", o.t);
        else std::snprintf(b, sizeof b, "%" PRId64 " L:%d=%x\n", o.t, o.w, o.v);
        out += b;
    }
    std::string hex;
    for (unsigned k = 0; k < sp.p.serial_n && k < Cnc1Peripheral::kSerial; k++) { std::snprintf(b, sizeof b, "%02x", uint8_t(sp.p.serial_out[k])); hex += b; }
    std::snprintf(b, sizeof b, "END toast %d at %" PRId64 " gos %" PRIu64 " serial ", sp.p.toast ? 1 : 0, sp.p.toast_at, sp.p.gos);
    out += b + (hex.empty() ? std::string("-") : hex) + "\n";
    return out;
}

std::string body(const std::string& f) {
    std::string r;
    size_t p = 0;
    while (p < f.size()) {
        size_t e = f.find('\n', p);
        if (e == std::string::npos) e = f.size();
        if (f[p] != '#') r += f.substr(p, e - p + 1);
        p = e + 1;
    }
    return r;
}

bool same(const std::string& name, const std::string& got, const std::string& want) {
    if (got == want) return true;
    size_t p = 0, line = 1;
    while (p < got.size() && p < want.size() && got[p] == want[p]) { if (got[p] == '\n') line++; p++; }
    size_t a = got.rfind('\n', p), b = want.rfind('\n', p);
    a = a == std::string::npos ? 0 : a + 1; b = b == std::string::npos ? 0 : b + 1;
    std::fprintf(stderr, "%s: line %zu differs\n  ref: %s\n  c++: %s\n", name.c_str(), line,
                 want.substr(b, want.find('\n', b) - b).c_str(), got.substr(a, got.find('\n', a) - a).c_str());
    return false;
}

const char* kFixtures[] = {"s1", "s2", "s3"};

void conformance(sim::Options opt) {
    for (const char* name : kFixtures) {
        bool ok1 = false, ok2 = false;
        std::string script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok1);
        std::string ref = body(portconf::read_command("gzip -dc " + kDir + name + ".obs.gz", &ok2));
        if (!ok1 || !ok2 || ref.empty()) { ::test::fail(__FILE__, __LINE__, "fixture %s missing", name); return; }
        if (!same(name, run(parse(script), opt), ref)) { ::test::fail(__FILE__, __LINE__, "%s differs from ref", name); return; }
    }
}

}  // namespace

TEST(cnc1_periph_conformance_with_ref) { conformance({}); }

TEST(cnc1_periph_conformance_under_debug_options) {
    sim::Options a; a.shuffle_seed = 7; conformance(a);
    sim::Options b; b.order = sim::Options::Order::instance_major; conformance(b);
    sim::Options c; c.step_all = true; conformance(c);
}

TEST(cnc1_periph_identity_and_window_rules) {
    Cnc1Peripheral p;
    CHECK_EQ(p.type(), 1); CHECK_EQ(p.params(), 1);
    uint32_t v = 7;
    CHECK(!p.word(0, 46, v));                                  // undefined
    CHECK_EQ(p.write_word(0, 35, 1), 0);                       // read-only
    CHECK_EQ(p.write_word(0, 60, 1), -1);                      // undefined
    CHECK(p.events().value(1) >> Cnc1Peripheral::EV_UART_TX & 1);   // TX not full at reset (live)
    CHECK(p.idle(0));
    for (int k = 0; k < 16; k++) CHECK_EQ(p.write_word(0, Cnc1Peripheral::UART_TX, uint32_t(k)), 1);
    CHECK(p.store_stalls(1, Cnc1Peripheral::UART_TX));
    CHECK(!p.idle(1));
    CcStub s;
    CHECK_EQ(s.type(), 2); CHECK_EQ(s.params(), 2); CHECK(s.idle(0)); CHECK(!s.running(0)); CHECK_EQ(s.errors(0), 0);
    s.accept(0, 0, 0, 2, 0, 0);
    CHECK_EQ(s.violation_count(), 1u);
    CHECK_EQ(s.write_word(0, 40, 1), -1);
}
