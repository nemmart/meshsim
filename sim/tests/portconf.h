// Port conformance, the C++ side (sim/src/PORTS.md section 10). Runs a PortConfGen script on the framework's
// port of the same kind and writes the observation lines PortConfDriver writes on ref's bus class; the two must be
// identical. One unit owns both ends of the port, so the script's ops run in their written order within each cycle, as in
// the Java driver; the port's semantics make every observation independent of that order anyway (the debug shuffle tests
// that separately). The queries that are observers rather than unit operations (a link's in-flight count, a fifo's
// overflow count and first cycle, a hold's refusals) read the engine through TestingAccess.
//
// The reserved link (rlink, WordBackpressureDelayLine) is M2's (PORTS.md 2a): its scripts are reported as skipped.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"

namespace sim {
struct TestingAccess {
    static Engine& engine(Ctx& c) { return *c.e_; }
};
}  // namespace sim

namespace portconf {
using namespace sim;

enum Kind { LINK, FIFO, CREDIT, HOLD, REG, RLINK };

struct Op {
    char c;
    Word w[3];
};
struct Script {
    Kind kind = LINK;
    std::string kind_text;
    unsigned p1 = 0, p2 = 0;
    uint64_t cycles = 0;
    std::vector<Op> ops;
    std::vector<uint32_t> first;   // per cycle t (from 1), its ops are [first[t - 1], first[t])
};

// Parses a script (the '#' lines and one line per cycle). Returns "" or the error.
inline std::string parse(const std::string& text, Script& s) {
    size_t pos = 0;
    uint64_t expect = 1;
    s.first.push_back(0);
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.rfind("# kind ", 0) == 0) {
                char k[16] = {};
                int n = std::sscanf(line.c_str() + 7, "%15s %u %u", k, &s.p1, &s.p2);
                s.kind_text = line.substr(7);
                std::string ks = k;
                if (ks == "link") s.kind = LINK; else if (ks == "fifo") s.kind = FIFO; else if (ks == "credit") s.kind = CREDIT;
                else if (ks == "hold") s.kind = HOLD; else if (ks == "reg") s.kind = REG; else if (ks == "rlink") s.kind = RLINK;
                else return "unknown kind " + ks;
                (void)n;
            }
            continue;
        }
        const char* p = line.c_str();
        char* q;
        uint64_t t = std::strtoull(p, &q, 10);
        if (t != expect) return "cycle " + std::to_string(t) + ", expected " + std::to_string(expect);
        expect++;
        p = q;
        while (*p == ' ') {
            p++;
            Op op{};
            op.c = *p++;
            if (*p == ':') {
                p++;
                for (int k = 0; k < 3; k++) { op.w[k] = std::strtoull(p, &q, 16); p = q; if (*p == ',') p++; }
            }
            s.ops.push_back(op);
        }
        s.first.push_back(uint32_t(s.ops.size()));
    }
    s.cycles = expect - 1;
    return "";
}

// The unit: both ends of one port of the script's kind.
struct Conf {
    LinkOut lo; LinkIn li; FifoOut fo; FifoIn fi; CreditOut co; HoldOut ho; HoldIn hi; RegOut ro; RegIn ri;
    const Script* s;
    char* out;
    size_t cap, len = 0;
    unsigned link_cap = 0;
    Word last[3] = {};

    Conf(Ports& p, const Script* sc, char* buf, size_t bufcap) : s(sc), out(buf), cap(bufcap) {
        switch (s->kind) {
            case LINK:   // link L [C]: C given, the input declared with depth C - L (PORTS.md 2b)
                p.out(lo, "out", 3);
                if (s->p2) { if (s->p2 <= s->p1) FATAL("portconf: link %u %u: C must exceed L", s->p1, s->p2);
                             p.in(li, "in", 3, s->p2 - s->p1); link_cap = s->p2; }
                else { p.in(li, "in", 3); link_cap = s->p1 + 2; }
                break;
            case FIFO: p.out(fo, "out", 3); p.in(fi, "in", 3, s->p1); break;
            case CREDIT: p.out(co, "out", 3); p.in_credit(fi, "in", 3, s->p1); break;
            case HOLD: p.out(ho, "out", 3); p.in(hi, "in", 3); break;
            case REG: p.out(ro, "out", 3); p.in(ri, "in", 3); break;
            case RLINK: break;
        }
    }
    uint64_t digest() const { return hash_fields(len, last); }

    void emit(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list ap;
        va_start(ap, fmt);
        int n = std::vsnprintf(out + len, cap - len, fmt, ap);
        va_end(ap);
        if (n < 0 || size_t(n) >= cap - len) FATAL("portconf: output buffer full");
        len += size_t(n);
    }
    void words(char c, const Word* w) { emit(" %c:%llx,%llx,%llx", c, (unsigned long long)w[0], (unsigned long long)w[1], (unsigned long long)w[2]); }

