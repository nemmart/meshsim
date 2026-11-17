// bench.h: one link on the bench: two link ends, the wire between them in each direction, a queue behind each
// stream at each end, a source in front of each, and the monitor. Everything that is not the link end is as simple
// as it can be: the queues are counts, a source hands over numbered packets, and the monitor knows what every
// delivered word must be.
//
// The same bench runs a kept case (cases/, a script of events), a random run and the search: all three are a
// config and a list of things done to it from outside, between cycles.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "link_end.h"

namespace hl {

// What the wire does to one packet (aimed damage; NOTES.md section 7). The forged ones change a field and seal the
// header again, so that it passes its CRC: a wire makes one in 2^32, and they are reported apart.
enum class Fate : uint8_t {
    WHOLE, GONE, FIRST0, HEADER, DATA,
    FORGE_ACK, FORGE_STOP, FORGE_LOST, FORGE_RECOVERY, FORGE_RESEND, FORGE_NUMBER,
    FORGE_ACK_DATA,                        // a forged acknowledge field on a packet whose data is damaged too: must change nothing
    COUNT
};
const char* fate_name(Fate f);
bool fate_from(const std::string& name, Fate& f);

// One direction of the link: a delay line, and the damage.
struct Wire {
    std::vector<uint8_t> ring;             // the bytes on their way, the oldest at pos
    uint32_t pos = 0;
    uint32_t at = 0, len = 0;              // the packet passing: its next byte's index, and its bytes
    Fate fate = Fate::WHOLE;
    uint8_t forged[HEADER_BYTES] = {};
    uint8_t hdr[HEADER_BYTES] = {};        // the passing packet's header as sent (from the tap)
    uint32_t ordinal = 0;                  // the packets started so far: how a script names one

    explicit Wire(unsigned delay = 1) : ring(delay, 0) {}
    uint8_t out() const { return ring[pos]; }
    void push(uint8_t b, const Tap& tap);
    void set_fate(Fate f, const Numbers& nums);     // just after the push of a packet's first byte
    static bool forges(Fate f) { return f > Fate::DATA; }
    bool idle() const { return at >= len; }
    bool noise();                                   // a byte put into the idle wire, where the last byte went
    template <class A> void io(A& a) {
        std::vector<uint8_t> turned(ring.size());
        for (size_t i = 0; i < ring.size(); i++) turned[i] = ring[(pos + i) % ring.size()];
        a.bytes(turned, turned.size());
        ring = turned; pos = 0;
        a(at); a(len); a(fate); a(forged); a(hdr); a(ordinal);
    }
    void canon(Writer& a) const {
        for (size_t i = 0; i < ring.size(); i++) a(ring[(pos + i) % ring.size()]);
        const uint32_t left = len - at; a(left);
        if (left) { a(at); a(fate); if (fate > Fate::DATA) a(forged); }
        if (at == 1) a(hdr);               // just started: its fate may still be chosen
    }
private:
    uint8_t damaged(uint32_t index, uint8_t b) const;
};

struct Config {
    Params p;                              // both ends alike
    unsigned queue_words = 512;            // each stream's queue at each end; "stop" at half
    unsigned delay = 1;                    // the wire's, in cycles, each way
    unsigned words = 32;                   // the words in each packet a source makes
    unsigned packets[2][STREAMS] = {};     // how many packets each source has: [end][stream]
    bool images = false;                   // every word a source makes is the image of a good header (G10's case)
};

struct Source { uint32_t total = 0, made = 0, avail = 0; };   // its packets; those taken by the end; those let out so far

class Link1 {
public:
    Config cfg;
    LinkEnd end[2];
    Wire wire[2];                          // wire[e] carries end e's bytes to the other end
    Source src[2][STREAMS];
    uint32_t fill[2][STREAMS] = {};        // the words waiting in each queue: [end][stream]
    uint32_t most_fill[2][STREAMS] = {};
    uint64_t delivered[2][STREAMS] = {};   // the words delivered into each queue: the next is this index of its flow
    bool gaps_allowed = false;             // after a restart words may be missing, never repeated or out of order
    bool fresh[2][STREAMS] = {};           // end e was powered on: what it had is gone, and its next word may be any of its flow
    bool must_report = false;
    bool excused = false;                  // something happened that may end in the link starting again: a forged header that
                                           // was believed, a memory's flipped bit, a power-on. Without it, starting again is a failure
    bool stopped[2][STREAMS] = {};         // the stop bit end e last took, each stream: it must not start data there              // an end started the link again: an error must be up at an end that was not powered on
    bool power[2] = {};                    // an end starts again from power-on in the next cycle
    uint32_t budget[3] = {};               // the search's: packets it may still damage, bytes of noise, forged headers
    uint64_t t = 0;
    std::string bad;                       // the first property that failed, or empty
    bool stop = false;                     // something happened in the last cycle that the search stops at
    bool started[2] = {};                  // end e started a packet in the last cycle

