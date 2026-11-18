# WORKER8's answer: the host link's specification against the link end

*Checked against `updater-w8` at 5c8ad13a: `updater_wip/link/link_end.cpp` ("le"), `header.h`, `node.cpp`,
`hostlink.h` and `hostlink.cpp`; line numbers are that commit's. The specification is `v137-spec` at 9201577b:
`host-link.md` 28.1 to 28.17, and `machine-spec.md` 3, 17.6.3, 17.6.8, 17.6.12, 17.6.14, 18, 21.5 and 24. "Run" means
run for this answer, on that commit, with programs outside the repository; the one for the reset is the appendix.
A dagger (†) marks what I propose. I changed nothing but this file.*

**In short:** the chapter states what the link end does, and I found no rule in it that the code breaks. The reset
in the middle of a running chain is now run, 1,380 times, and does what 28.14 says. List 1 has 14 places where
the text is looser than the code or quotes a figure that has moved; list 2 has 7 places where the code is silent
or a run is missing.

## The 11 claims, in brief

| # | Claim | Verdict |
|---|---|---|
| 1 | the worked examples | **Right, and now produced by the code.** A program on `header.h` and `crc32c.h` printed 24 values: the CRC's three, the four headers, the residue, the three mask examples with their `header_crc`, starts and line bytes, the restart table's two words and its checksum, the fast unit's start (four zero bytes take 0x641F_6454 to all ones), and the mask's two standing states. Each is in the chapter character for character |
| 2 | the one reset | **Right. Run:** the next section |
| 3 | ahead and behind | **Right** (`header.h` 94: 1 to `last / 2` steps, and `last / 2` is 0x1FFFF) |
| 4 | 28.7's table and asking again | **Right,** with three precisions: list 1, items 2 to 4 |
| 5 | 28.8's rounds, and both flags | **Right** (le 147–153: a link that leaves step starts a round at its zeros; le 265–267 and 399–400: a lost end that hears "lost" sends both flags) |
| 6 | 28.11's order | **Right** (le 384 the round; le 407 with 337–353 the link packet with data: one to be sent again, else the foreground's, else the background's, a new one only with its stream not stopped and fewer than 8 kept; le 432–435 the status) |
| 7 | 28.12's taking and stop bits | **Right** (`node.cpp` 44–45, 57–58, 65–66, 72) |
| 8 | 28.13's tables and totals | **Right:** `build/linkhw` prints 231, 344, 16 and 39 flip-flops, 630 and 4,608 bytes a link end, 1,424 and 77,312 an ASIC, 110,080 with the queues, 34,048 and 17,024 saved, 85%. The gate counts (2,490 and 270) are not from my code or my runs |
| 9 | 28.16's figures | **Two have moved, and three need a word:** list 1, items 9 to 13 |
| 10 | 17.6.3's landing | **Right for the core's count; the room must count more:** list 1, item 7 |
| 11 | 17.6.8's offer | **Can be fed so,** with two things to know: list 1, item 14 |

## The one reset, run

**No run had covered it.** `linksearch --power` and `cases/power-on.case` power one end of one link; the chain
bench has no reset. So I made one on `hostlink.h` alone.

**The run.** A host and a chain of ASICs under traffic on both streams and replies from every ASIC, under half of
what the line carries each way. At a random cycle an ASIC is reset: held for a number of cycles, in which it is not clocked
and its lines read zero, then `Node::power_on()`, which is 17.6.14's reset: both link ends, the four forwarding
buffers and the queues. The cycle is random, so the reset cuts link packets in the middle, in both directions.
Noise, where the row says so, changes each byte on every line with a chance of 2 in 10,000 (1 in 10,000 on the chain
of 16), before, during and after.

**Checked in every run:**

1. no end has an error before the reset;
2. after it, each neighbour that was not itself reset has its error up, on its end of the link to the reset ASIC;
3. no other end has: not the reset ASIC's two, and not a link further away;
4. every link is in step at the end, and no error is raised again once they are cleared;
5. words pushed once the links are up all arrive, at every ASIC and at the host;
6. at every taker a source's words come in order and none twice; above the reset ASIC none is missing.

