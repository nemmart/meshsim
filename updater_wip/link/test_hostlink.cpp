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
        // The counts: every packet first sent is taken once; nothing refused, nothing started again.
        const LinkCounts c = e[i].counts(), o = e[1 - i].counts();
        CHECK(c.taken == o.sent_data && c.taken > 0, "two ends, seed %llu: end %u took %llu packets of the %llu the other sent", (unsigned long long)seed, i,
              (unsigned long long)c.taken, (unsigned long long)o.sent_data);
        CHECK(c.refused == 0 && c.restarts == 0 && c.wrong_once == 0 && c.asked_not_kept == 0, "two ends, seed %llu: end %u refused %llu, started again %llu", (unsigned long long)seed, i,
              (unsigned long long)c.refused, (unsigned long long)c.restarts);
        if (noise > 0) CHECK(c.bad_header + c.bad_data > 0 && c.rounds > 0 && o.sent_again > 0 && o.went_back > 0, "two ends, seed %llu: end %u counted no damage under noise", (unsigned long long)seed, i);
        else CHECK(c.bad_header + c.bad_data + c.bad_fields + c.ahead + c.behind + o.sent_again == 0 && c.rounds > 0, "two ends, seed %llu: end %u counted damage with none", (unsigned long long)seed, i);
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
        if (e[0].status().error) {
            error_seen = true;
            CHECK(t > 60000, "power-on: an error before anything happened");
            const LinkStatus st = e[0].status();                      // why: the other end said so, and nothing else
            CHECK(st.other_end && !st.made_wrong && !st.not_kept, "power-on: the error's cause is not the other end's starting again");
            e[0].clear_error();
            CHECK(!e[0].status().error && !e[0].status().other_end, "power-on: the error is not cleared");
        }
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
    CHECK(e[0].counts().restarts == 1 && e[1].counts().restarts == 0 && !e[1].status().error, "power-on: end 0 started again %llu times, end 1 %llu",
          (unsigned long long)e[0].counts().restarts, (unsigned long long)e[1].counts().restarts);
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

// A packet made wrong, and who is told what. A chain of 17 is one too long: hops is four bits, so the 16th ASIC is
// sent a packet with hops 15 that it would have to forward. It asks for it again, gets the same, and starts the
// link again, its cause "made wrong"; the 15th, which made the packet so, hears "I have started again" and no more.
void made_wrong() {
    const unsigned n = 17;
    End host;
    std::vector<Node> node;
    for (unsigned i = 0; i < n; i++) node.emplace_back(i + 1 < n);
    std::vector<uint8_t> down(n, 0), up(n, 0);
    const Word w = made(7, 0);
    host.push(BACKGROUND, &w, 1);
    for (unsigned t = 0; t < 30000; t++) {
        std::vector<uint8_t> nd(n), nu(n);
        nd[0] = host.clock(up[0]);
        for (unsigned i = 0; i < n; i++) {
            const Node::Bytes b = node[i].clock(down[i], i + 1 < n ? up[i + 1] : 0);
            nu[i] = b.up; if (i + 1 < n) nd[i + 1] = b.down;
        }
        down = nd; up = nu;
    }
    const LinkStatus got = node[15].above(), sent = node[14].below();
    CHECK(got.error && got.made_wrong && !got.other_end && !got.not_kept, "made wrong: the ASIC that was sent it: error %d, made wrong %d, other end %d", got.error, got.made_wrong, got.other_end);
    CHECK(sent.error && sent.other_end && !sent.made_wrong && !sent.not_kept, "made wrong: the ASIC that sent it: error %d, other end %d, made wrong %d", sent.error, sent.other_end, sent.made_wrong);
    const LinkCounts c = node[15].counts_above();
    CHECK(c.wrong_once == 1 && c.wrong_twice == 1 && c.bad_fields == 2 && c.restarts == 1, "made wrong: counted once %llu, twice %llu, fields %llu, restarts %llu",
          (unsigned long long)c.wrong_once, (unsigned long long)c.wrong_twice, (unsigned long long)c.bad_fields, (unsigned long long)c.restarts);
    unsigned others = 0, took = 0;
    for (unsigned i = 0; i < n; i++) {
        if (i != 15 && node[i].above().error) others++;
        if (i != 14 && node[i].below().error) others++;
        Word x; if (node[i].take(BACKGROUND, x)) took++;
    }
    CHECK(others == 0 && !host.status().error, "made wrong: %u other ends have an error", others);
    CHECK(took == 15 && node[15].above().in_step && node[14].below().in_step, "made wrong: %u ASICs took the word", took);
    printf("a chain of 17: the 16th ASIC is sent hops 15 to forward, twice; it raises \"made wrong\", its neighbour above only \"the other end\"; the 15 above it took the word\n");
}

// A size set wrong shows as packets refused. The host's window is 64, four times what a queue of 1,024 words allows;
// the ASIC never takes a word, and nothing it sends reaches the host, so no "stop" does.
void refused() {
    Sizes big; big.window = 64;
    End host(big);
    Node asic(false);
    uint8_t down = 0, up = 0;
    Word w[MAX_WORDS] = {};
    unsigned pushed = 0;
    for (unsigned t = 0; t < 60000; t++) {
        const bool cut = t >= 10000;                                  // from here the host hears nothing
        const uint8_t h = host.clock(cut ? 0 : up);
        const Node::Bytes b = asic.clock(down, 0);
        down = h; up = b.up;
        if (t == 9999) CHECK(host.status().in_step && asic.above().in_step, "refused: the link is not up");
        if (cut && pushed < 48 && host.push(BACKGROUND, w, MAX_WORDS)) pushed++;
    }
    const LinkCounts c = asic.counts_above();
    // The 33rd is the one expected and finds no room: refused. Those behind it are ahead of the one expected.
    CHECK(pushed == 48 && c.taken == 32 && c.refused == 1 && c.ahead >= 15 && asic.waiting(BACKGROUND) == 1024, "refused: pushed %u, taken %llu, refused %llu, ahead %llu, waiting %u", pushed,
          (unsigned long long)c.taken, (unsigned long long)c.refused, (unsigned long long)c.ahead, asic.waiting(BACKGROUND));
    CHECK(!asic.above().error, "refused: an error");
    printf("a host's window of 64 and a line that brings nothing back: the ASIC took 32 full packets, refused the 33rd, and threw %llu away as ahead of it\n", (unsigned long long)c.ahead);
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
    made_wrong();
    refused();
    two_processes();
    if (failures) { printf("test_hostlink: %d FAILED\n", failures); return 1; }
    printf("test_hostlink: ok\n");
    return 0;
}
