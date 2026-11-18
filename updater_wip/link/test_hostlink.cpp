// test_hostlink.cpp: the interface of hostlink.h, used only as its header says. Each test is one of the things
// README.md, "Using the link end", says a user may rely on.
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "hostlink.h"

using namespace hostlink;

namespace {

int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAILED %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rng {
    uint64_t s;
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    unsigned below(unsigned n) { return unsigned(next() % n); }
    bool chance(double p) { return double(next() >> 11) / double(1ull << 53) < p; }
};

// A word that says where it is from and which it is.
Word made(unsigned source, uint64_t n) {
    Word w = {};
    w.byte[0] = uint8_t(source);
    memcpy(w.byte + 1, &n, 8);
    for (unsigned i = 9; i < WORD_BYTES; i++) w.byte[i] = uint8_t(n * 7 + i + source);
    return w;
}
bool is(const Word& w, unsigned source, uint64_t n) { const Word x = made(source, n); return memcmp(w.byte, x.byte, WORD_BYTES) == 0; }
uint64_t number_of(const Word& w) { uint64_t n; memcpy(&n, w.byte + 1, 8); return n; }

// A source of packets: words numbered from 0, in packets of random lengths.
struct Source {
    unsigned id; uint64_t pushed = 0; std::vector<uint8_t> ends;      // ends[n]: word n is its packet's last
    explicit Source(unsigned which) : id(which) {}
    template <class Push> bool packet(Rng& rng, Push push) {
        const unsigned len = 1 + rng.below(MAX_WORDS);
        Word w[MAX_WORDS];
        for (unsigned i = 0; i < len; i++) w[i] = made(id, pushed + i);
        if (!push(w, len)) return false;
        for (unsigned i = 0; i < len; i++) ends.push_back(i + 1 == len);
        pushed += len;
        return true;
    }
};
// What must come out: the source's words in order, each once, the packets' ends where they were.
struct Sink {
    uint64_t taken = 0;
    void word(const Source& from, const Word& w, bool last, const char* what) {
        CHECK(is(w, from.id, taken), "%s: word %llu is not the next of source %u", what, (unsigned long long)taken, from.id);
        CHECK(taken < from.ends.size() && bool(from.ends[taken]) == last, "%s: word %llu: a packet's end is not where it was pushed", what, (unsigned long long)taken);
        taken++;
    }
};

uint8_t noisy(Rng& rng, uint8_t b, double rate) { return rng.chance(rate) ? uint8_t(b ^ (1 + rng.below(255))) : b; }

// Two ends back to back, both streams both ways, noise on both wires for the first part.
void two_ends(uint64_t seed, double noise) {
    Rng rng{seed * 0x9E3779B97F4A7C15ull + 1};
    End e[2];
    uint8_t wire[2] = {};
    Source src[2][2] = {{Source(0), Source(1)}, {Source(2), Source(3)}};
    Sink sink[2][2];
    const unsigned busy = 300000, all = 600000;
    for (unsigned t = 0; t < all; t++) {
        const uint8_t out0 = e[0].clock(wire[1]), out1 = e[1].clock(wire[0]);
        wire[0] = t < busy ? noisy(rng, out0, noise) : out0;
        wire[1] = t < busy ? noisy(rng, out1, noise) : out1;
        for (unsigned i = 0; i < 2; i++) for (unsigned s = 0; s < 2; s++) {
            if (t < busy && rng.chance(0.004)) src[i][s].packet(rng, [&](const Word* w, unsigned n) { return e[i].push(Stream(s), w, n); });
            Word w; bool last = false;
            if (rng.chance(0.3)) while (e[i].take(Stream(s), w, nullptr, &last)) sink[i][s].word(src[1 - i][s], w, last, "two ends");
        }
    }
    for (unsigned i = 0; i < 2; i++) {
        for (unsigned s = 0; s < 2; s++) {
            Word w; bool last = false;
            while (e[i].take(Stream(s), w, nullptr, &last)) sink[i][s].word(src[1 - i][s], w, last, "two ends");
            CHECK(sink[i][s].taken == src[1 - i][s].pushed, "two ends, seed %llu: end %u stream %u took %llu of %llu words", (unsigned long long)seed, i, s,
                  (unsigned long long)sink[i][s].taken, (unsigned long long)src[1 - i][s].pushed);
        }
        CHECK(e[i].status().in_step && !e[i].status().error, "two ends, seed %llu: end %u in step %d, error %d", (unsigned long long)seed, i, e[i].status().in_step, e[i].status().error);
    }
    printf("two ends, seed %llu, noise %g a byte: %llu words one way, %llu the other, all taken in order\n", (unsigned long long)seed, noise,
           (unsigned long long)(src[0][0].pushed + src[0][1].pushed), (unsigned long long)(src[1][0].pushed + src[1][1].pushed));
}

// A host and n ASICs. Every ASIC gets the host's words on both streams; the host gets each ASIC's own words under
// that ASIC's hops. `deaf`: an ASIC that never takes its background words (-1: none).
void chain(uint64_t seed, unsigned n, double noise, int deaf) {
    Rng rng{seed * 0x9E3779B97F4A7C15ull + 7};
    End host;
    std::vector<Node> node;
    for (unsigned i = 0; i < n; i++) node.emplace_back(i + 1 < n);
    std::vector<uint8_t> down(n, 0), up(n, 0);                        // wire i: between ASIC i and what is above it
    Source hsrc[2] = {Source(100), Source(101)};
    std::vector<Source> nsrc; for (unsigned i = 0; i < n; i++) nsrc.push_back(Source(i));
    std::vector<Sink> got[2] = {std::vector<Sink>(n), std::vector<Sink>(n)}, at_host(n);
    const unsigned busy = 300000, all = 700000;
    uint64_t fg_after = 0;
    for (unsigned t = 0; t < all; t++) {
        std::vector<uint8_t> nd(n), nu(n);
        const uint8_t h = host.clock(up[0]);
        for (unsigned i = 0; i < n; i++) {
            const Node::Bytes b = node[i].clock(down[i], i + 1 < n ? up[i + 1] : 0);
            nu[i] = b.up; if (i + 1 < n) nd[i + 1] = b.down;
        }
        nd[0] = h;
        for (unsigned i = 0; i < n; i++) { down[i] = t < busy ? noisy(rng, nd[i], noise) : nd[i]; up[i] = t < busy ? noisy(rng, nu[i], noise) : nu[i]; }
        if (t < busy) {
            for (unsigned s = 0; s < 2; s++) if (rng.chance(0.003)) hsrc[s].packet(rng, [&](const Word* w, unsigned k) { return host.push(Stream(s), w, k); });
            for (unsigned i = 0; i < n; i++) if (rng.chance(0.002)) nsrc[i].packet(rng, [&](const Word* w, unsigned k) { return node[i].push(w, k); });
        } else if (deaf >= 0 && t == busy) {
            fg_after = hsrc[0].pushed;                                // with the background stuck, the foreground still goes
            for (unsigned k = 0; k < 4; k++) hsrc[0].packet(rng, [&](const Word* w, unsigned c) { return host.push(FOREGROUND, w, c); });
        }
        for (unsigned i = 0; i < n; i++) for (unsigned s = 0; s < 2; s++) {
            if (int(i) == deaf && s == BACKGROUND) continue;
            Word w; bool last = false;
            while (node[i].take(Stream(s), w, &last)) got[s][i].word(hsrc[s], w, last, "chain, at an ASIC");
        }
        for (unsigned hops = 0; hops < n; hops++) {
            Word w; bool last = false;
            while (host.take_from(hops, w, &last)) at_host[hops].word(nsrc[hops], w, last, "chain, at the host");
        }
    }
    for (unsigned i = 0; i < n; i++) {
        CHECK(got[0][i].taken == hsrc[0].pushed, "chain of %u, seed %llu: ASIC %u took %llu of %llu foreground words", n, (unsigned long long)seed, i,
              (unsigned long long)got[0][i].taken, (unsigned long long)hsrc[0].pushed);
        if (deaf < 0) CHECK(got[1][i].taken == hsrc[1].pushed, "chain of %u, seed %llu: ASIC %u took %llu of %llu background words", n, (unsigned long long)seed, i,
                            (unsigned long long)got[1][i].taken, (unsigned long long)hsrc[1].pushed);
        CHECK(at_host[i].taken == nsrc[i].pushed, "chain of %u, seed %llu: the host took %llu of ASIC %u's %llu words", n, (unsigned long long)seed,
              (unsigned long long)at_host[i].taken, i, (unsigned long long)nsrc[i].pushed);
        CHECK(!node[i].above().error && !node[i].below().error, "chain of %u, seed %llu: ASIC %u raised an error", n, (unsigned long long)seed, i);
    }
    if (deaf >= 0) {
        CHECK(hsrc[0].pushed > fg_after, "chain: no foreground packet could be pushed with the background stuck");
        CHECK(node[unsigned(deaf)].above().stop_said[BACKGROUND], "chain: the ASIC that takes no background words does not say stop");
        CHECK(host.status().stop_heard[BACKGROUND], "chain: the host has not heard stop on the background");
        CHECK(!host.status().stop_heard[FOREGROUND], "chain: the foreground is stopped too");
    }
    CHECK(host.waiting(FOREGROUND) == 0 && host.waiting(BACKGROUND) == 0, "chain: words left at the host that are no ASIC's");
    uint64_t replies = 0;
    for (const Source& x : nsrc) replies += x.pushed;
    printf("chain of %u, seed %llu, noise %g a byte%s: %llu foreground and %llu background words from the host, %llu words of replies\n", n, (unsigned long long)seed, noise,
           deaf >= 0 ? ", one ASIC never taking its background" : "", (unsigned long long)hsrc[0].pushed, (unsigned long long)hsrc[1].pushed, (unsigned long long)replies);
}

// One end powers on again under a running link: the other raises its error, and after that words still arrive in
// order, each once: some are missing, none is repeated.
void power_on_under() {
    Rng rng{99};
    End e[2];
    uint8_t wire[2] = {};
    Source src(5);
    uint64_t last_number = 0, taken = 0;
    bool any = false, error_seen = false;
    for (unsigned t = 0; t < 200000; t++) {
        const uint8_t out0 = e[0].clock(wire[1]), out1 = e[1].clock(wire[0]);
        wire[0] = out0; wire[1] = out1;
        if (t == 60000) e[1].power_on();
        if (t < 120000 && rng.chance(0.01)) src.packet(rng, [&](const Word* w, unsigned n) { return e[0].push(BACKGROUND, w, n); });
        if (e[0].status().error) { error_seen = true; CHECK(t > 60000, "power-on: an error before anything happened"); e[0].clear_error(); }
        Word w;
        while (e[1].take(BACKGROUND, w)) {
            const uint64_t n = number_of(w);
            CHECK(is(w, 5, n), "power-on: a word that was never pushed");
            CHECK(!any || n > last_number, "power-on: word %llu after word %llu", (unsigned long long)n, (unsigned long long)last_number);
            any = true; last_number = n; taken++;
        }
    }
    CHECK(error_seen, "power-on: the other end raised no error");
    CHECK(!e[0].status().error && e[0].status().in_step && e[1].status().in_step, "power-on: the link did not come up again");
    CHECK(any && last_number + 1 == src.pushed, "power-on: the last word pushed did not arrive");
    CHECK(taken < src.pushed, "power-on: nothing was lost, so this test tests nothing");
    printf("an end powered on under a running link: %llu of %llu words arrived, in order, none twice; the other end raised its error\n",
           (unsigned long long)taken, (unsigned long long)src.pushed);
}

void limits() {
    End e;
    Word w[MAX_WORDS + 1] = {};
    Word x;
    CHECK(!e.push(FOREGROUND, w, 0) && !e.push(FOREGROUND, w, MAX_WORDS + 1), "limits: a packet of 0 or of 33 words was taken");
    CHECK(!e.take(FOREGROUND, x) && !e.take_from(3, x) && e.waiting(BACKGROUND) == 0, "limits: a word from an empty end");
    const unsigned room = e.room(BACKGROUND);
    CHECK(room == Sizes().send_words, "limits: room %u", room);
    unsigned n = 0;
    while (e.push(BACKGROUND, w, MAX_WORDS)) n++;
    CHECK(n == room / MAX_WORDS && e.room(BACKGROUND) == room % MAX_WORDS && e.room(FOREGROUND) == room, "limits: %u packets pushed", n);
    Node last(false);
    CHECK(!last.below().in_step && !last.below().error, "limits: the last ASIC's lower link shows something");
    for (unsigned t = 0; t < 5000; t++) CHECK(last.clock(0, uint8_t(t * 37 + 1)).down == 0, "limits: the last ASIC sends on a link it does not have");
}

// The bytes are all there is between two ends: one in this process, one in a child, a socket between.
void two_processes() {
    int fd[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd)) { CHECK(false, "two processes: no socket"); return; }
    const unsigned cycles = 12000, words = 40;
    fflush(stdout);
    const pid_t child = fork();
    if (child == 0) {
        close(fd[0]);
        End b;
        uint64_t next = 0;
        for (unsigned t = 0; t < cycles; t++) {
            uint8_t in = 0;
            if (read(fd[1], &in, 1) != 1) _exit(2);
            const uint8_t out = b.clock(in);
            if (write(fd[1], &out, 1) != 1) _exit(2);
            Word w;
            while (b.take(FOREGROUND, w)) { if (!is(w, 9, next)) _exit(3); next++; }
        }
        _exit(next == words ? 0 : 4);
    }
    close(fd[1]);
    End a;
    Word w[MAX_WORDS];
    for (unsigned i = 0; i < words; i += 20) { for (unsigned k = 0; k < 20; k++) w[k] = made(9, i + k); a.push(FOREGROUND, w, 20); }
    uint8_t in = 0;
    for (unsigned t = 0; t < cycles; t++) {
        const uint8_t out = a.clock(in);
        if (write(fd[0], &out, 1) != 1 || read(fd[0], &in, 1) != 1) { CHECK(false, "two processes: the socket closed"); break; }
    }
    int status = 0;
    waitpid(child, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "two processes: the child ended with %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    close(fd[0]);
    printf("two ends in two processes, a socket between: %u words arrived\n", words);
}

}  // namespace

int main() {
    limits();
    for (uint64_t seed = 1; seed <= 4; seed++) two_ends(seed, seed == 1 ? 0 : 1e-4 * double(seed));
    chain(1, 1, 0, -1);
    chain(2, 2, 2e-4, -1);
    chain(3, 4, 1e-4, -1);
    chain(4, 3, 0, 1);
    power_on_under();
    two_processes();
    if (failures) { printf("test_hostlink: %d FAILED\n", failures); return 1; }
    printf("test_hostlink: ok\n");
    return 0;
}
