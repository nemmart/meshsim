// linksearch: the search that misses nothing, on one link of the shrunken model (NOTES.md, section 10).
//
// The model is the bench of bench.h with the link end's own code and small sizes. It runs cycle by cycle and stops
// where something happens (a packet starts or ends on a wire, a packet is judged, a header fails, a mode changes).
// At a stop every choice of the outside is tried:
//   the fate of a packet that has just started   whole; gone; its first byte zero; a bit of its header; a bit of its data
//                                                 (and, with --forge, a field changed under a header CRC made right again)
//   each source with packets left                 lets its next packet out, or not yet
//   each queue with words in it                   nothing taken; one word; all of them
//   an idle wire                                  a byte of noise, or none
//   (with --power) end 1                          starts again from power-on
// Budgets bound it: the packets of each source, the packets damaged, the bytes of noise, the headers forged.
//
// Every state reached is known by a 128-bit hash of what can still matter in it (Link1::canon) and is tried once,
// breadth first, so the first failure is a shortest one. In every state: the properties that hold at every cycle
// (the bench's); and, from that state, if nothing more goes wrong, everything arrives and the link comes to rest
// (Link1::quiet). A failure is written out as a case (cases/), and played back to see that it is the same failure.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "bench.h"

using namespace hl;

namespace {

struct Seen {                                  // the states seen: open addressing on the hash
    std::vector<Hash128> slots = std::vector<Hash128>(1 << 16, Hash128{0, 0});
    size_t used = 0;
    bool add(Hash128 h) {
        if (h.a == 0 && h.b == 0) h.b = 1;
        if (used * 2 >= slots.size()) {
            std::vector<Hash128> old(slots.size() * 2, Hash128{0, 0});
            old.swap(slots); used = 0;
            for (const Hash128& o : old) if (o.a || o.b) add(o);
        }
        for (size_t i = h.a & (slots.size() - 1);; i = (i + 1) & (slots.size() - 1)) {
            if (slots[i] == h) return false;
            if (slots[i].a == 0 && slots[i].b == 0) { slots[i] = h; used++; return true; }
        }
    }
};

struct Node { uint32_t parent; uint32_t first, count; };   // how it was reached: its events, in the pool

struct Options {
    std::string rules = "proposed";
    Config cfg;
    uint32_t damage = 1, noise = 0, forge = 0, power = 0, flips = 0;
    unsigned tick = 0;                         // also stop every so many cycles; 0 is never
    uint64_t bound = 20000, max_states = 0;
    std::string out;
    bool all = false, whole = false, quiet_check = true;
    unsigned drains = 3;                       // 2: nothing or all; 3: also one word
    std::string fates;                         // only these fates, by name, comma-separated; empty is all
};

}  // namespace