| what is reset | runs | the neighbour above raises its error, cycles from the release | the neighbour below | every link in step again | words that did not arrive, a run |
|---|---|---|---|---|---|
| the middle ASIC of 3, no hold, no noise | 150 | 1,553 | 1,553 | 4,015 at most | 120 on average, 353 at most |
| the middle ASIC of 3, held up to 6,000 cycles, no noise | 150 | 1,553 | 1,553 | 4,022 at most | 263, 484 |
| the same, with noise | 150 | 1,553 to 6,209 | 1,553 to 6,209 | 17,638 at most | 283, 791 |
| the first ASIC of 3: its upper neighbour is the host | 150 | 1,553 to 6,209 | 1,553 to 4,657 | 23,851 | 460, 1,132 |
| the last ASIC of 3: none below | 150 | 1,553 to 7,761 | | 18,737 | 67, 212 |
| any one ASIC of 4 | 150 | 1,553 to 4,657 | 1,553 to 4,657 | 27,911 | 307, 1,623 |
| any one ASIC of 16 | 30 | 1,553 to 4,657 | 1,553 to 4,657 | 62,825 | 746, 2,074 |
| every ASIC of 4 at once, the host running on | 150 | 1,553 to 4,657 | | 37,554 | 848, 2,349 |
| the second ASIC of 4, and again within 4,000 cycles of its release | 150 | 1,553 to 6,965 | 1,553 to 8,069 | 25,082 | 425, 1,196 |
| the second ASIC of 4, then the third within 4,000 cycles | 150 | 1,553 to 6,209 | 1,837 to 8,497 | 23,896 | 494, 1,385 |

**1,380 runs; no check failed.** Rows 4 to 10 have noise and a hold of up to 6,000 cycles.

- **28.14 holds: both neighbours raise their error, and the link comes back up.** Without noise the error is up
  1,553 cycles after the reset is released: the 1,024 zeros of the first round, the restart link packet's 528
  bytes, and one. With noise it is a round or more later, 1,552 cycles each. Every link was in step again within
  4,022 cycles without noise; with noise a link also leaves step for the noise's own reasons, so those figures are
  of the noise as much as of the reset.
- **The reset ASIC raises no error itself.** Its `error` is zero out of the reset (le 10), and it is in the
  handshake when it hears the answers (le 250–254).
- **The ASICs above the reset one miss nothing** of the host's words, and the host misses none of theirs. What is
  lost is what the reset ASIC held, and what its two neighbours had sent it and not yet had acknowledged. "Words
  that did not arrive" adds every taker's: each ASIC's of the host's, and the host's of each ASIC's.
- **Every ASIC reset at once, with the host running:** only the host's end raises an error. The links between the
  ASICs come up with none, as 28.9 says of two ends that power on together.
- **Two neighbours reset one after the other:** in 13 of 150 runs the one reset first had its own error up
  afterwards. Its link was already up again when it heard the second say "I have started again". So a chip can
  have `link_error` set shortly after its own reset, with nothing wrong in it. † 28.14 could say so.
- **† What the run is not:** it is random, not a search of every state, and it is not in the link's `make check`.
  I can add it there as a test, and a reset to the chain's search, on the owner's word; both are changes to the
  link's code.

## List 1: where the specification misdescribes the code

1. **28.3, 28.12, `hops` 15.** The code does not test `hops` on a restart link packet (le 123–124, `!is_restart`).
   One is always made with `hops` 0, so only a restart link packet that is itself made wrong shows the difference:
   the code acts on it, the text would count it as made wrong.
2. **28.7, "The `ack` field and the stop bits of every good link packet are taken", and 28.8, "An `ack` counts in
   any state".** Three kinds are not taken. Two the chapter has elsewhere: a restart link packet's (le 249–258), and
   any ordinary one's while this end's `restart` is set (le 260); 28.9 says both. The third it does not have: **a
   link packet with a field that cannot be has its `ack`, its stop bits, `resend` and `lost` ignored,** though both
   its checks pass (le 158–166 never reaches le 272). 28.13.3's table implies it by its order.
3. **28.7's table, "the data checksum failing ... a resend is owed".** Only for a link packet with data, and only
   while this end is in step (le 171). A link packet of length 0 whose `data_checksum` is not 0 is thrown away and
   nothing is owed. While the link is not in step nothing is owed for a damaged link packet: the recovery's own
   exchange tells the sender where to go back to.
4. **28.7, a `length` over 32 "counted as a link packet made wrong if its `number` is the one expected".** And only
   while this end is not lost, not starting again and not answering a restart (le 112, 115). The same holds for a
   field that cannot be (le 160). The second time, the receiver starts the link again and waits for the 528 zeros
   without being `lost` (le 116).
5. **28.13.3, the judging table's row 2: "ahead (after the 2,048 cycles)".** The wording is my `HARDWARE.md`'s, and
   it is loose. 28.7 has it right: a link packet ahead is asked for at once if nothing has been asked for, and
   again only 2,048 cycles after the last asking (le 173, 294, 305, 447).
6. **17.6.1 and 28.14, `format_error`.** The text does not say at which ASIC. In the code only the end that
   *received* the wrong link packet knows the cause (le 103, `ev.wrong_twice`). The end that *made* it hears "I
   have started again" and raises its error with no cause (le 254). So `format_error` comes up at the neighbour of
   the ASIC at fault, and `link_error` alone at the ASIC at fault.
