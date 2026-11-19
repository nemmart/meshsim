// test_reset.cpp: an ASIC comes out of reset in the middle of a running chain. Built on hostlink.h alone, for the
// specification's 28.14: "its neighbours each raise their error, and the link comes back up".
//
//   build/test_reset          150 runs of each kind, 1,380 in all: about 9 minutes. Prints a table
//   build/test_reset N        N runs of each kind (make check runs a few)
//
// A host and n ASICs under traffic on both streams and replies from every ASIC, with or without noise. At a random
// cycle one ASIC (or every ASIC at once) is reset: held for a number of cycles, in which it is not clocked and
// drives zeros, then Node::power_on(), which is the chip's reset: both link ends, the forwarding buffers, the
// queues. The cycle is random, so the reset cuts packets in the middle, in both directions. Checked:
//   1. no end has an error before the reset;
//   2. after it, each neighbour that was not itself reset has its error up on the link to the reset ASIC, its cause
//      "the other end" and no other;
//   3. no other end has: not the reset ASIC's own, not a link further away;
//   4. every link is in step again, and stays so, and no error comes again;
//   5. words pushed after the links are up again all arrive, at every ASIC and at the host;
//   6. at every taker the words of a source come in order, none twice; above the reset ASIC none is missing.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "hostlink.h"

