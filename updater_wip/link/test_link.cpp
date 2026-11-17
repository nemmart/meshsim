// test_link: two link ends on the bench. A perfect wire first, at the real sizes and the small ones: everything sent
// arrives, in order, once, and the link comes to rest. Then the wrap of the numbers, the other direction's
// acknowledgements under a flood, and the host's window against the queue.
#include <cstdio>
#include <string>

#include "bench.h"

using namespace hl;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Everything let out at once, the queues emptied as they fill: must finish, and in how many cycles.
static long perfect(Config cfg, const char* rules, const char* name) {
    std::string err;
    if (!apply_rules(rules, cfg.p, err)) { printf("%s\n", err.c_str()); failures++; return -1; }
    Link1 b(cfg);
    const long took = b.quiet(50000000);
    if (took < 0) { printf("FAIL %s under %s: %s\n", name, rules, b.bad.c_str()); failures++; }
    return took;
}

int main() {
    for (const char* rules : {"hl", "hl-ack_now", "hl,pass=0", "hl+stop_holds_again"}) {
        Config real;                                         // the real sizes: full packets both ways on both streams
        real.packets[0][FG] = 40; real.packets[0][BG] = 300; real.packets[1][FG] = 200; real.packets[1][BG] = 0;
        const long a = perfect(real, rules, "the real sizes");
        Config small; small.p = Params::tiny(); small.queue_words = 8; small.words = 1;
        small.packets[0][FG] = 50; small.packets[0][BG] = 50; small.packets[1][FG] = 50; small.packets[1][BG] = 50;   // many wraps of the number
        const long c = perfect(small, rules, "the small sizes");
        Config one = real; one.words = 1;                    // short packets: the window and the acknowledgements' pace
        one.packets[0][FG] = 400; one.packets[0][BG] = 0; one.packets[1][FG] = 0;
        const long d = perfect(one, rules, "one-word packets");
        printf("%-22s real sizes %ld cycles, small sizes %ld, 400 one-word packets %ld\n", rules, a, c, d);
    }
    // The wrap at the real width: both ends started just under it.
    {
        Config cfg; cfg.packets[0][BG] = 20; cfg.packets[1][FG] = 20;
        Link1 b(cfg);
        for (auto& e : b.end) { e.r.restart = false; e.r.last = e.r.acked = 0x3FFF0; e.r.next = e.r.tail = 0x3FFF1; }
        CHECK(b.quiet(1000000) > 0);
        CHECK(b.end[0].r.tail == 7 && b.end[1].r.tail == 7 && b.end[1].r.last == 6 && b.end[0].r.last == 6);
    }
    // The starvation of the first header (numbers for each stream): the background one way while the other side's
    // foreground replies flow. End 1 sends 200 full foreground packets; end 0 sends 300 full background packets.
    // The longest that end 0's background stands still: with one acknowledgement in every header, a packet or two.
    for (const char* rules : {"hl-ack_now", "hl"}) {
        Config cfg; cfg.packets[0][BG] = 300; cfg.packets[1][FG] = 200;
        std::string err; apply_rules(rules, cfg.p, err);
        Link1 b(cfg);
        uint64_t last_start = 0, longest = 0; bool any = false;
        while (!b.done() && b.bad.empty() && b.t < 2000000) {
            for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++) { b.src[e][s].avail = b.src[e][s].total; b.fill[e][s] = 0; }
            b.step();
            if (b.end[0].tap.start && b.end[0].tap.kind == Kind::DATA) {
                if (any && b.t - last_start > longest) longest = b.t - last_start;
                last_start = b.t; any = true;
            }
        }
        printf("%-22s background stood still for at most %llu cycles under the other side's replies; all done at %llu\n", rules,
               (unsigned long long)longest, (unsigned long long)b.t);
        CHECK(b.done() && b.bad.empty() && longest < 2 * 528 + 64);
    }
    // The host's window (HL, "Holding the host back"): end 0 has 80 full background packets; nothing comes back from
    // end 1 (every packet it sends is turned to zeros: the stop bit and the acknowledgements with it) and its queue
    // is not emptied. With a window of 8 or 16 the host stops by itself with room to spare. With 64 it goes on: the
    // queue of 1,024 words holds 32 packets, and the 33rd is thrown away or, without that rule, overruns.
    for (const char* rules : {"hl", "hl-refuse"}) {
        for (unsigned window : {8u, 16u, 64u}) {
            Config cfg; cfg.packets[0][BG] = 80; cfg.window0 = window;
            std::string err; apply_rules(rules, cfg.p, err);
            Link1 b(cfg);
            while (b.bad.empty() && b.t < 70000) {
                b.src[0][BG].avail = b.src[0][BG].total;
                b.step();
                if (b.started(1) && b.end[0].running() && b.end[1].running()) b.fate(1, Fate::GONE);
            }
            const bool overruns = !b.bad.empty();
            printf("the host's window %2u, %-10s it kept %2u packets at most; the queue held %4u words at most; %llu packets thrown away for no room%s\n",
                   window, rules, b.end[0].count.most_kept, b.most_fill[1][BG], (unsigned long long)b.end[1].count.refused, overruns ? "; " : "");
            if (overruns) printf("    %s\n", b.bad.c_str());
            CHECK(overruns == (window == 64 && std::string(rules) == "hl-refuse"));
            CHECK(overruns || b.end[0].count.most_kept == window);
        }
    }
    printf(failures ? "test_link: %d FAILED\n" : "test_link: ok\n", failures);
    return failures != 0;
}