7. **17.6.3 and 28.13.6 row 1, "each counting in `…_WORDS` as it lands".** The model counts a link packet's words
   all at once in the cycle it is taken (`hostlink.cpp` 38, 119; `node.cpp` 44–45), and the room and "half full"
   are of that count. In hardware a full link packet lands over 32 cycles, and a link packet of one word behind it
   is judged exactly 32 cycles after it: 16 for its header and 16 for its word. If room is read from a count of
   words landed, it can be one word too many. † **Two counts:** the words landed, for the core and its DMA; and the
   words landed and still to land, for the room and the stop bit. The same for a forwarding buffer's "words held".
   My `HARDWARE.md` is silent on it too.
8. **17.6.3, "At half full a queue's stop bit goes up".** On a chain the upper end's stop bit is also up when the
   forwarding buffer down for that stream is half full (`node.cpp` 44). 28.12 has it; 17.6.3 reads as if the queue
   alone decides.
9. **28.16, "the longest a foreground link packet waits for the line: 534 cycles".** That is of 60 runs. In the
   campaign the chapter quotes beside it (7,519 runs) it is **573.** The bound the bench checks is 594 (`chain.h`
   119).
10. **28.16, "the longest wait for an acknowledgement: 2,727 cycles".** Of 60 runs. In the campaign it is **3,316,**
    on a link in step with its lines clean.
11. **28.16, "a full foreground link packet on an idle line ... 561 cycles".** 561 is the average; 574 at most
    (`build/linkcost pass`).
12. **28.16, "consumers that stall up to 1 ms".** A run draws a mean stall of up to 1,000,000 cycles
    (`linkrun.cpp` 128); single stalls are longer.
13. **28.16, "86,515,438 in the longest".** That search was run with the sender's pass at a byte a cycle
    (72f12b9a). Every other search was run again with the fast unit, 36.9 million states in all; this one with
    the fast unit is Quest 242, which is parked. And **953 words,** the fullest queue, needs a word: the campaign
    draws the host's window at 16 or 64 in two runs of five (`linkrun.cpp` 98), and 28.15 rules 64 out. A queue
    fed by a window of 8 cannot pass 767 words: 511 at most when the last header without "stop" left, and a
    window of 256 after it. So 953 is the first ASIC's queue under such a host; I have not traced the run. In 30
    runs with the host's window at 8 the fullest was 615.
14. **17.6.8, feeding Egress's link packets to the node.** `Node::push()` takes one link packet of 1 to 32 words,
    sends it on the foreground with `hops` 0, and offers it only when no forwarded foreground link packet waits
    (`hostlink.cpp` 74–86, 167; `node.cpp` 50). Two things to know. **`hostlink.h` has a queue of its own** between
    `push()` and the link end, `Sizes::send_words`, 1,024 by default and not Egress's 512 (`hostlink.h` 40). And
    **it has no strobe for "taken":** for `egress_credit`, a word has left the queue when `Node::room()` grows.
    The up to 31 words of a frame that are not yet a link packet must be held above `push()`.

## List 2: where the specification is right and the code is wrong or silent

1. **28.14, the reset in a running chain.** Not covered by any search or campaign before this answer. Run now,
   above; not yet a test in the repository.
2. **28.16, "The simulator's link ends ... count every kind of damage, recovery and restart, and report a link
   packet refused for want of room".** `End` and `Node` do not. `hostlink.h` shows a link's `in_step`, `error` and
   stop bits (45–51) and nothing else. The counts are behind it (`link_end.h` 76–79: `refused`, `rounds`,
   `restarts`, `made_wrong`, `not_kept` and the rest) and cannot be reached. † A `counts()` on each class.
3. **17.6.1, `format_error`.** `hostlink.h` has one `error` for a link and no cause. The cause is in the link end
   for one cycle (`link_end.h` 71) and does not come out. † A sticky cause beside `error`.
4. **28.13.2, `error` "sticky until the reset".** So it is in the link end (le 10 is the only place it is cleared).
   `hostlink.h` adds `clear_error()` (80, 114), which the chip has not: the simulator's chip must not call it.
5. **28.12, item 183, a background link packet with data from below.** The code forwards it (`node.cpp` 72), as
   ruled. No run of mine sends one: nothing makes one but a forged header.
6. **28.9, a false acknowledgement, and 28.7, a link packet made wrong.** The campaign of 7,519 runs has neither:
   noise alone makes neither. They are run here by 60 random runs with forged headers and by the searches, and
   the runner's campaign for them (Quest 238) is parked.
7. **The chapter's request names chapter 20's invariant 40.** In the text, invariant 40 is the line between the
   special tiles, and I found no gathered invariant on the host link. If one is meant, I have not checked it.

