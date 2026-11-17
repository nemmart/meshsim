// link_params.h: the link's sizes, the rules taken where HOST-LINK.md ("HL") is silent, and the planted bugs.
// Every rule is a switch: WORKER8/QUESTIONS.md has, for each, the case that fails with it off (cases/).
#pragma once

#include <cstdint>

namespace hl {

constexpr unsigned MAX_WORDS = 32;     // HL: a packet's data, at most

// The rules of QUESTIONS.md, by its numbers. as_written() is the least that makes HL run: the header of 20 bytes,
// one status timer a direction, and nothing where HL gives no rule.
struct Rules {
    bool resend_ahead = true;      // G6: a good packet ahead of the one expected owes a "resend"
    bool ask_every = false;        // G6, the other way: asked at every such packet, not once
    bool zero_number = true;       // G7: a stream's next number, in any header, ahead of the one expected owes a "resend"
    bool lost_answers = true;      // G9: a lost side answers the other side's "lost"
    bool flush = true;             // G10: after losing its place a receiver waits for a run of zeros
    bool refuse = true;            // G11: a good packet with no room is thrown away, not acknowledged
    bool long_is_lost = true;      // G14: a length over the most, under a good header CRC, is a lost place
    bool stop_lift_back = false;   // G17: at "stop" lifted the sender goes back to the oldest not acknowledged
    bool ack_now = true;           // G18 (owner, 2026-11-17): an acknowledgement owed goes out as soon as the line is free
    bool lost_uses_ack = true;     // G19: a good packet while lost gives its acknowledge fields and stop bits
    bool restart = true;           // G4: the restart handshake
    bool tell_behind = true;       // G20: acknowledgements that go backwards, or pass what was sent: the link starts again
    bool check_kept = true;        // G21: a sender checks a kept packet's data against its checksum as it sends it
    bool made_wrong = true;        // G22: a packet that passes both checks with a field that cannot be: the link starts again

    static Rules as_written() {
        Rules r;
        r.resend_ahead = r.zero_number = r.lost_answers = r.flush = r.refuse = r.long_is_lost = r.ack_now = false;
        r.lost_uses_ack = r.restart = r.tell_behind = r.check_kept = r.made_wrong = false;
        return r;
    }
};

// A flaw put in on purpose, one at a time, to show that the checks catch it. Compiled out unless LINK_PLANTS.
enum class Plant : uint8_t {
    NONE,
    ACK_ONE_OFF,          // the acknowledge field sent is one more than the last packet received
    WINDOW_NOT_KEPT,      // a new packet is taken with the window full, over the oldest kept
    WRAP_WRONG,           // the number after the last is last + 1, not 1
    TIMER_NOT_RESTARTED,  // the status timer fires once and never again
    DUPLICATE_TAKEN,      // a packet one behind the expected is taken as well
    STOP_IGNORED,         // the other side's stop bit is not obeyed
    RECOVERY_ONCE,        // "lost" is answered with one round only, however often it is said
    ZERO_TAKES_NUMBER,    // a packet of length 0 advances the sender's number
    ACK_FROM_BAD_DATA,    // the acknowledge field is used from a packet whose data failed its check
    NO_ZEROS,             // a recovery round has no zeros
    OTHER_STREAM_STALE,   // a packet with data says nothing new of the other stream: its acknowledge field as last sent
    HOPS_NOT_COUNTED,     // a forwarded packet keeps its hops (the node's)
    NOT_SEALED_AGAIN,     // a forwarded packet's header CRC is the incoming one's (the node's)
    OWN_BEFORE_FORWARDED, // a node sends its own packet before one it is forwarding (the node's)
    COUNT
};

struct Params {
    unsigned number_bits = 18;     // numbers 1 to 2^bits - 2
    unsigned max_words = 32;       // the most words in a packet
    unsigned window = 8;           // the packets kept a stream; under half the numbers
    unsigned zeros = 1024;         // the zeros of a recovery round, in bytes
    unsigned flush_zeros = 532;    // G10: the zeros in a row a lost receiver waits for: what no packet can hold
    unsigned status_timer = 512;   // cycles of silence before the status is sent again
    unsigned resend_timer = 0;     // G1: cycles before an unacknowledged packet is sent again unasked; 0 is never
    unsigned reask = 2048;         // G7: cycles before "resend" is asked again when the sender still shows itself ahead
    Rules rules;
    Plant plant = Plant::NONE;

    uint32_t last_number() const { return (1u << number_bits) - 2; }

    // The shrunken model's sizes (NOTES.md, section 2), in the real order: the status timer, under the zeros a lost
    // receiver waits for, under a recovery's zeros (512, 532, 1,024).
    static Params tiny() {
        Params p;
        p.number_bits = 3; p.max_words = 1; p.window = 2; p.zeros = 44; p.flush_zeros = 36; p.status_timer = 24; p.reask = 96;
        return p;
    }
};

#ifdef LINK_PLANTS
#define HL_PLANT(p, which) ((p).plant == Plant::which)
#else
#define HL_PLANT(p, which) false
#endif

}  // namespace hl