    explicit Link1(const Config& c);
    void step();

    // From outside, between cycles.
    void offer(unsigned e, unsigned s) { if (src[e][s].avail < src[e][s].total) src[e][s].avail++; }
    void drain(unsigned e, unsigned s, uint32_t n) { fill[e][s] -= n == 0 || n > fill[e][s] ? fill[e][s] : n; }
    void fate(unsigned w, Fate f) { wire[w].set_fate(f, end[w].nums); if (Wire::forges(f) && f != Fate::FORGE_ACK_DATA) excused = true; }
    bool flip(unsigned e, unsigned s);     // a bit of the oldest packet end e keeps on stream s: a memory's fault, not the wire's
    void word(unsigned from, unsigned stream, uint64_t index, uint8_t out[WORD_BYTES]) const;   // what a flow carries at an index

    bool done() const;                     // everything sent has arrived and been acknowledged, and both ends are in step
    // From here nothing more goes wrong: every source lets everything out, every queue is emptied every cycle, the
    // wire is whole. Returns the cycles it took to be done, or -1 if it is not within the bound (bad then says so).
    long quiet(uint64_t bound);
    FILE* trace = nullptr;                 // if set, a line for each thing that happens

    template <class A> void io(A& a) {
        for (auto& e : end) e.io(a);
        for (auto& w : wire) w.io(a);
        for (auto& e : src) for (auto& s : e) { a(s.made); a(s.avail); }
        a(fill); a(delivered); a(gaps_allowed); a(fresh); a(must_report); a(excused); a(stopped); a(power); a(budget); a(t); a(started);
    }
    // What the search knows a state by: the hash of everything that can still matter (LinkEnd::canon).
    Hash128 canon() const;
    std::vector<uint8_t> whole();          // the same with nothing left out but the cycle number: to check canon()
    std::vector<uint8_t> save();
    void load(const std::vector<uint8_t>& bytes);

private:
    void deliver(unsigned e);
};

// The word a flow carries at an index: never zero, and different at every index and flow.
void flow_word(unsigned from, unsigned stream, uint64_t index, uint8_t out[WORD_BYTES]);

// ---- a kept case: a config, a list of events, and what must come of it under one or more sets of rules ----

struct Event {
    enum What : uint8_t { FATE, NOISE, OFFER, DRAIN, POWER, FLIP } what;
    uint64_t cycle = 0;                    // FATE: the packet's ordinal on its wire, from 1
    uint8_t a = 0, b = 0;                  // the wire or the end; the stream
    uint32_t n = 0;                        // FATE: the fate; DRAIN: the words, 0 for all
};
struct Run { std::string rules; bool ok = true; std::string text; };   // a rules line, and what is expected of it
struct Case {
    Config cfg;
    std::vector<Event> events;
    uint64_t quiet_at = 0, bound = 100000;
    std::vector<Run> runs;
};

// "as_written" or "proposed", then +name and -name for single rules, and name=value for sizes: one word.
bool apply_rules(const std::string& spec, Params& p, std::string& err);
std::string rules_text(const Params& p);          // the spec that gives p's rules and timers, from "proposed"
bool parse_case(const std::string& path, Case& c, std::string& err);
std::string case_text(const Case& c);
// Plays a case under a rules line: "" if every property held to the end, or the property that failed.
std::string play(const Case& c, const std::string& rules, Link1* out = nullptr, FILE* trace = nullptr);

}  // namespace hl
