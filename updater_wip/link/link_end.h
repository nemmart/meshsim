// link_end.h: one end of one link of HOST-LINK.md ("HL", as it stands on 2026-11-17), as a state machine in the
// shape of hardware: its registers in one struct (Regs), the state enumerations among them, and one function,
// step(), that advances a clock: a byte in and a byte out each cycle. It knows nothing of the chain: a node is two
// of these.
//
// ONE RUN OF PACKET NUMBERS for each direction, shared by both streams: one acknowledgement, one resend flag, one
// window, one set of kept packets. What is a stream's is its stop bit, and which queue a packet's data goes to.
//
// THE RECEIVER finds a packet's start (the first byte that is not zero), checks the header CRC, the fields, the
// data checksum and the number, in HL's order, and hands a good packet in order to its owner in the cycle after
// its last byte (rx_valid). THE SENDER keeps the data of what it has sent until it is acknowledged, sends again
// from the oldest not acknowledged, sends the status, and obeys the other side's stop bits. It makes both checks
// afresh for every packet that goes out, and passes over a packet's data to do so before the header leaves.
// THE LINK'S MODE is four bits: lost (this end has lost its place), answer (the other end has said so), and the
// same two for starting the link again: restart and restart_answer.
//
// The rules are switches (link_params.h), so that a kept case can show what each is for. step() runs its parts in
// order (receive, timers, send), each seeing what the one before it wrote: where two write one register in the
// same cycle, the later wins.
#pragma once

#include <cstdint>
#include <vector>

#include "archive.h"
#include "header.h"
#include "link_params.h"

namespace hl {

// A packet offered to the sender: its data stays valid for the cycle. The sender makes the checksum.
struct Offer {
    uint8_t length = 0, hops = 0;
    uint32_t id = 0;                       // which packet this is, to its owner: the same while it is the same packet
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
    bool not_kept = false, wrong_twice = false;   // why this end started the link again, this cycle
    int stop_taken[STREAMS] = {-1, -1};     // the stop bit this end took from a packet this cycle, for each stream; -1 for none
    bool any() const { return rx_good || rx_bad_data || rx_bad_fields || rx_bad_header || zeros_start || mode_change; }
};

struct Counts {
    uint64_t sent[8] = {}, rx_good = 0, rx_bad_data = 0, rx_bad_fields = 0, rx_bad_header = 0;
    uint64_t delivered = 0, refused = 0, ahead = 0, behind = 0, went_back = 0;
    uint64_t ack_behind = 0, ack_beyond = 0, rounds = 0, restarts = 0, not_kept = 0, made_wrong = 0, wrong_once = 0;
    uint64_t bytes_data = 0, bytes_status = 0, bytes_round = 0;   // sent: packets with data; of length 0 in step; a recovery's or a restart's
    uint64_t pass_wait = 0;                // cycles the line stood idle while a packet's data was being passed over
    uint32_t most_kept = 0;
};

class LinkEnd {
public:
    struct Regs {
        // the receiver
        RxState rx_state = RxState::HUNT;
        uint32_t rx_count = 0, rx_bytes = 0, rx_crc = 0, zero_run = 0;
        uint8_t rx_hdr[HEADER_BYTES] = {};
        bool rx_valid = false;         // the packet in rx_buf is handed over this cycle
        uint8_t rx_stream = 0, rx_length = 0, rx_hops = 0;   // of the packet handed over
        std::vector<uint8_t> rx_buf;   // the packet buffer: one packet's data
        uint32_t last = 0;             // the last number taken in order, 0 for none yet: the acknowledge field sent
        bool need = false;             // a packet was thrown away: a resend is owed until the expected one comes
        bool asked = false;            // the resend has gone out since
        uint32_t ask_timer = 0;        // cycles since it went: asked again only after p.reask
        bool wrong = false;            // HL, "A packet made wrong": the packet expected has failed a field test once
        bool ack_owed = false;         // a packet was taken since the last header
        bool stop_sent[STREAMS] = {};  // the stop bits in the last header sent
        // the sender
        TxState tx_state = TxState::IDLE;
        uint32_t tx_count = 0, tx_bytes = 0;
        uint8_t tx_hdr[HEADER_BYTES] = {};
        uint8_t tx_slot = 0;
        bool tx_data = false;          // the packet being sent has data
        bool tx_pattern = false;       // and the data is the restart packet's
        uint8_t tx_byte = 0;           // the wire
        uint32_t acked = 0;            // the last acknowledge value taken: the oldest kept is the number after it
        uint32_t next = 1;             // the number that goes on the wire next
        uint32_t tail = 1;             // the number the next new packet takes
        uint32_t head = 0;             // the slot of the oldest kept
        bool peer_stop[STREAMS] = {};  // the other side's stop bits
        std::vector<uint8_t> meta;     // a slot's stream, length and hops (and, for a planted bug, its first checksum)
        std::vector<uint8_t> data;     // a slot's words
        uint64_t prep_key = 0;         // the packet whose data the sending unit is passing over, or has; 0 for none
        uint32_t prep_left = 0;        // the cycles of that pass still to go
        // the link
        bool lost = false;             // this end has lost its place
        bool answer = false;           // the other end has: this end answers
        bool restart = false;          // this end has started again and waits for the answer
        bool restart_answer = false;   // the other end has started again: this end answers
        bool error = false;            // sticky: the link started again under this end, and what it kept is gone
        uint8_t round = 0;             // a recovery round: 0 the zeros, 1 its packet
        bool due = false;              // a status is due
        uint32_t status_timer = 0;     // HL's: cycles since the end of the last packet
        bool timer_dead = false;       // a planted bug's
        bool answered = false;         // a planted bug's
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