int main(int argc, char** argv) {
    Options o;
    o.cfg.p = Params::tiny(); o.cfg.queue_words = 4; o.cfg.words = 1; o.cfg.delay = 1;
    o.cfg.packets[0][FG] = 2; o.cfg.packets[1][FG] = 2;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto num = [&]() { return i + 1 < argc ? strtoull(argv[++i], nullptr, 0) : 0; };
        if (a == "--rules") o.rules = argv[++i];
        else if (a == "--packets") { for (auto& e : o.cfg.packets) for (auto& s : e) s = unsigned(num()); }
        else if (a == "--damage") o.damage = uint32_t(num());
        else if (a == "--noise") o.noise = uint32_t(num());
        else if (a == "--forge") o.forge = uint32_t(num());
        else if (a == "--power") o.power = uint32_t(num());
        else if (a == "--flip") o.flips = uint32_t(num());
        else if (a == "--images") o.cfg.images = true;
        else if (a == "--queue") o.cfg.queue_words = unsigned(num());
        else if (a == "--delay") o.cfg.delay = unsigned(num());
        else if (a == "--tick") o.tick = unsigned(num());
        else if (a == "--bound") o.bound = num();
        else if (a == "--max-states") o.max_states = num();
        else if (a == "--drains") o.drains = unsigned(num());
        else if (a == "--out") o.out = argv[++i];
        else if (a == "--fates") o.fates = std::string(",") + argv[++i] + ",";
        else if (a == "--all") o.all = true;
        else if (a == "--whole") o.whole = true;
        else if (a == "--no-quiet") o.quiet_check = false;
        else { printf("linksearch: unknown option %s\n", a.c_str()); return 2; }
    }
    std::string err;
    if (!apply_rules(o.rules, o.cfg.p, err)) { printf("linksearch: %s\n", err.c_str()); return 2; }

    Link1 root(o.cfg);
    root.budget[0] = o.damage; root.budget[1] = o.noise; root.budget[2] = o.forge;

    printf("linksearch: rules %s; packets %u %u %u %u; damage %u, noise %u, forge %u, power %u, flips %u; queue %u, delay %u, tick %u%s%s\n",
           o.rules.c_str(), o.cfg.packets[0][0], o.cfg.packets[0][1], o.cfg.packets[1][0], o.cfg.packets[1][1], o.damage, o.noise,
           o.forge, o.power, o.flips, o.cfg.queue_words, o.cfg.delay, o.tick, o.cfg.images ? "; words are headers' images" : "",
           o.whole ? "; states kept whole" : "");

    Seen seen;
    std::map<std::vector<uint8_t>, bool> seen_whole;
    std::vector<Node> nodes;
    std::vector<Event> pool;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> frontier;
    std::vector<uint32_t> power_left;          // by node: the power-ons still allowed (outside the bench's state, so in the hash by hand)
    std::map<std::string, uint64_t> failures;
    uint64_t tried = 0, quiet_runs = 0, quiet_cycles = 0, longest_quiet = 0, deepest = 0, failed_states = 0;
    bool first_written = false, cut_short = false;
    const auto t0 = std::chrono::steady_clock::now();

    auto known = [&](Link1& b, uint32_t pl) {
        if (o.whole) { auto v = b.whole(); v.push_back(uint8_t(pl)); return !seen_whole.emplace(std::move(v), true).second; }
        Hash128 h = b.canon(); h.a ^= uint64_t(pl) * 0x9E3779B97F4A7C15ull;
        return !seen.add(h);
    };
    auto write_case = [&](uint32_t node, const std::vector<Event>& last, const Link1& at, const std::string& bad) {
        Case c; c.cfg = o.cfg; c.cfg.p = Params::tiny();
        std::vector<Event> evs(last.rbegin(), last.rend());
        for (uint32_t n = node; n != 0; n = nodes[n].parent)
            for (uint32_t k = nodes[n].count; k-- > 0;) evs.push_back(pool[nodes[n].first + k]);
        c.events.assign(evs.rbegin(), evs.rend());
        c.quiet_at = at.t; c.bound = o.bound;
        std::string text = bad.substr(0, bad.find_first_of(":("));
        while (!text.empty() && text.back() == ' ') text.pop_back();
        c.runs.push_back(Run{o.rules, false, text});
        const std::string again = play(c, o.rules);
        printf("\nFAILED after %zu events, at cycle %llu: %s\n", c.events.size(), (unsigned long long)at.t, bad.c_str());
        printf("played back: %s\n", again == bad ? "the same failure" : ("DIFFERENT: " + again).c_str());
        printf("%s", case_text(c).c_str());
        if (!o.out.empty()) { std::ofstream f(o.out); f << "# found by linksearch --rules " << o.rules << "\n" << case_text(c); }
    };

    nodes.push_back(Node{0, 0, 0});
    power_left.push_back(o.power | o.flips << 8);
    known(root, o.power | o.flips << 8);
    frontier.emplace_back(0, root.save());
    Link1 b(o.cfg), q(o.cfg);
    const uint64_t cap = 16ull * (o.cfg.p.zeros + o.cfg.p.status_timer + 64);

    while (!frontier.empty()) {
        const uint32_t parent = frontier.front().first;
        const std::vector<uint8_t> state = std::move(frontier.front().second);
        frontier.pop_front();
        b.load(state);
        // The choices at this stop, as digits of a mixed-radix number.
        struct Dim { uint8_t what, a, b; std::vector<uint32_t> values; };
        std::vector<Dim> dims;
        for (unsigned w = 0; w < 2; w++) {
            if (!b.started[w]) continue;
            Dim d{0, uint8_t(w), 0, {uint32_t(Fate::WHOLE)}};
            if (b.budget[0]) {
                for (Fate f : {Fate::GONE, Fate::FIRST0, Fate::HEADER}) d.values.push_back(uint32_t(f));
                if (b.wire[w].len > HEADER_BYTES) d.values.push_back(uint32_t(Fate::DATA));
            }
            if (b.budget[2]) for (unsigned f = unsigned(Fate::FORGE_ACK); f < unsigned(Fate::COUNT); f++)
                if (Fate(f) != Fate::FORGE_ACK_DATA || b.wire[w].len > HEADER_BYTES) d.values.push_back(f);
            if (!o.fates.empty())
                std::erase_if(d.values, [&](uint32_t v) { return v != uint32_t(Fate::WHOLE) && o.fates.find(std::string(",") + fate_name(Fate(v)) + ",") == std::string::npos; });
            if (d.values.size() > 1) dims.push_back(d);
        }
        for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++) {
            if (b.src[e][s].avail < b.src[e][s].total && b.src[e][s].made == b.src[e][s].avail) dims.push_back(Dim{1, uint8_t(e), uint8_t(s), {0, 1}});
            if (b.fill[e][s] > 1 && o.drains >= 3) dims.push_back(Dim{2, uint8_t(e), uint8_t(s), {99, 1, 0}});
            else if (b.fill[e][s] > 0) dims.push_back(Dim{2, uint8_t(e), uint8_t(s), {99, 0}});
        }
        for (unsigned w = 0; w < 2; w++) if (b.budget[1] && b.wire[w].idle() && !b.started[w]) dims.push_back(Dim{3, uint8_t(w), 0, {0, 1}});
        if (power_left[parent] & 0xFF) dims.push_back(Dim{4, 1, 0, {0, 1}});
        if (power_left[parent] >> 8) for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++)
            if (b.end[e].kept(s)) dims.push_back(Dim{5, uint8_t(e), uint8_t(s), {0, 1}});

        std::vector<size_t> digit(dims.size(), 0);
        bool halt = false;
        for (bool more = true; more && !halt;) {
            b.load(state);
            std::vector<Event> evs;
            uint32_t pl = power_left[parent];
            uint32_t cost[3] = {0, 0, 0};          // this choice within the budgets?
            for (size_t i = 0; i < dims.size(); i++) {
                const uint32_t v = dims[i].values[digit[i]];
                if (dims[i].what == 0 && v != uint32_t(Fate::WHOLE)) cost[Fate(v) > Fate::DATA ? 2 : 0]++;
                if (dims[i].what == 3 && v) cost[1]++;
            }
            const bool affordable = cost[0] <= b.budget[0] && cost[1] <= b.budget[1] && cost[2] <= b.budget[2];
            if (affordable) {
            for (size_t i = 0; i < dims.size(); i++) {
                const Dim& d = dims[i];
                const uint32_t v = d.values[digit[i]];
                Event ev{}; ev.a = d.a; ev.b = d.b; ev.cycle = b.t;
                if (d.what == 0 && v != uint32_t(Fate::WHOLE)) {
                    b.fate(d.a, Fate(v));
                    b.budget[Fate(v) > Fate::DATA ? 2 : 0]--;
                    ev.what = Event::FATE; ev.cycle = b.wire[d.a].ordinal; ev.n = v; evs.push_back(ev);
                } else if (d.what == 1 && v) { b.offer(d.a, d.b); ev.what = Event::OFFER; evs.push_back(ev); }
                else if (d.what == 2 && v != 99) { b.drain(d.a, d.b, v); ev.what = Event::DRAIN; ev.n = v; evs.push_back(ev); }
                else if (d.what == 3 && v) { if (b.wire[d.a].noise()) { b.budget[1]--; ev.what = Event::NOISE; evs.push_back(ev); } }
                else if (d.what == 4 && v) { b.power[d.a] = true; pl--; ev.what = Event::POWER; evs.push_back(ev); }
                else if (d.what == 5 && v && (pl >> 8)) { b.flip(d.a, d.b); pl -= 0x100; ev.what = Event::FLIP; evs.push_back(ev); }
            }
            for (uint64_t n = 0; b.bad.empty(); ) {
                b.step();
                if (b.stop || ++n >= cap || (o.tick && n >= o.tick)) break;
            }
            tried++;
            std::string bad = b.bad;
            const bool fresh = bad.empty() && !known(b, pl);
            if (fresh && o.quiet_check) {
                q.load(b.save()); q.bad.clear();
                const uint64_t before = q.t;
                const long took = q.quiet(o.bound);
                quiet_runs++; quiet_cycles += q.t - before;
                if (took < 0) bad = q.bad; else if (uint64_t(took) > longest_quiet) longest_quiet = uint64_t(took);
            }
            if (!bad.empty()) {
                failed_states++;
                std::string kind = bad.substr(0, bad.find_first_of(":("));
                if (failures[kind]++ == 0 && (o.all || !first_written)) {
                    write_case(parent, evs, b, bad); first_written = true;
                }
                if (!o.all) { frontier.clear(); halt = true; }
            } else if (fresh) {
                nodes.push_back(Node{parent, uint32_t(pool.size()), uint32_t(evs.size())});
                pool.insert(pool.end(), evs.begin(), evs.end());
                power_left.push_back(pl);
                frontier.emplace_back(uint32_t(nodes.size() - 1), b.save());
                if (b.t > deepest) deepest = b.t;
                if (nodes.size() % 200000 == 0) {
                    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    printf("  %zu states, %zu waiting, %.0f s\n", nodes.size(), frontier.size(), sec); fflush(stdout);
                }
            }
            }
            more = false;
            for (size_t i = 0; i < dims.size(); i++) { if (++digit[i] < dims[i].values.size()) { more = true; break; } digit[i] = 0; }
            if (o.max_states && nodes.size() >= o.max_states) { frontier.clear(); more = false; cut_short = true; printf("stopped at --max-states: NOT complete\n"); }
        }
    }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("\n%zu states, %llu steps between them tried, the latest at cycle %llu; %llu runs to rest, the longest %llu cycles, %llu on average; %.1f s\n",
           nodes.size(), (unsigned long long)tried, (unsigned long long)deepest, (unsigned long long)quiet_runs,
           (unsigned long long)longest_quiet, (unsigned long long)(quiet_runs ? quiet_cycles / quiet_runs : 0), sec);
    if (failures.empty() && cut_short) printf("RESULT: no failure in the states tried, and NOT complete: there are more\n");
    else if (failures.empty()) printf("RESULT: every property held in every state\n");
    else { printf("RESULT: FAILED in %llu states:\n", (unsigned long long)failed_states); for (const auto& f : failures) printf("  %8llu  %s\n", (unsigned long long)f.second, f.first.c_str()); }
    return failures.empty() ? 0 : 1;
}
