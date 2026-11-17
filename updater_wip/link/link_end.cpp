// link_end.cpp: see link_end.h. HL is HOST-LINK.md; Gn is an entry of WORKER8/QUESTIONS.md.
#include "link_end.h"

#include <algorithm>
#include <cstring>

namespace hl {

void LinkEnd::power_on() {
    r = Regs();
    r.rx_buf.assign(slot_bytes(), 0);
    for (auto& st : r.s) { st.meta.assign(size_t(p.window) * META, 0); st.data.assign(size_t(p.window) * slot_bytes(), 0); }
    r.restart = p.rules.restart;           // G4: an end that starts says so, and waits for the answer
    tap = Tap(); ev = Events();
}

Taken LinkEnd::step(const In& in) {
    Taken t;
    tap = Tap(); ev = Events();
    if (in.power_on) { power_on(); return t; }
    r.rx_valid = false;
    receive(in);
    timers(in);
    send(in, t);
    for (unsigned s = 0; s < STREAMS; s++) count.most_kept = std::max(count.most_kept, kept(s));
    return t;
}

void LinkEnd::canon(Writer& a) const {
    auto some = [&](const uint8_t* b, size_t n) { a.out.insert(a.out.end(), b, b + n); };
    a(r.rx_state); a(r.zero_run);
    if (r.rx_state == RxState::HEADER) { a(r.rx_count); some(r.rx_hdr, r.rx_count); }
    if (r.rx_state == RxState::DATA) { a(r.rx_count); some(r.rx_hdr, HEADER_BYTES); some(r.rx_buf.data(), r.rx_count); }
    if (r.rx_state == RxState::SKIP) { a(r.rx_count); some(r.rx_hdr, HEADER_BYTES); }
    a(r.tx_state);
    if (r.tx_state == TxState::HEADER) {
        a(r.tx_count); some(r.tx_hdr + r.tx_count, HEADER_BYTES - r.tx_count); a(r.tx_stream); a(r.tx_data);
        if (r.tx_data) { a(r.tx_bytes); some(&r.s[r.tx_stream].data[size_t(r.tx_slot) * slot_bytes()], r.tx_bytes); }
    }
    if (r.tx_state == TxState::DATA) {
        a(r.tx_stream); const uint32_t left = r.tx_bytes - r.tx_count; a(left);
        some(&r.s[r.tx_stream].data[size_t(r.tx_slot) * slot_bytes() + r.tx_count], left);
    }
    if (r.tx_state == TxState::ZEROS) { const uint32_t left = r.tx_bytes - r.tx_count; a(left); }
    a(r.lost); a(r.answer); a(r.restart); a(r.restart_answer); a(r.error); a(r.timer_dead); a(r.answered);
    if (!running()) a(r.round);
    if (!p.rules.status_each) a(r.status_timer);
    for (unsigned s = 0; s < STREAMS; s++) {
        const StreamRegs& st = r.s[s];
        a(st.last); a(st.need); a(st.asked); a(st.stop_sent); a(st.due); a(st.ack_owed);
        if (p.rules.status_each) a(st.stimer);
        a(st.acked); a(st.next); a(st.tail); a(st.peer_stop); a(st.behind);
        if (p.resend_timer) a(st.rtimer);
        for (uint32_t i = 0; i < kept(s) && i < p.window; i++) {
            const size_t slot = (st.head + i) % p.window;
            some(&st.meta[slot * META], META);
            some(&st.data[slot * slot_bytes()], size_t(st.meta[slot * META]) * WORD_BYTES);
        }
    }
}

// ---- the receiver ----

void LinkEnd::receive(const In& in) {
    const uint8_t b = in.rx_byte;
    r.zero_run = b ? 0 : std::min(r.zero_run + 1, p.flush_zeros);
    switch (r.rx_state) {
    case RxState::FLUSH:                   // G10: out of step until zeros that no packet can hold have gone by
        if (r.zero_run >= p.flush_zeros) r.rx_state = RxState::HUNT;
        break;
    case RxState::HUNT:                    // idle is zeros in any number; the first byte that is not zero starts a header
        if (b) { r.rx_state = RxState::HEADER; r.rx_hdr[0] = b; r.rx_count = 1; r.rx_crc = crc_step(CRC_START, b); }
        break;
    case RxState::HEADER:
        r.rx_hdr[r.rx_count++] = b; r.rx_crc = crc_step(r.rx_crc, b);
        if (r.rx_count == HEADER_BYTES) header_end(in);
        break;
    case RxState::DATA:
        r.rx_buf[r.rx_count++] = b; r.rx_crc = crc_step(r.rx_crc, b);
        if (r.rx_count == r.rx_bytes) { r.rx_state = RxState::HUNT; packet_end(in, crc_result(r.rx_crc) == unpack(r.rx_hdr).dsum); }
        break;
    case RxState::SKIP:                    // the data of a packet with a field that cannot be: passed over
        if (++r.rx_count == r.rx_bytes) { r.rx_state = RxState::HUNT; bad_fields(unpack(r.rx_hdr)); }
        break;
    }
}

// The header's 16th byte has arrived. HL's order: the header CRC, then the fields.
void LinkEnd::header_end(const In& in) {
    if (crc_result(r.rx_crc) != CRC_GOOD_HEADER) { lose_place(); return; }
    const Header h = unpack(r.rx_hdr);
    const bool too_long = h.length > p.max_words;
    if (too_long && p.rules.long_is_lost) { lose_place(); return; }   // G14: how many bytes to pass over is not known
    const uint8_t reserved_ok = p.rules.restart ? R_RESTART : 0;
    const bool cannot_be = (h.stream != 1 && h.stream != 2) || too_long || (h.reserved & ~reserved_ok) != 0 ||
                           (in.rx_forwards && h.hops == 15 && h.length > 0);
    r.rx_bytes = WORD_BYTES * h.length; r.rx_count = 0;
    if (cannot_be) {
        if (h.length == 0) { r.rx_state = RxState::HUNT; bad_fields(h); } else r.rx_state = RxState::SKIP;
    } else if (h.length == 0) {
        r.rx_state = RxState::HUNT; packet_end(in, h.dsum == 0);          // of no data the checksum is 0, and it is checked
    } else {
        r.rx_state = RxState::DATA; r.rx_crc = CRC_START;                 // the unit set back: all ones, not zero
    }
}

// A header CRC failed: the packet's length is unknown, and this end has lost its place (HL, "When a packet is bad").
void LinkEnd::lose_place() {
    ev.rx_bad_header = true; count.rx_bad_header++;
    r.rx_state = p.rules.flush ? RxState::FLUSH : RxState::HUNT;
    const bool was_running = running();
    if (!r.lost) { r.lost = true; ev.mode_change = true; }
    if (!p.rules.lost_answers) r.answer = false;           // as written, a lost side only says "lost"
    mode_changed(was_running);
}

// The link's mode may have changed: a recovery starts with its zeros; back in step, a status goes out at once, so
// that the other side sees an ordinary acknowledgement (HL, "Recovery").
void LinkEnd::mode_changed(bool was_running) {
    if (was_running && !running()) r.round = 0;
    if (!was_running && running()) {
        r.round = 0;
        for (auto& st : r.s) st.due = true;
        if (r.tx_state == TxState::ZEROS) r.tx_state = TxState::IDLE;     // zeros cut short are idle
    }
}

// A good header with a field that cannot be: thrown away like a packet that failed a check (HL, owner).
void LinkEnd::bad_fields(const Header& h) {
    ev.rx_bad_fields = true; count.rx_bad_fields++;
    if (h.stream == 1 || h.stream == 2) thrown_away(h.stream - 1u, h.number, h.length > 0);
    else if (!r.lost && running()) for (auto& st : r.s) { st.need = true; st.asked = false; }   // whose it was is not known
}

// A packet with a credible header was thrown away: "resend" is owed, if what was lost can be sent again.
void LinkEnd::thrown_away(unsigned s, uint32_t number, bool has_data) {
    if (r.lost || !running() || !has_data) return;
    StreamRegs& st = r.s[s];
    if (number == expect(s)) { st.need = true; st.asked = false; }
    else if (nums.ahead(expect(s), number)) { st.need = true; if (p.rules.ask_every) st.asked = false; }
}

// A packet's last byte has arrived, its header good and its fields possible. HL's order: the data checksum, the number.
void LinkEnd::packet_end(const In& in, bool data_ok) {
    const Header h = unpack(r.rx_hdr);
    if (!data_ok) {
        ev.rx_bad_data = true; count.rx_bad_data++;
        if (HL_PLANT(p, ACK_FROM_BAD_DATA)) take_ack(h.stream - 1u, h.ack);
        thrown_away(h.stream - 1u, h.number, h.length > 0);
        return;
    }
    ev.rx_good = true; count.rx_good++;
    good_packet(in, h);
}

// The sender goes back to the oldest packet not acknowledged.
void LinkEnd::go_back(unsigned s) {
    StreamRegs& st = r.s[s];
    if (st.next != base(s)) { ev.went_back = true; count.went_back++; }
    st.next = base(s); st.rtimer = 0;
}

// An acknowledge field from a good packet: what it covers is released. One that is behind, or beyond what was
// sent, is ignored and counted (G3).
//
// G20: the other end's acknowledge field never goes backwards, since a header is written when it is sent and the
// wire keeps its order; and it never passes what this end has sent. One that does either was made by a false
// header: this one, and then the next is right again; or an earlier one (a false acknowledgement that released
// packets that never arrived, a false number that was taken for the next), and then every one from now on is
// wrong. Twice in a row is the second: the numbers disagree for good, and the link starts again and says so.
// Returns true if it did.
bool LinkEnd::take_ack(unsigned s, uint32_t ack) {
    StreamRegs& st = r.s[s];
    if (ack == st.acked) { st.behind = 0; return false; }
    const uint32_t have = kept(s);
    const uint32_t k = (ack == 0 || ack > nums.last) ? nums.last : nums.dist_ack(st.acked, ack);
    if (k > have) {
        (k <= nums.last / 2 ? count.ack_beyond : count.ack_behind)++;
        if (p.rules.tell_behind && p.rules.restart && ++st.behind >= 2) { declare(); return true; }
        return false;
    }
    const uint32_t sent_past = nums.dist(base(s), st.next);
    st.head = (st.head + k) % p.window; st.acked = ack; st.rtimer = 0; st.behind = 0;
    if (sent_past < k) st.next = base(s);                  // the next to send was among those released
    return false;
}

// This end has found that the link cannot go on as it is: it starts again, says so (the error), and asks the other
// end to do the same (G4's handshake).
void LinkEnd::declare() {
    const bool was_running = running();
    start_again(true);
    r.restart = true; ev.mode_change = true;
    mode_changed(was_running);
}

// G4: the link starts again under this end: its numbers, what it kept, and its mode, as at power-on. The packet
// in hand is finished; the receiver keeps its place in the bytes.
void LinkEnd::start_again(bool report) {
    for (auto& st : r.s) {
        st.last = 0; st.need = st.asked = st.due = st.ack_owed = false; st.stimer = 0;
        st.acked = 0; st.next = st.tail = 1; st.head = 0; st.peer_stop = false; st.rtimer = 0; st.behind = 0;
    }
    r.lost = r.answer = false; r.round = 0; r.status_timer = 0;
    if (report) r.error = true;
    ev.restarted = true; count.restarts++;
}

void LinkEnd::good_packet(const In& in, const Header& h) {
    const unsigned s = h.stream - 1u;
    StreamRegs& st = r.s[s];
    const bool was_running = running(), was_lost = r.lost;
    const bool restart_pkt = p.rules.restart && (h.reserved & R_RESTART);
    const bool f_lost = h.flags & F_LOST, f_rec = h.flags & F_RECOVERY;

    // G4, the restart handshake: with the reserved byte's flag, "lost" reads "I have started again" and
    // "recovery" reads "so have I, in answer".
    if (restart_pkt) {
        const bool in_handshake = r.restart || r.restart_answer;        // nothing of this life has gone by yet
        if (r.lost) { r.lost = false; ev.mode_change = true; }            // its place is found
        if (f_rec && r.restart) { r.restart = false; ev.mode_change = true; }
        if (f_lost) {
            if (!in_handshake) start_again(true);                         // what this end kept is gone, and it says so
            if (!r.restart_answer) { r.restart_answer = true; ev.mode_change = true; }
        } else if (r.restart_answer) { r.restart_answer = false; ev.mode_change = true; }
        mode_changed(was_running);
        return;                                            // a restart packet carries nothing else
    }
    if (r.restart) return;                                 // from the other end's life before: nothing in it counts
    if (r.restart_answer) { r.restart_answer = false; ev.mode_change = true; }   // an ordinary packet: the other end is up

    // The link's mode.
    if (!f_lost) r.answered = false;
    const bool hears_lost = f_lost && (!was_lost || p.rules.lost_answers) && !r.answered;   // G9
    if (f_rec && r.lost) { r.lost = false; ev.mode_change = true; }
    if (hears_lost) { if (!r.answer) { r.answer = true; ev.mode_change = true; } }
    else if (!f_lost && r.answer) { r.answer = false; ev.mode_change = true; }
    mode_changed(was_running);

    // The acknowledge field and the stop bit (G3, G19).
    const bool heard = !was_lost || p.rules.lost_uses_ack || f_rec || hears_lost;
    if (heard) {
        if (take_ack(s, h.ack)) return;
        ev.stop_taken[s] = (h.flags & F_STOP) ? 1 : 0;
        const bool stop = (h.flags & F_STOP) && !HL_PLANT(p, STOP_IGNORED);
        if (st.peer_stop && !stop && p.rules.stop_lift_back) go_back(s);    // G17
        st.peer_stop = stop;
        if (hears_lost) go_back(s);                        // "carries on from the packet after the acknowledged one"
        if (h.flags & F_RESEND) go_back(s);
    }

    // The number, which means something only now.
    if (r.lost) return;                                    // G19: out of step, nothing is taken
    const uint32_t want = expect(s);
    if (h.length == 0) {                                   // its field is the number the sender puts on the wire next
        if (h.number == want) st.need = st.asked = false;
        else if (p.rules.zero_number && nums.ahead(want, h.number)) {      // G7: the sender is past a packet this end never
            st.need = true; st.asked = false; ev.ahead = true; count.ahead++;   // took, and is not going back: asked, and asked again
        }
        return;
    }
    bool take = false;
    if (h.number == want) {
        if (!p.rules.refuse || in.rx_free[s] >= h.length) take = true;
        else { st.need = true; st.asked = false; ev.refused = true; count.refused++; }      // G11
    } else if (nums.ahead(want, h.number)) {
        ev.ahead = true; count.ahead++;
        if (p.rules.resend_ahead) { st.need = true; if (p.rules.ask_every) st.asked = false; }   // G6
    } else {
        ev.behind = true; count.behind++; st.ack_owed = true;
        if (HL_PLANT(p, DUPLICATE_TAKEN) && nums.dist(h.number, want) == 1) take = true;
    }
    if (take) {
        if (h.number == want) { st.last = h.number; st.need = st.asked = false; }
        st.ack_owed = true;
        r.rx_valid = true; r.rx_stream = uint8_t(s); r.rx_length = h.length; r.rx_hops = h.hops; r.rx_dsum = h.dsum;
        ev.delivered = true; count.delivered++;
    }
}

// ---- the timers ----

void LinkEnd::timers(const In&) {
    const bool between = r.tx_state == TxState::IDLE || r.tx_state == TxState::ZEROS;
    // HL: "a timer runs from the end of the last packet sent in that direction"; at its end the status goes again.
    if (!p.rules.status_each && between && !r.timer_dead && ++r.status_timer >= p.status_timer) {
        r.status_timer = 0;
        for (auto& st : r.s) st.due = true;
        if (HL_PLANT(p, TIMER_NOT_RESTARTED)) r.timer_dead = true;
    }
    for (unsigned s = 0; s < STREAMS; s++) {
        StreamRegs& st = r.s[s];
        // G13: the same for each stream, from its own last packet: the other stream's packets do not hold it.
        const bool own = !between && r.tx_stream == s;
        if (p.rules.status_each && !own && !r.timer_dead && ++st.stimer >= p.status_timer) {
            st.stimer = 0; st.due = true;
            if (HL_PLANT(p, TIMER_NOT_RESTARTED)) r.timer_dead = true;
        }
        // G1: a packet not acknowledged for long enough is sent again unasked.
        if (p.resend_timer && running() && kept(s) != 0 && !st.peer_stop) {
            if (++st.rtimer >= p.resend_timer) { count.timer_back++; go_back(s); }
        } else st.rtimer = 0;
    }
}

// ---- the sender ----

void LinkEnd::packet_sent() {
    r.tx_state = TxState::IDLE; tap.end = true;
    r.status_timer = 0; r.s[r.tx_stream].stimer = 0;
    if (r.tx_data) r.s[r.tx_stream].rtimer = 0;
}

void LinkEnd::send(const In& in, Taken& t) {
    if (r.tx_state == TxState::IDLE) choose(in, t);
    switch (r.tx_state) {
    case TxState::IDLE: r.tx_byte = 0; break;
    case TxState::HEADER:
        r.tx_byte = r.tx_hdr[r.tx_count++];
        if (r.tx_count == HEADER_BYTES) { if (r.tx_data) { r.tx_state = TxState::DATA; r.tx_count = 0; } else packet_sent(); }
        break;
    case TxState::DATA:
        r.tx_byte = r.s[r.tx_stream].data[size_t(r.tx_slot) * slot_bytes() + r.tx_count++];
        if (r.tx_count == r.tx_bytes) packet_sent();
        break;
    case TxState::ZEROS:
        r.tx_byte = 0;
        if (++r.tx_count == r.tx_bytes) r.tx_state = TxState::IDLE;
        break;
    }
}

// The sender is free: what it starts now.
void LinkEnd::choose(const In& in, Taken& t) {
    // A recovery or a restart: rounds of zeros and a packet of length 0 for each stream. No data.
    if (!running()) {
        if (r.round == 0) {
            r.round = 1; count.rounds++;
            if (p.zeros && !HL_PLANT(p, NO_ZEROS)) {
                r.tx_state = TxState::ZEROS; r.tx_count = 0; r.tx_bytes = p.zeros; ev.zeros_start = true;
                return;
            }
        }
        const unsigned s = r.round == 1 ? FG : BG;
        r.round = (r.round == 1 && p.rules.lost_each_stream) ? 2 : 0;       // G8
        // An end that answers a restart and has lost its place says "lost" first: the other end may be out of the
        // handshake already, and only its recovery's zeros let this end hear anything again.
        if (r.restart || (r.restart_answer && !r.lost)) {
            start_header(in, s, uint8_t((r.restart ? F_LOST : 0) | (r.restart_answer ? F_RECOVERY : 0)), R_RESTART, false, Kind::RESTART);
        } else {
            const Kind kind = r.lost && r.answer ? Kind::BOTH : r.lost ? Kind::LOST : Kind::RECOVERY;
            start_header(in, s, uint8_t((r.lost ? F_LOST : 0) | (r.answer ? F_RECOVERY : 0)), 0, false, kind);
            if (HL_PLANT(p, RECOVERY_ONCE) && r.round == 0 && !r.lost) { r.answer = false; r.answered = true; mode_changed(false); }
        }
        return;
    }
    bool due[STREAMS], data[STREAMS];
    for (unsigned s = 0; s < STREAMS; s++) {
        const StreamRegs& st = r.s[s];
        due[s] = st.due || st.stop_sent != in.say_stop[s] || (st.need && !st.asked) || (p.rules.ack_now && st.ack_owed);
        const bool room = kept(s) < p.window || HL_PLANT(p, WINDOW_NOT_KEPT);
        data[s] = !st.peer_stop && (st.next != st.tail || (room && in.offer[s]));
    }
    int status = -1, packet = -1;
    if (p.rules.status_each) {             // G13: a status that is due goes before the other stream's data
        if (due[FG] && !data[FG]) status = FG;
        else if (due[BG] && !(data[BG] && !data[FG])) status = BG;
        else if (data[FG]) packet = FG;
        else if (data[BG]) packet = BG;
    } else {                               // HL: "as a packet of length 0 if there is nothing else to send"
        if (data[FG]) packet = FG;
        else if (data[BG]) packet = BG;
        else if (due[FG]) status = FG;
        else if (due[BG]) status = BG;
    }
    if (status >= 0) {
        start_header(in, unsigned(status), 0, 0, false, Kind::STATUS);
        if (HL_PLANT(p, ZERO_TAKES_NUMBER)) { StreamRegs& st = r.s[status]; if (st.next == st.tail) st.next = st.tail = nums.succ(st.tail); }
    } else if (packet >= 0) {
        const unsigned s = unsigned(packet);
        StreamRegs& st = r.s[s];
        const bool again = st.next != st.tail;
        if (!again) {                      // a new packet: into the window, to be kept until it is acknowledged
            if (kept(s) == p.window) { st.head = (st.head + 1) % p.window; st.acked = nums.succ(st.acked); }   // only the planted bug gets here
            const unsigned slot = (st.head + kept(s)) % p.window;
            const Offer& o = *in.offer[s];
            uint8_t* m = &st.meta[size_t(slot) * META];
            m[0] = o.length; m[1] = o.hops; memcpy(m + 2, &o.dsum, 4);
            memcpy(&st.data[size_t(slot) * slot_bytes()], o.data, size_t(o.length) * WORD_BYTES);
            st.tail = (HL_PLANT(p, WRAP_WRONG) && st.tail == nums.last) ? st.tail + 1 : nums.succ(st.tail);
            t.tx[s] = true;
        }
        r.tx_slot = uint8_t((st.head + nums.dist(base(s), st.next)) % p.window);
        if (p.rules.check_kept && p.rules.restart) {       // G21: what was kept is no longer what was checksummed
            const uint8_t* m = &st.meta[size_t(r.tx_slot) * META];
            uint32_t dsum; memcpy(&dsum, m + 2, 4);
            if (m[0] > p.max_words || crc32c(&st.data[size_t(r.tx_slot) * slot_bytes()], size_t(m[0]) * WORD_BYTES) != dsum) {
                count.kept_bad++; declare(); return;
            }
        }
        start_header(in, s, 0, 0, true, again ? Kind::RESENT : Kind::DATA);
        st.next = (HL_PLANT(p, WRAP_WRONG) && st.next == nums.last) ? st.next + 1 : nums.succ(st.next);
    }
}

// A header is written, for the link as it stands now: the acknowledge field, the stop bit and "resend" are this
// cycle's, on a packet sent again too (G12).
void LinkEnd::start_header(const In& in, unsigned s, uint8_t flags, uint8_t reserved, bool data, Kind kind) {
    StreamRegs& st = r.s[s];
    Header h;
    h.stream = uint8_t(s + 1); h.flags = flags; h.reserved = reserved;
    h.number = st.next;
    h.ack = HL_PLANT(p, ACK_ONE_OFF) && st.last ? nums.succ(st.last) : st.last;
    if (in.say_stop[s]) h.flags |= F_STOP;
    if (running() && st.need && !st.asked) { h.flags |= F_RESEND; st.asked = true; }
    if (data) {
        const uint8_t* m = &st.meta[size_t(r.tx_slot) * META];
        h.length = m[0]; h.hops = m[1]; memcpy(&h.dsum, m + 2, 4);
    }
    st.stop_sent = in.say_stop[s]; st.due = false; st.ack_owed = false;
    pack(h, r.tx_hdr);
    r.tx_state = TxState::HEADER; r.tx_count = 0; r.tx_stream = uint8_t(s); r.tx_data = data && h.length > 0;
    r.tx_bytes = WORD_BYTES * h.length;
    tap.start = true; tap.kind = kind; tap.bytes = HEADER_BYTES + r.tx_bytes; memcpy(tap.hdr, r.tx_hdr, HEADER_BYTES);
    count.sent[unsigned(kind)]++;
}

}  // namespace hl