    uint32_t base() const { return nums.succ(r.acked); }
    uint32_t kept() const { return nums.dist(base(), r.tail); }
    uint32_t expect() const { return nums.succ(r.last); }
    bool running() const { return !r.lost && !r.answer && !r.restart && !r.restart_answer; }
    unsigned slot_of(uint32_t number) const { return (r.head + nums.dist(base(), number)) % p.window; }
    unsigned slot_stream(unsigned slot) const { return r.meta[size_t(slot) * META]; }
    // Among the packets still to be sent again, is one the foreground's? (for the benches' properties)
    bool again_has(unsigned stream) const;
    uint32_t restart_sum() const;          // the data checksum of this model's restart packet

    template <class A> void io(A& a) {
        a(r.rx_state); a(r.rx_count); a(r.rx_bytes); a(r.rx_crc); a(r.zero_run); a(r.rx_hdr); a(r.rx_valid);
        a(r.rx_stream); a(r.rx_length); a(r.rx_hops); a.bytes(r.rx_buf, r.rx_buf.size());
        a(r.last); a(r.need); a(r.asked); a(r.ask_timer); a(r.wrong); a(r.ack_owed); a(r.stop_sent);
        a(r.tx_state); a(r.tx_count); a(r.tx_bytes); a(r.tx_hdr); a(r.tx_slot); a(r.tx_data); a(r.tx_pattern); a(r.tx_byte);
        a(r.acked); a(r.next); a(r.tail); a(r.head); a(r.peer_stop); a.bytes(r.meta, r.meta.size()); a.bytes(r.data, r.data.size());
        a(r.prep_key); a(r.prep_left);
        a(r.lost); a(r.answer); a(r.restart); a(r.restart_answer); a(r.error); a(r.round); a(r.due); a(r.status_timer);
        a(r.timer_dead); a(r.answered);
    }
    // The registers that can still matter, in a form that does not depend on what no longer does (a header already
    // judged, a slot already released, which slot is the oldest): two ends that write the same bytes here behave
    // the same from here on. The search knows a state by this.
    void canon(Writer& a) const;

private:
    static constexpr unsigned META = 7;
    unsigned slot_bytes() const { return p.max_words * WORD_BYTES; }
    struct Pick { int kind = 0; unsigned stream = 0; uint8_t length = 0; uint64_t key = 0; };   // 0 none, 1 again, 2 new

    void receive(const In& in);
    void header_end(const In& in);
    void packet_end(const In& in, bool data_ok);
    void good_packet(const In& in, const Header& h);
    void bad_fields(const Header& h, bool data_ok);
    bool made_wrong();
    void thrown_away(uint32_t number, bool has_data);
    void lose_place();
    void mode_changed(bool was_running);
    int take_ack(uint32_t ack);
    void go_back();
    void start_again(bool report);
    void declare();
    void timers(const In& in);
    Pick pick(const In& in) const;
    void send(const In& in, Taken& t);
    void choose(const In& in, Taken& t, const Pick& c);
    void start_header(const In& in, unsigned stream, uint8_t flags, uint8_t length, uint8_t hops, uint32_t dsum, Kind kind);
    void packet_sent();
};

}  // namespace hl