using namespace hostlink;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; if (failures < 40) { printf("FAILED: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)

struct Rng {
    uint64_t s;
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    unsigned below(unsigned n) { return unsigned(next() % n); }
    bool chance(double p) { return double(next() >> 11) / double(1ull << 53) < p; }
};

static Word made(unsigned source, uint64_t n) {
    Word w = {};
    w.byte[0] = uint8_t(source);
    memcpy(w.byte + 1, &n, 8);
    for (unsigned i = 9; i < WORD_BYTES; i++) w.byte[i] = uint8_t(n * 7 + i + source);
    return w;
}
static uint64_t number_of(const Word& w) { uint64_t n; memcpy(&n, w.byte + 1, 8); return n; }
static bool is(const Word& w, unsigned source) { const Word x = made(source, number_of(w)); return memcmp(w.byte, x.byte, WORD_BYTES) == 0; }

struct Source {
    unsigned id; uint64_t pushed = 0;
    explicit Source(unsigned which) : id(which) {}
    template <class Push> bool packet(unsigned len, Push push) {
        Word w[MAX_WORDS];
        for (unsigned i = 0; i < len; i++) w[i] = made(id, pushed + i);
        if (!push(w, len)) return false;
        pushed += len;
        return true;
    }
};
struct Sink {
    bool any = false; uint64_t last = 0, taken = 0, missing = 0;
    void word(const Source& from, const Word& w, const char* what, unsigned at) {
        CHECK(is(w, from.id), "%s %u: a word that source %u never pushed", what, at, from.id);
        const uint64_t n = number_of(w);
        CHECK(!any || n > last, "%s %u: source %u's word %llu after its word %llu", what, at, from.id, (unsigned long long)n, (unsigned long long)last);
        missing += any ? n - last - 1 : n;
        any = true; last = n; taken++;
    }
};

struct Result { unsigned to_error_up = 0, to_error_down = 0, to_step = 0; uint64_t lost_words = 0; bool first_heard_second = false; };

// which: the ASIC that is reset, or -1 for every ASIC at once. again: 0 none; 1 the same ASIC is reset a second
// time, up to 4,000 cycles after the first release (in the middle of its links' starting again); 2 the ASIC below it
// is reset then instead.
static Result run(uint64_t seed, unsigned n, int which, unsigned hold, double noise, int again = 0) {
    Rng rng{seed * 0x9E3779B97F4A7C15ull + 11};
    End host;
    std::vector<Node> node;
    for (unsigned i = 0; i < n; i++) node.emplace_back(i + 1 < n);
    std::vector<uint8_t> down(n, 0), up(n, 0);                        // wire i: between ASIC i and what is above it
    Source hsrc[2] = {Source(100), Source(101)};
    std::vector<Source> nsrc; for (unsigned i = 0; i < n; i++) nsrc.push_back(Source(i));
    std::vector<Sink> got[2] = {std::vector<Sink>(n), std::vector<Sink>(n)}, at_host(n);
    const unsigned t0 = 20000 + rng.below(100000), release = t0 + hold;
    const int second = again == 1 ? which : again == 2 ? which + 1 : -9;                 // the ASIC of the second reset
    const unsigned t1 = again ? release + rng.below(4001) : 0, release1 = again ? t1 + rng.below(2001) : 0;
    const unsigned busy = (again ? release1 : release) + 60000, all = busy + 120000;
    auto is_reset = [&](unsigned i) { return which < 0 || unsigned(which) == i || int(i) == second; };
    auto held = [&](unsigned i, unsigned t) {
        return ((which < 0 || unsigned(which) == i) && t >= t0 && t < release) || (int(i) == second && t >= t1 && t < release1);
    };
    Result res;
    char tag[112]; snprintf(tag, sizeof tag, "seed %llu, %u ASICs, reset %d, hold %u, noise %g, again %d", (unsigned long long)seed, n, which, hold, noise, again);
    uint64_t final_from[2] = {}; std::vector<uint64_t> final_reply(n, 0);
    unsigned final_left = 0;

    auto errors = [&](unsigned i, bool below) { return below ? node[i].below().error : node[i].above().error; };
    auto in_step_all = [&]() {
        if (!host.status().in_step) return false;
        for (unsigned i = 0; i < n; i++) if (!node[i].above().in_step || (i + 1 < n && !node[i].below().in_step)) return false;
        return true;
    };

    for (unsigned t = 0; t < all; t++) {
        if (t == release) for (unsigned i = 0; i < n; i++) if (which < 0 || unsigned(which) == i) node[i].power_on();
        if (again && t == release1) node[unsigned(second)].power_on();
        std::vector<uint8_t> nd(n, 0), nu(n, 0);
        nd[0] = host.clock(up[0]);
        for (unsigned i = 0; i < n; i++) {
            if (held(i, t)) continue;                                 // in reset: not clocked, and its lines are zero
            const Node::Bytes b = node[i].clock(down[i], i + 1 < n ? up[i + 1] : 0);
            nu[i] = b.up; if (i + 1 < n) nd[i + 1] = b.down;
        }
        const bool noisy_now = t < busy && noise > 0;
        for (unsigned i = 0; i < n; i++) {
            down[i] = noisy_now && rng.chance(noise) ? uint8_t(nd[i] ^ (1 + rng.below(255))) : nd[i];
            up[i] = noisy_now && rng.chance(noise) ? uint8_t(nu[i] ^ (1 + rng.below(255))) : nu[i];
        }

        // 1. Nothing before the reset.
        if (t == t0 - 1) {
            CHECK(!host.status().error, "%s: the host has an error before the reset", tag);
            for (unsigned i = 0; i < n; i++) CHECK(!errors(i, false) && !errors(i, true), "%s: ASIC %u has an error before the reset", tag, i);
        }
        // How long: the neighbours' errors, and every link in step, from the release.
        if (t >= release) {
            const int first = which < 0 ? 0 : which, last = which < 0 ? int(n) - 1 : again == 2 ? second : which;
            const bool up_err = first == 0 ? host.status().error : errors(unsigned(first - 1), true);
            if (up_err && !res.to_error_up) res.to_error_up = t - release + 1;
            if (unsigned(last) + 1 < n && errors(unsigned(last + 1), false) && !res.to_error_down) res.to_error_down = t - release + 1;
            if (!res.to_step && t > release + 16 && in_step_all()) res.to_step = t - release + 1;
        }

        // The traffic.
        if (t < busy) {
            // Under what the line carries: 0.21 bytes a cycle on each stream down, 0.32 in all up.
            for (unsigned s = 0; s < 2; s++) if (rng.chance(0.0008)) hsrc[s].packet(1 + rng.below(MAX_WORDS), [&](const Word* w, unsigned k) { return host.push(Stream(s), w, k); });
            for (unsigned i = 0; i < n; i++) if (!held(i, t) && rng.chance(0.0012 / n)) nsrc[i].packet(1 + rng.below(MAX_WORDS), [&](const Word* w, unsigned k) { return node[i].push(w, k); });
        } else if (t == busy) {
            // 2, 3. The errors, at the neighbours and nowhere else. Then cleared, to see that none comes again.
            for (unsigned i = 0; i < n; i++) {
                const bool want_above = !is_reset(i) && i > 0 && is_reset(i - 1);          // its upper link is to a reset ASIC
                const bool want_below = !is_reset(i) && i + 1 < n && is_reset(i + 1);
                CHECK(errors(i, false) == want_above, "%s: ASIC %u's upper end: error %d, expected %d", tag, i, errors(i, false), want_above);
                for (const LinkStatus& st : {node[i].above(), node[i].below()})
                    CHECK(!st.made_wrong && !st.not_kept && st.other_end == st.error, "%s: ASIC %u: an error whose cause is not the other end's starting again", tag, i);
                // Two neighbours reset one after the other: the first may be up again, or not, when it hears the second.
                if (again == 2 && int(i) == which) res.first_heard_second = errors(i, true);
                else CHECK(errors(i, true) == want_below, "%s: ASIC %u's lower end: error %d, expected %d", tag, i, errors(i, true), want_below);
                node[i].clear_error();
            }
            CHECK(host.status().error == is_reset(0), "%s: the host's end: error %d, expected %d", tag, host.status().error, is_reset(0));
            CHECK(!host.status().made_wrong && !host.status().not_kept, "%s: the host's end: a cause that is not the other end's", tag);
            host.clear_error();
            for (unsigned s = 0; s < 2; s++) final_from[s] = hsrc[s].pushed;
            for (unsigned i = 0; i < n; i++) final_reply[i] = nsrc[i].pushed;
            final_left = 3;
        }
        if (t >= busy && final_left && t % 2000 == 0) {              // 5. words pushed once the noise is over
            final_left--;
            for (unsigned s = 0; s < 2; s++) CHECK(hsrc[s].packet(MAX_WORDS, [&](const Word* w, unsigned k) { return host.push(Stream(s), w, k); }), "%s: the host's queue is still full", tag);
            for (unsigned i = 0; i < n; i++) CHECK(nsrc[i].packet(7, [&](const Word* w, unsigned k) { return node[i].push(w, k); }), "%s: ASIC %u's queue is still full", tag, i);
        }
        for (unsigned i = 0; i < n; i++) {
            if (held(i, t)) continue;
            for (unsigned s = 0; s < 2; s++) { Word w; while (node[i].take(Stream(s), w)) got[s][i].word(hsrc[s], w, "the host's words at ASIC", i); }
        }
        for (unsigned hops = 0; hops < n; hops++) { Word w; while (host.take_from(hops, w)) at_host[hops].word(nsrc[hops], w, "at the host, the words of ASIC", hops); }
    }

    // 2. The errors came at all.
    {
        const int first = which < 0 ? 0 : which, last = which < 0 ? int(n) - 1 : again == 2 ? second : which;
        CHECK(res.to_error_up, "%s: the neighbour above raised no error", tag);
        if (unsigned(last) + 1 < n) CHECK(res.to_error_down, "%s: the neighbour below raised no error", tag);
        (void)first;
    }
    // 4. In step, and no error since they were cleared.
    CHECK(in_step_all(), "%s: a link is not in step at the end", tag);
    CHECK(!host.status().error, "%s: the host raised an error again", tag);
    for (unsigned i = 0; i < n; i++) CHECK(!errors(i, false) && !errors(i, true), "%s: ASIC %u raised an error again", tag, i);
    // 5, 6. What arrived.
    const unsigned first_reset = which < 0 ? 0 : unsigned(which);
    for (unsigned i = 0; i < n; i++) {
        for (unsigned s = 0; s < 2; s++) {
            CHECK(hsrc[s].pushed > final_from[s] && got[s][i].any && got[s][i].last + 1 == hsrc[s].pushed, "%s: ASIC %u did not get the host's last words on stream %u", tag, i, s);
            if (i < first_reset) CHECK(got[s][i].missing == 0 && got[s][i].taken == hsrc[s].pushed, "%s: ASIC %u, above the reset, missed %llu of the host's words", tag, i, (unsigned long long)got[s][i].missing);
            res.lost_words += hsrc[s].pushed - got[s][i].taken;
        }
        CHECK(nsrc[i].pushed > final_reply[i] && at_host[i].any && at_host[i].last + 1 == nsrc[i].pushed, "%s: the host did not get ASIC %u's last words", tag, i);
        if (i < first_reset) CHECK(at_host[i].missing == 0 && at_host[i].taken == nsrc[i].pushed, "%s: the host missed %llu words of ASIC %u, above the reset", tag, (unsigned long long)at_host[i].missing, i);
        res.lost_words += nsrc[i].pushed - at_host[i].taken;
    }
    return res;
}

int main(int argc, char** argv) {
    const unsigned SEEDS = argc == 2 && atoi(argv[1]) > 0 ? unsigned(atoi(argv[1])) : 150;
    struct Set { const char* what; unsigned n; int which; unsigned hold; double noise; unsigned seeds; int again; };
    const Set sets[] = {
        {"the middle ASIC of 3, no hold, no noise", 3, 1, 0, 0, SEEDS, 0},
        {"the middle ASIC of 3, held up to 6,000 cycles, no noise", 3, 1, 6000, 0, SEEDS, 0},
        {"the middle ASIC of 3, held up to 6,000 cycles, noise", 3, 1, 6000, 2e-4, SEEDS, 0},
        {"the first ASIC of 3 (its upper neighbour is the host), noise", 3, 0, 6000, 2e-4, SEEDS, 0},
        {"the last ASIC of 3 (no neighbour below), noise", 3, 2, 6000, 2e-4, SEEDS, 0},
        {"an ASIC of 4, any, noise", 4, -2, 6000, 2e-4, SEEDS, 0},
        {"an ASIC of 16, any, noise", 16, -2, 6000, 1e-4, SEEDS < 5 ? 1 : SEEDS / 5, 0},
        {"every ASIC of 4 at once, noise", 4, -1, 6000, 2e-4, SEEDS, 0},
        {"the second ASIC of 4, reset again within 4,000 cycles, noise", 4, 1, 6000, 2e-4, SEEDS, 1},
        {"the second ASIC of 4, then the third within 4,000 cycles, noise", 4, 1, 6000, 2e-4, SEEDS, 2},
    };
    printf("| what is reset | runs | the neighbour above raises its error, cycles from the release | the neighbour below | every link in step again | words that did not arrive, a run |\n|---|---|---|---|---|---|\n");
    uint64_t seed = 0;
    unsigned total = 0;
    for (const Set& s : sets) {
        unsigned up_most = 0, up_least = ~0u, down_most = 0, down_least = ~0u, step_most = 0, heard = 0; uint64_t lost = 0, lost_most = 0;
        for (unsigned k = 0; k < s.seeds; k++) {
            seed++;
            Rng pick{seed * 77 + 5};
            const int which = s.which == -2 ? int(pick.below(s.n)) : s.which;
            const unsigned hold = s.hold ? pick.below(s.hold + 1) : 0;
            const Result r = run(seed, s.n, which, hold, s.noise, s.again);
            up_most = std::max(up_most, r.to_error_up); up_least = std::min(up_least, r.to_error_up);
            if (r.to_error_down) { down_most = std::max(down_most, r.to_error_down); down_least = std::min(down_least, r.to_error_down); }
            step_most = std::max(step_most, r.to_step); lost += r.lost_words; lost_most = std::max(lost_most, r.lost_words);
            heard += r.first_heard_second;
            total++;
        }
        char dn[48] = "none below";
        if (down_most) snprintf(dn, sizeof dn, "%u to %u", down_least, down_most);
        printf("| %s | %u | %u to %u | %s | %u at most | %llu on average, %llu at most |\n", s.what, s.seeds, up_least, up_most, dn, step_most,
               (unsigned long long)(lost / s.seeds), (unsigned long long)lost_most);
        if (s.again == 2) printf("\n- Of those %u runs, in %u the ASIC reset first had its own error up afterwards: it was up again when it heard the second.\n\n", s.seeds, heard);
        fflush(stdout);
    }
    printf("\ntest_reset: %u runs; %d checks failed\n", total, failures);
    if (!failures) printf("test_reset: ok\n");
    return failures ? 1 : 0;
}
