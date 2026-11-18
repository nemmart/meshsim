// link_params.h: the link's sizes, its rules as switches, and the planted bugs.
//
// The rules are HOST-LINK.md's ("HL") as it stands on 2026-11-17, after the owner's rulings. Each is a switch so
// that a kept case (cases/) can show what goes wrong without it. Rules() is HL as it stands.
#pragma once

#include <cstdint>

namespace hl {

constexpr unsigned MAX_WORDS = 32;     // HL: a packet's data, at most

struct Rules {
    bool resend_ahead = true;      // a good packet ahead of the one expected is thrown away and a resend asked for
    bool zero_number = true;       // an empty packet's number ahead of the one expected: a resend is asked for
    bool reask_any = true;         // asking again after the wait: at any sign that the sender is still ahead, a packet
                                   // with data included (off: only at an empty packet's number; Quest 227's two seeds)
    bool lost_answers = true;      // a lost end that hears "lost" answers it
    bool flush = true;             // a lost receiver takes nothing until it has seen the zeros
    bool refuse = true;            // a good packet with no room is thrown away and not acknowledged
    bool ack_now = true;           // an acknowledgement goes out as soon as the line is free
    bool lost_uses_ack = true;     // a good packet while lost: its acknowledge field and stop bits are taken
    bool restart = true;           // the restart handshake
    bool wrong_twice = true;       // a packet made wrong: asked for again once; the second time running, the error
    bool ask_not_kept = true;      // the other end asks for a packet no longer kept: the error, and the link starts again
    bool mask = true;              // the data is masked on the wire (off: data that holds a packet can be taken for one)
    bool stop_holds_again = false; // HL as first written: "a stream that is stopped simply has no packets sent", a packet
                                   // to be sent again too. Unsound (cases/stopped-blocks.case). HL now: "stop" holds only
                                   // packets not yet numbered; one that has a number is sent again whatever the stop bit
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
    HOPS_NOT_COUNTED,     // a forwarded packet keeps its hops (the node's)
    NOT_SEALED_AGAIN,     // a forwarded packet's header CRC is made before hops is counted
    OWN_BEFORE_FORWARDED, // a node sends its own packet before one it is forwarding (the node's)
    WRONG_FLAG_CLEARED,   // the "made wrong" flag is cleared by a packet that is thrown away
    ERROR_ON_RELEASE,     // an error is raised on an acknowledgement behind, with nothing asked for
    RESTART_ANY_DATA,     // a restart packet is acted on whatever its data
    CHECKSUM_KEPT,        // a packet sent again goes with the checksum of its first sending
    MASK_KEPT,            // a packet sent again is masked from the header CRC of its first sending
    COUNT
};

struct Params {
    unsigned number_bits = 18;     // numbers 1 to 2^bits - 2
    unsigned max_words = 32;       // the most words in a packet
    unsigned window = 8;           // the packets kept for a direction, of both streams together
    unsigned zeros = 1024;         // the zeros of a recovery round, in bytes
    unsigned flush_zeros = 528;    // the zeros in a row a lost receiver waits for: a full packet's length
    unsigned status_timer = 512;   // cycles of silence before the status is sent again
    unsigned reask = 2048;         // cycles before a resend is asked for again
    unsigned pass = 16;            // the bytes a cycle of the sender's pass over a packet's data before it leaves: HL's fast
                                   // unit of 128 bits a cycle (1: a byte a cycle; 0: no pass)
    unsigned restart_words = 32;   // the restart packet's length (the shrunken model's is shorter)
    Rules rules;
    Plant plant = Plant::NONE;

    uint32_t last_number() const { return (1u << number_bits) - 2; }

    // The shrunken model's sizes, in the real order: the status timer, under the zeros a lost receiver waits for,
    // under a recovery's zeros (512, 528, 1,024).
    static Params tiny() {
        Params p;
        p.number_bits = 3; p.max_words = 1; p.window = 2; p.zeros = 40; p.flush_zeros = 32; p.status_timer = 24; p.reask = 96;
        p.restart_words = 1;
        return p;
    }
};

#ifdef LINK_PLANTS
#define HL_PLANT(p, which) ((p).plant == Plant::which)
#else
#define HL_PLANT(p, which) false
#endif

}  // namespace hl
