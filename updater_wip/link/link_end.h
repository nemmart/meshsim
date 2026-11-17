// link_end.h: one end of one link of HOST-LINK.md ("HL"), as a state machine in the shape of hardware: its
// registers in one struct (Regs), the state enumerations among them, and one function, step(), that advances a
// clock: a byte in and a byte out each cycle. It knows nothing of the chain: a node is two of these.
//
// THE RECEIVER finds a packet's start (the first byte that is not zero), checks the header CRC, the fields, the
// data checksum and the number, in HL's order, and hands a good packet in order to its owner in the cycle after
// its last byte (rx_valid). THE SENDER keeps what it has sent until it is acknowledged, sends again from the
// oldest not acknowledged, sends the status, and obeys the other side's stop bit. THE LINK'S MODE is two bits:
// lost (this end has lost its place) and answer (the other end has said so); and two more for the restart
// handshake proposed in QUESTIONS.md (G4): restart (this end has started again) and restart_answer.
//
// What HL does not say is in Rules (link_params.h), each rule a switch, each argued in WORKER8/QUESTIONS.md.
// step() runs its three parts in order (receive, timers, send), each seeing what the one before it wrote: where
// two write one register in the same cycle, the later wins.
#pragma once

#include <cstdint>
#include <vector>

#include "archive.h"
#include "header.h"
#include "link_params.h"

namespace hl {

// A packet offered to the sender: its data stays valid for the cycle.
struct Offer {
    uint8_t length = 0, hops = 0;
    uint32_t dsum = 0;                     // the data checksum: computed by whoever made the packet, kept by who forwards it
    const uint8_t* data = nullptr;
};

// A cycle's inputs.
struct In {
    uint8_t rx_byte = 0;
    bool say_stop[STREAMS] = {};           // the level of each stream's stop bit
    uint32_t rx_free[STREAMS] = {};        // the words the owner can take on each stream this cycle
    bool rx_forwards = false;              // a packet received here would be forwarded: hops 15 cannot be
    const Offer* offer[STREAMS] = {};      // a new packet to send on each stream, or null
    bool power_on = false;                 // this cycle the end starts again from power-on
};

// What step() took from the cycle's offers: the handshake's other half.
struct Taken { bool tx[STREAMS] = {}; };

enum class RxState : uint8_t { HUNT, HEADER, DATA, SKIP, FLUSH };
enum class TxState : uint8_t { IDLE, HEADER, DATA, ZEROS };

// What the sender started this cycle: for the wire's aimed damage and the counts. The receiver never sees it.
enum class Kind : uint8_t { NONE, DATA, RESENT, STATUS, LOST, RECOVERY, BOTH, RESTART };
struct Tap {
    bool start = false, end = false;       // the byte now on the wire is a packet's first; a packet's last
    Kind kind = Kind::NONE;
    uint8_t hdr[HEADER_BYTES] = {};        // the header starting
    unsigned bytes = 0;                    // the packet's bytes, header and data
};

// What happened in the cycle: where the search stops to choose, and what the counts count.
struct Events {
    bool rx_good = false, rx_bad_data = false, rx_bad_fields = false, rx_bad_header = false;
    bool delivered = false, refused = false, ahead = false, behind = false;
    bool went_back = false, zeros_start = false, mode_change = false, restarted = false;
    int stop_taken[STREAMS] = {-1, -1};     // the stop bit this end took from a packet this cycle, for each stream; -1 for none
    bool any() const { return rx_good || rx_bad_data || rx_bad_fields || rx_bad_header || zeros_start || mode_change; }
};

struct Counts {
    uint64_t sent[8] = {}, rx_good = 0, rx_bad_data = 0, rx_bad_fields = 0, rx_bad_header = 0;
    uint64_t delivered = 0, refused = 0, ahead = 0, behind = 0, went_back = 0, timer_back = 0;
    uint64_t ack_behind = 0, ack_beyond = 0, rounds = 0, restarts = 0, kept_bad = 0;
    uint32_t most_kept = 0;
};

class LinkEnd {
public:
    struct StreamRegs {
        // receiving
        uint32_t last = 0;             // the last number received in order, 0 for none yet: the acknowledge field sent
        bool need = false;             // a packet was thrown away: "resend" is owed until the expected one comes
        bool asked = false;            // "resend" has gone out since
        bool stop_sent = false;        // the stop bit in the last header sent
        bool due = false;              // a status is due
        bool ack_owed = false;
        uint32_t stimer = 0;           // G13: cycles since this stream's last header
        // sending
        uint32_t acked = 0;            // the last acknowledge value taken: the oldest kept is the number after it
        uint32_t next = 1;             // the number that goes on the wire next
        uint32_t tail = 1;             // the number the next new packet takes
        uint32_t head = 0;             // the slot of the oldest kept
        bool peer_stop = false;        // the other side's stop bit
        uint32_t rtimer = 0;           // G1
        uint8_t behind = 0;            // G20: good packets in a row whose acknowledge field went backwards
        std::vector<uint8_t> meta;     // a slot's length, hops and data checksum: 6 bytes
        std::vector<uint8_t> data;     // a slot's words
    };
    struct Regs {
        // the receiver
        RxState rx_state = RxState::HUNT;
        uint32_t rx_count = 0, rx_bytes = 0, rx_crc = 0, zero_run = 0;
        uint8_t rx_hdr[HEADER_BYTES] = {};
        bool rx_valid = false;         // the packet in rx_buf is handed over this cycle
        uint8_t rx_stream = 0, rx_length = 0, rx_hops = 0;   // of the packet handed over
        uint32_t rx_dsum = 0;
        std::vector<uint8_t> rx_buf;   // the packet buffer: one packet's data
        // the sender
        TxState tx_state = TxState::IDLE;
        uint32_t tx_count = 0, tx_bytes = 0;
        uint8_t tx_hdr[HEADER_BYTES] = {};
        uint8_t tx_stream = 0, tx_slot = 0;
        bool tx_data = false;          // the packet being sent has data
        uint8_t tx_byte = 0;           // the wire
        // the link
        bool lost = false;             // this end has lost its place
        bool answer = false;           // the other end has: this end answers
        bool restart = false;          // G4: this end has started again and waits for the answer
        bool restart_answer = false;   // G4: the other end has started again: this end answers
        bool error = false;            // sticky: the link started again under this end, and what it kept is gone
        uint8_t round = 0;             // a recovery round: 0 the zeros, 1 and 2 the streams' packets
        uint32_t status_timer = 0;     // HL's: cycles since the end of the last packet
        bool timer_dead = false;       // a planted bug's
        bool answered = false;         // a planted bug's
        StreamRegs s[STREAMS];
    };

