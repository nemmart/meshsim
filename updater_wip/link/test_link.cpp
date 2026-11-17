// test_link: two link ends on the bench. A perfect wire first, at the real sizes and the small ones, under the
// rules as written and as proposed: everything sent arrives, in order, once, and the link comes to rest.
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
    for (const char* rules : {"as_written", "proposed", "proposed+ack_now", "proposed,resend_timer=2048"}) {
        Config real;                                         // the real sizes: full packets both ways on both streams
        real.packets[0][FG] = 40; real.packets[0][BG] = 300; real.packets[1][FG] = 200; real.packets[1][BG] = 0;
        const long a = perfect(real, rules, "the real sizes");
        Config small; small.p = Params::tiny(); small.queue_words = 4; small.words = 1;
        small.packets[0][FG] = 50; small.packets[0][BG] = 50; small.packets[1][FG] = 50; small.packets[1][BG] = 50;   // 8 wraps of the number
        const long c = perfect(small, rules, "the small sizes");
        Config one = real; one.words = 1;                    // short packets: the window and the acknowledgements' pace
        one.packets[0][FG] = 400; one.packets[0][BG] = 0; one.packets[1][FG] = 0;
        const long d = perfect(one, rules, "one-word packets");
        printf("%-28s real %ld cycles, small %ld, 400 one-word packets %ld\n", rules, a, c, d);
    }
    // The wrap at the real width: both ends started just under it.
    {
        Config cfg; cfg.packets[0][BG] = 20; cfg.packets[1][FG] = 20;
        Link1 b(cfg);
        for (auto& e : b.end) for (auto& st : e.r.s) { st.last = st.acked = 0x3FFF0; st.next = st.tail = 0x3FFF1; }
        CHECK(b.quiet(1000000) > 0);
        CHECK(b.end[0].r.s[BG].tail == 7 && b.end[1].r.s[FG].tail == 7 && b.end[1].r.s[BG].last == 6);
    }
    // G13: the background's acknowledgements while the other side's foreground replies flow. End 1 sends 200 full
    // foreground packets; end 0 sends 300 full background packets. The longest that end 0's background stands still.
    for (const char* rules : {"as_written", "proposed-status_each", "proposed"}) {
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
        const bool fixed = std::string(rules) == "proposed";
        CHECK(b.done() && (fixed ? longest < 2 * 528 + 64 : longest > 50000));    // fails without the rule
    }
    // What one damaged packet costs at the real sizes: 40 full background packets one way, the 20th damaged (and,
    // for "both lost", the status coming back at that moment as well). The cycles from that packet's first byte to
    // the next word delivered: on a whole wire a packet later, 528.
    {
        struct { const char* name; Fate f0; bool both; } forms[] = {
            {"a bit of its data", Fate::DATA, false}, {"a bit of its header", Fate::HEADER, false},
            {"its first byte zero", Fate::FIRST0, false}, {"the whole packet gone", Fate::GONE, false},
            {"a bit of its header, and the other way too", Fate::HEADER, true}};
        for (const auto& form : forms) {
            Config cfg; cfg.packets[0][BG] = 40;
            Link1 b(cfg);
            uint64_t hit = 0, back = 0, last_delivery = 0; bool other_hit = false;
            while (!b.done() && b.bad.empty() && b.t < 2000000) {
                for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++) { b.src[e][s].avail = b.src[e][s].total; b.fill[e][s] = 0; }
                const uint64_t had = b.delivered[1][BG];
                b.step();
                if (b.started[0] && b.end[0].tap.kind == Kind::DATA && unpack(b.end[0].tap.hdr).number == 20 && !hit) { b.fate(0, form.f0); hit = b.t; }
                if (form.both && hit && !other_hit && b.started[1]) { b.fate(1, Fate::HEADER); other_hit = true; }
                if (b.delivered[1][BG] != had) { if (hit && !back && last_delivery > hit) {} last_delivery = b.t; if (hit && !back && b.delivered[1][BG] > 19 * 32) back = b.t; }
            }
            CHECK(b.done() && hit && back);
            printf("the 20th packet, %-44s the next word delivered %5llu cycles after its first byte; all done at %llu\n", form.name,
                   (unsigned long long)(back - hit), (unsigned long long)b.t);
        }
    }
    printf(failures ? "test_link: %d FAILED\n" : "test_link: ok\n", failures);
    return failures != 0;
}