    Wake step(Ctx& c) {
        const uint64_t t = c.now();
        if (t > s->cycles) return Wake::none();
        Engine& e = TestingAccess::engine(c);
        emit("%llu", (unsigned long long)t);
        for (uint32_t k = s->first[t - 1]; k < s->first[t]; k++) {
            const Op& op = s->ops[k];
            switch (op.c) {
                case 'T': case 'K': {
                    bool ready = s->kind == LINK ? c.ready(li) : s->kind == HOLD ? c.ready(hi) : s->kind == REG ? c.valid(ri) : c.ready(fi);
                    if (!ready) { emit(" %c:-", op.c); break; }
                    const Word* w = s->kind == LINK ? c.peek(li) : s->kind == HOLD ? c.peek(hi) : s->kind == REG ? c.read(ri) : c.peek(fi);
                    words(op.c, w);
                    if (op.c == 'T') {
                        if (s->kind == LINK) c.take(li); else if (s->kind == HOLD) c.take(hi); else if (s->kind != REG) c.take(fi);
                    }
                    break;
                }
                case 'P':
                    if (s->kind == FIFO) c.put(fo, op.w[0], op.w[1], op.w[2]);
                    else if (s->kind == REG) c.put(ro, op.w[0], op.w[1], op.w[2]);
                    else FATAL("portconf: P on this kind");
                    emit(" P");
                    break;
                case 'G': {
                    bool ok = s->kind == LINK ? e.links[li.idx].ring.count() + e.links[li.idx].inflight < link_cap : c.credits(co) > 0;
                    if (!ok) { emit(" G:-"); break; }
                    if (s->kind == LINK) c.put(lo, op.w[0], op.w[1], op.w[2]); else c.put(co, op.w[0], op.w[1], op.w[2]);
                    emit(" G");
                    break;
                }
                case 'R':
                    if (c.refused(ho)) { c.put(ho, last[0], last[1], last[2]); emit(" R"); break; }
                    last[0] = op.w[0]; last[1] = op.w[1]; last[2] = op.w[2];
                    c.put(ho, last[0], last[1], last[2]);
                    emit(" P");
                    break;
                default: FATAL("portconf: unknown op %c", op.c);
            }
        }
        switch (s->kind) {
            case LINK: emit(" Q:%d,%u\n", c.ready(li), e.links[li.idx].ring.count() + e.links[li.idx].inflight); break;
            case FIFO: {
                const FifoPort& f = e.fifos[fi.idx];
                emit(" Q:%d,%u,%u,%llu,%lld\n", c.ready(fi), c.size(fi), c.size_at_start(fi), (unsigned long long)f.overflows,
                     f.first_overflow == kNoCycle ? -1ll : (long long)f.first_overflow);
                break;
            }
            case CREDIT: emit(" Q:%d,%u,%d\n", c.ready(fi), c.size(fi), c.credits(co) > 0); break;
            case HOLD:
                emit(" Q:%d,%d,%d,%d,%llu\n", c.ready(hi), c.can_put(ho), c.refused(ho), c.held_at_start(ho),
                     (unsigned long long)e.holds[hi.idx].refusals);
                break;
            case REG: emit(" Q:%d\n", c.valid(ri)); break;
            case RLINK: break;
        }
        return t < s->cycles ? c.next() : Wake::none();
    }
};

// Runs a parsed script; returns its observations ("" with *skipped set for a kind not built in M1).
inline std::string run(const Script& s, bool* skipped = nullptr) {
    if (skipped) *skipped = s.kind == RLINK;
    if (s.kind == RLINK) return "";
    std::vector<char> buf(size_t(s.cycles) * 256 + 1024);
    Config cfg;
    auto& u = cfg.add<Conf>("conf", &s, buf.data(), buf.size());
    switch (s.kind) {
        case LINK: cfg.link(u.lo, u.li, s.p1); break;
        case FIFO: cfg.connect(u.fo, u.fi); break;
        case CREDIT: cfg.connect(u.co, u.fi); break;
        case HOLD: cfg.connect(u.ho, u.hi); break;
        case REG: cfg.connect(u.ro, u.ri); break;
        case RLINK: break;
    }
    Sim sim = cfg.build();
    sim.run(s.cycles);
    const Conf& done = sim.unit<Conf>("conf");
    return std::string(buf.data(), done.len);
}

// The observation lines of a file's text, without its '#' header.
inline std::string body(const std::string& text) {
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        if (text[pos] != '#') out.append(text, pos, end - pos + 1);
        pos = end + 1;
    }
    if (!out.empty() && out.back() != '\n') out += '\n';
    return out;
}

inline std::string read_command(const std::string& cmd, bool* ok) {
    std::string r;
    FILE* f = popen(cmd.c_str(), "r");
    *ok = f != nullptr;
    if (!f) return r;
    char b[65536];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) r.append(b, n);
    *ok = pclose(f) == 0;
    return r;
}

// The first differing line of two observation texts, or "".
inline std::string first_difference(const std::string& a, const std::string& b) {
    size_t i = 0, line = 1, start = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) { if (a[i] == '\n') { line++; start = i + 1; } i++; }
    if (i == a.size() && i == b.size()) return "";
    auto rest = [&](const std::string& s) { size_t e = s.find('\n', start); return s.substr(start, e == std::string::npos ? std::string::npos : e - start); };
    return "line " + std::to_string(line) + ":\n  ref: " + rest(b) + "\n  c++: " + rest(a);
}

}  // namespace portconf
