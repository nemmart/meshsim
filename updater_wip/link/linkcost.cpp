// linkcost: what the protocol's choices cost, measured on one link at the real sizes, with nothing drawn at random:
// every line can be had again.
//   setback   one run of numbers: how far a damaged background packet sets a foreground packet back
//   window    the window shared by both streams: the data rate and a foreground packet's wait, by window and wire delay
//   pass      the sender's pass over a packet's data before it leaves: waits and data rate, idle link and busy
//   wrong     a packet made wrong: from its first arrival to the error
//   start     how long a link takes to come up
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bench.h"

using namespace hl;

namespace {

void feed(Link1& b, bool bg0, bool fg1) {                  // the floods, and every queue emptied
    if (bg0) b.src[0][BG].avail = b.src[0][BG].total;
    if (fg1) b.src[1][FG].avail = b.src[1][FG].total;
    for (auto& e : b.fill) for (auto& f : e) f = 0;
}

// A background flood from end 0. Its packet number `hit` is damaged (or not); one foreground packet of `words` is let
// out d cycles after the packet before it started. The cycles from letting it out to its last word delivered.
long setback_one(Fate fate, unsigned d, unsigned words, unsigned* resent) {
    Config cfg; cfg.packets[0][BG] = 120; cfg.packets[0][FG] = 1; cfg.words = 32;
    Link1 b(cfg);
    const unsigned hit = 40;
    unsigned bg_seen = 0; uint64_t ref = 0, let = 0, got = 0; bool is_let = false;
    (void)words;
    while (b.bad.empty() && b.t < 400000 && !got) {
        feed(b, true, false);
        if (ref && !is_let && b.t >= ref + d) { b.src[0][FG].avail = 1; let = b.t; is_let = true; }
        b.step();
        if (b.started(0) && b.end[0].tap.kind == Kind::DATA && unpack(b.end[0].tap.hdr).stream == 2) {
            bg_seen++;
            if (bg_seen == hit - 1) ref = b.t;
            if (bg_seen == hit && fate != Fate::WHOLE) b.fate(0, fate);
        }
        if (is_let && b.delivered[1][FG] == 32) got = b.t;
    }
    if (!b.bad.empty()) { printf("FAILED: %s\n", b.bad.c_str()); return -1; }
    if (resent) *resent = unsigned(b.end[0].count.sent[unsigned(Kind::RESENT)]);
    return got ? long(got - let) : -1;
}

void setback() {
    printf("setback: a background flood; one full foreground packet let out d cycles after background packet 39 starts, d = 0, 16, ... 1600;\n"
           "background packet 40 is damaged. Cycles from the foreground packet let out to its last word delivered:\n");
    struct { const char* name; Fate f; } rows[] = {{"nothing damaged", Fate::WHOLE}, {"a bit of its data", Fate::DATA}, {"a bit of its header", Fate::HEADER},
                                                   {"its first byte zero", Fate::FIRST0}, {"the whole packet gone", Fate::GONE}};
    std::vector<long> base;
    for (const auto& row : rows) {
        long worst = 0, sum = 0, worst_extra = 0, sum_extra = 0; unsigned n = 0, at = 0, most_resent = 0, k = 0;
        for (unsigned d = 0; d <= 1600; d += 16, k++) {
            unsigned resent = 0;
            const long took = setback_one(row.f, d, 32, &resent);
            if (took < 0) { printf("  %s, d %u: not delivered\n", row.name, d); continue; }
            if (row.f == Fate::WHOLE) base.push_back(took);
            const long extra = took - base[k];
            if (took > worst) { worst = took; at = d; }
            worst_extra = std::max(worst_extra, extra); sum += took; sum_extra += extra; n++; most_resent = std::max(most_resent, resent);
        }
        printf("  %-24s at most %5ld (d = %4u), on average %7.1f; over nothing damaged at the same d: at most %5ld more, on average %7.1f; packets sent again, at most %u\n",
               row.name, worst, at, double(sum) / n, worst_extra, double(sum_extra) / n, most_resent);
    }
}

struct Flow { double rate = 0; uint64_t wait_max = 0, n = 0, full_max = 0; double wait_mean = 0; unsigned kept = 0; uint64_t trip_max = 0; double trip_mean = 0; };

// End 0 floods the background (or not); end 1 floods foreground replies (or not); end 0 lets out foreground packets
// of `words` one at a time, each a while after the last was delivered. The background's data rate, and each
// foreground packet's wait from let out to its first byte on the line, and to its last word delivered.
Flow flow(const char* rules, unsigned window, unsigned delay, unsigned words, bool bg0, bool fg1, uint64_t cycles = 600000) {
    Config cfg; cfg.delay = delay; cfg.words = words;
    std::string err; apply_rules(rules, cfg.p, err);
    cfg.p.window = window;
    cfg.packets[0][BG] = bg0 ? 1000000 : 0; cfg.packets[1][FG] = fg1 ? 1000000 : 0; cfg.packets[0][FG] = 1000000;
    Link1 b(cfg);
    Flow f;
    uint64_t let = 0, next_let = 20000, sum_wait = 0, sum_trip = 0, full = 0, x = 12345, bg_at_start = 0, t_start = 0;
    bool waiting = false, sent = false;
    while (b.bad.empty() && b.t < cycles) {
        feed(b, bg0, fg1);
        if (b.t == 20000) { bg_at_start = b.delivered[1][BG]; t_start = b.t; }
        if (!waiting && b.t >= next_let) { b.src[0][FG].avail++; let = b.t; waiting = true; sent = false; full = 0; }
        const uint64_t had = b.delivered[1][FG];
        if (waiting && !sent && b.end[0].running() && b.end[0].kept() >= b.end[0].p.window) full++;
        b.step();
        if (waiting && !sent && b.started(0) && b.end[0].tap.kind == Kind::DATA && unpack(b.end[0].tap.hdr).stream == 1) {
            sent = true; const uint64_t w = b.t - 1 - let;
            sum_wait += w; f.wait_max = std::max(f.wait_max, w); f.full_max = std::max(f.full_max, full); f.n++;
        }
        if (waiting && b.delivered[1][FG] != had && b.delivered[1][FG] % words == 0) {
            const uint64_t trip = b.t - let; sum_trip += trip; f.trip_max = std::max(f.trip_max, trip);
            waiting = false;
            x = x * 6364136223846793005ull + 1442695040888963407ull;
            next_let = b.t + 1 + (x >> 33) % 3001;                 // 1 to 3,001 cycles later: every phase of the flood
        }
    }
    if (!b.bad.empty()) printf("FAILED: %s\n", b.bad.c_str());
    f.rate = double(b.delivered[1][BG] - bg_at_start) * WORD_BYTES / double(b.t - t_start);
    f.wait_mean = f.n ? double(sum_wait) / f.n : 0; f.trip_mean = f.n ? double(sum_trip) / f.n : 0;
    f.kept = b.end[0].count.most_kept;
    return f;
}

void window() {
    printf("window: end 0 floods full background packets; a full foreground packet is let out now and then (every phase of the flood).\n"
           "By the wire's delay each way and the window: the background's bytes of data a cycle; the most packets kept; a foreground packet's wait\n"
           "from let out to its first byte on the line, on average and at most; of that wait, the most with the window full.\n");
    for (int replies = 0; replies < 2; replies++) {
        printf(replies ? "  with end 1 flooding full foreground packets the other way (its acknowledgements ride on them):\n" : "  with nothing but acknowledgements coming back:\n");
        for (unsigned delay : {1u, 16u, 128u, 512u, 2048u}) {
            for (unsigned w : {2u, 4u, 8u, 16u}) {
                const Flow f = flow("hl", w, delay, 32, true, replies);
                printf("    delay %4u, window %2u: %.4f bytes a cycle; kept %2u; foreground wait %6.1f on average, %5llu at most, %5llu with the window full (%llu packets)\n",
                       delay, w, f.rate, f.kept, f.wait_mean, (unsigned long long)f.wait_max, (unsigned long long)f.full_max, (unsigned long long)f.n);
            }
        }
    }
}

void pass() {
    printf("pass: the sender passes over a packet's data, a byte a cycle, before the header leaves (pass=1), against no pass (pass=0).\n"
           "A foreground packet let out now and then: from let out to its first byte on the line, and to its last word delivered.\n");
    for (unsigned words : {1u, 32u}) {
        for (int busy = 0; busy < 2; busy++) {
            for (const char* rules : {"hl,pass=0", "hl"}) {
                const Flow f = flow(rules, 8, 1, words, busy, false);
                printf("  %-9s %2u-word foreground packets, %-28s to the line %6.1f on average, %4llu at most; delivered %6.1f on average, %4llu at most; background %.4f bytes a cycle\n",
                       rules, words, busy ? "a background flood beside it:" : "an idle link:", f.wait_mean, (unsigned long long)f.wait_max, f.trip_mean,
                       (unsigned long long)f.trip_max, f.rate);
            }
        }
    }
    // One-word packets one after another, and full ones: the whole of a transfer.
    for (unsigned words : {1u, 32u}) {
        for (const char* rules : {"hl,pass=0", "hl"}) {
            Config cfg; cfg.words = words; cfg.packets[0][FG] = 400;
            std::string err; apply_rules(rules, cfg.p, err);
            Link1 b(cfg);
            uint64_t up = 0;
            while (b.bad.empty() && !b.done() && b.t < 5000000) {
                for (auto& e : b.fill) for (auto& f : e) f = 0;
                if (!up && b.end[0].running() && b.end[1].running()) up = b.t;
                if (up) b.src[0][FG].avail = b.src[0][FG].total;
                b.step();
            }
            printf("  %-9s 400 packets of %2u words back to back: %llu cycles from the link up to all acknowledged\n", rules, words, (unsigned long long)(b.t - up));
        }
    }
}

void wrong() {
    printf("wrong: end 0 sends full foreground packets back to back; its 10th is made wrong (hops 15 to an end that would forward it).\n");
    for (unsigned words : {32u, 1u}) {
        Config cfg; cfg.words = words; cfg.packets[0][FG] = 40; cfg.wrong = 10;
        Link1 b(cfg);
        uint64_t first = 0, second = 0, error = 0, back = 0; uint64_t had = 0;
        while (b.bad.empty() && b.t < 400000 && !(back && b.done())) {
            feed(b, false, false); b.src[0][FG].avail = b.src[0][FG].total;
            had = b.delivered[1][FG];
            b.step();
            if (b.end[1].ev.rx_bad_fields) { if (!first) first = b.t; else if (!second) second = b.t; }
            if (b.end[1].ev.wrong_twice) error = b.t;
            if (error && !back && b.delivered[1][FG] != had) back = b.t;
        }
        printf("  %2u-word packets: it arrives at cycle %llu; again %llu cycles later, and the error is raised; the next word is delivered %llu cycles after the error; "
               "%llu words were lost with the restart, of %u sent; %s\n", words, (unsigned long long)first, (unsigned long long)(second - first), (unsigned long long)(back - error),
               (unsigned long long)b.lost_words, 40 * words, b.bad.empty() ? (b.end[1].r.error ? "error up at end 1" : "NO ERROR") : b.bad.c_str());
    }
}

void start() {
    Config cfg; Link1 b(cfg);
    while (!(b.end[0].running() && b.end[1].running()) && b.t < 100000) b.step();
    printf("start: both ends from power-on at once: the link is up (both in step) after %llu cycles\n", (unsigned long long)b.t);
    Link1 c(cfg);
    while (!(c.end[0].running() && c.end[1].running()) && c.t < 100000) c.step();
    c.power[1] = true;
    const uint64_t at = c.t; uint64_t err = 0;
    do { c.step(); if (!err && c.end[0].r.error) err = c.t; } while (!(c.end[0].running() && c.end[1].running() && c.t > at + 2) && c.t < 200000);
    printf("start: one end from power-on on a link that was up: the other raises its error %llu cycles later, and the link is up again after %llu cycles\n",
           (unsigned long long)(err - at), (unsigned long long)(c.t - at));
}

}  // namespace

int main(int argc, char** argv) {
    const std::string what = argc > 1 ? argv[1] : "all";
    if (what == "setback" || what == "all") setback();
    if (what == "window" || what == "all") window();
    if (what == "pass" || what == "all") pass();
    if (what == "wrong" || what == "all") wrong();
    if (what == "start" || what == "all") start();
    return 0;
}