## Appendix: the program for the reset

Built against the library alone, on `updater-w8` at 5c8ad13a:

```
cd updater_wip/link && make lib
g++ -std=c++17 -O2 -I . reset_chain.cpp build/libhostlink.a -o reset_chain && ./reset_chain
```

It takes about 9 minutes and prints the table above. `./reset_chain 15` runs a tenth of it.

```cpp
// reset_chain.cpp: an ASIC comes out of reset in the middle of a running chain. Built on hostlink.h alone
// (updater-w8, 5c8ad13a). For the specification's 28.14: "its neighbours each raise their error, and the link comes
// back up".
//
// A host and n ASICs under traffic on both streams and replies from every ASIC, with or without noise. At a random
// cycle one ASIC (or every ASIC at once) is reset: held for `hold` cycles, in which it is not clocked and drives
// zeros, then power_on(). Checked:
//   1. no end has an error before the reset;
//   2. after it, each neighbour that was not itself reset has its error up on the link to the reset ASIC;
//   3. no other end has: not the reset ASIC's own, not a link further away;
//   4. every link is in step again, and stays so;
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
                // Two neighbours reset one after the other: the first may be up again, or not, when it hears the second.
                if (again == 2 && int(i) == which) res.first_heard_second = errors(i, true);
                else CHECK(errors(i, true) == want_below, "%s: ASIC %u's lower end: error %d, expected %d", tag, i, errors(i, true), want_below);
                node[i].clear_error();
            }
            CHECK(host.status().error == is_reset(0), "%s: the host's end: error %d, expected %d", tag, host.status().error, is_reset(0));
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

    if (getenv("DEBUG")) {
        printf("t0 %u release %u busy %u all %u\n", t0, release, busy, all);
        const LinkStatus h = host.status();
        printf("host: in step %d err %d said %d%d heard %d%d room %u %u waiting fg %u\n", h.in_step, h.error, h.stop_said[0], h.stop_said[1], h.stop_heard[0], h.stop_heard[1], host.room(FOREGROUND), host.room(BACKGROUND), host.waiting(FOREGROUND));
        for (unsigned i = 0; i < n; i++) {
            const LinkStatus a = node[i].above(), b = node[i].below();
            printf("asic %u above: step %d err %d said %d%d heard %d%d | below: step %d err %d said %d%d heard %d%d | room %u waiting %u %u | host fg/bg taken %llu/%llu of %llu/%llu; replies at host %llu of %llu\n", i,
                   a.in_step, a.error, a.stop_said[0], a.stop_said[1], a.stop_heard[0], a.stop_heard[1], b.in_step, b.error, b.stop_said[0], b.stop_said[1], b.stop_heard[0], b.stop_heard[1],
                   node[i].room(), node[i].waiting(FOREGROUND), node[i].waiting(BACKGROUND),
                   (unsigned long long)(got[0][i].any ? got[0][i].last + 1 : 0), (unsigned long long)(got[1][i].any ? got[1][i].last + 1 : 0), (unsigned long long)hsrc[0].pushed, (unsigned long long)hsrc[1].pushed,
                   (unsigned long long)(at_host[i].any ? at_host[i].last + 1 : 0), (unsigned long long)nsrc[i].pushed);
        }
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
    const unsigned SEEDS = argc == 2 ? unsigned(atoi(argv[1])) : 150;
    if (argc == 7) { run(strtoull(argv[1], 0, 10), unsigned(atoi(argv[2])), atoi(argv[3]), unsigned(atoi(argv[4])), atof(argv[5]), atoi(argv[6])); printf("%d checks failed\n", failures); return failures != 0; }
    struct Set { const char* what; unsigned n; int which; unsigned hold; double noise; unsigned seeds; int again; };
    const Set sets[] = {
        {"the middle ASIC of 3, no hold, no noise", 3, 1, 0, 0, SEEDS, 0},
        {"the middle ASIC of 3, held up to 6,000 cycles, no noise", 3, 1, 6000, 0, SEEDS, 0},
        {"the middle ASIC of 3, held up to 6,000 cycles, noise", 3, 1, 6000, 2e-4, SEEDS, 0},
        {"the first ASIC of 3 (its upper neighbour is the host), noise", 3, 0, 6000, 2e-4, SEEDS, 0},
        {"the last ASIC of 3 (no neighbour below), noise", 3, 2, 6000, 2e-4, SEEDS, 0},
        {"an ASIC of 4, any, noise", 4, -2, 6000, 2e-4, SEEDS, 0},
        {"an ASIC of 16, any, noise", 16, -2, 6000, 1e-4, SEEDS / 5, 0},
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
    printf("\n%u runs; %d checks failed\n", total, failures);
    return failures ? 1 : 0;
}
```