    Params p;
    Numbers nums;
    Regs r;
    Tap tap;         // of the last step
    Events ev;       // of the last step
    Counts count;

    LinkEnd() : LinkEnd(Params()) {}
    explicit LinkEnd(const Params& params) : p(params), nums{params.last_number()} { power_on(); }

    void power_on();                       // every register as at power-on
    Taken step(const In& in);

    // The packet handed over (while r.rx_valid).
    const uint8_t* rx_data() const { return r.rx_buf.data(); }

    uint32_t base(unsigned s) const { return nums.succ(r.s[s].acked); }
    uint32_t kept(unsigned s) const { return nums.dist(base(s), r.s[s].tail); }
    uint32_t expect(unsigned s) const { return nums.succ(r.s[s].last); }
    bool running() const { return !r.lost && !r.answer && !r.restart && !r.restart_answer; }

    template <class A> void io(A& a) {
        a(r.rx_state); a(r.rx_count); a(r.rx_bytes); a(r.rx_crc); a(r.zero_run); a(r.rx_hdr); a(r.rx_valid);
        a(r.rx_stream); a(r.rx_length); a(r.rx_hops); a(r.rx_dsum); a.bytes(r.rx_buf, r.rx_buf.size());
        a(r.tx_state); a(r.tx_count); a(r.tx_bytes); a(r.tx_hdr); a(r.tx_stream); a(r.tx_slot); a(r.tx_data); a(r.tx_byte);
        a(r.lost); a(r.answer); a(r.restart); a(r.restart_answer); a(r.error); a(r.round); a(r.status_timer); a(r.timer_dead); a(r.answered);
        for (auto& st : r.s) {
            a(st.last); a(st.need); a(st.asked); a(st.stop_sent); a(st.due); a(st.ack_owed); a(st.stimer);
            a(st.acked); a(st.next); a(st.tail); a(st.head); a(st.peer_stop); a(st.rtimer); a(st.behind);
            a.bytes(st.meta, st.meta.size()); a.bytes(st.data, st.data.size());
        }
    }

    // The registers that can still matter, in a form that does not depend on what no longer does (a header already
    // judged, a slot already released, which slot is the oldest): two ends that write the same bytes here behave
    // the same from here on. The search knows a state by this.
    void canon(Writer& a) const;

private:
    static constexpr unsigned META = 6;
    unsigned slot_bytes() const { return p.max_words * WORD_BYTES; }

    void receive(const In& in);
    void header_end(const In& in);
    void packet_end(const In& in, bool data_ok);
    void good_packet(const In& in, const Header& h);
    void bad_fields(const Header& h);
    void thrown_away(unsigned s, uint32_t number, bool has_data);
    void lose_place();
    void mode_changed(bool was_running);
    bool take_ack(unsigned s, uint32_t ack);
    void declare();
    void go_back(unsigned s);
    void start_again(bool report);
    void timers(const In& in);
    void send(const In& in, Taken& t);
    void choose(const In& in, Taken& t);
    void start_header(const In& in, unsigned s, uint8_t flags, uint8_t reserved, bool data, Kind kind);
    void packet_sent();
};

}  // namespace hl
