// link_end.cpp: see link_end.h. HL is HOST-LINK.md as it stands on 2026-11-17.
#include "link_end.h"

#include <algorithm>
#include <cstring>

namespace hl {

void LinkEnd::power_on() {
    r = Regs();
    r.rx_buf.assign(slot_bytes(), 0);
    r.meta.assign(size_t(p.window) * META, 0);
    r.data.assign(size_t(p.window) * slot_bytes(), 0);
    r.restart = p.rules.restart;           // an end that starts says so, and waits for the answer
    tap = Tap(); ev = Events();
}

uint32_t LinkEnd::restart_sum() const {
    uint32_t reg = CRC_START;
    for (unsigned i = 0; i < p.restart_words * WORD_BYTES; i++) reg = crc_step(reg, restart_byte(i));
    return crc_result(reg);
}

bool LinkEnd::again_has(unsigned stream) const {
    for (uint32_t n = r.next; n != r.tail; n = nums.succ(n)) if (slot_stream(slot_of(n)) == stream) return true;
    return false;
}

void LinkEnd::canon(Writer& a) const {
    auto some = [&](const uint8_t* b, size_t n) { a.out.insert(a.out.end(), b, b + n); };
    a(r.rx_state); a(r.zero_run);
    if (r.rx_state == RxState::HEADER) { a(r.rx_count); some(r.rx_hdr, r.rx_count); }
    if (r.rx_state == RxState::DATA) { a(r.rx_count); some(r.rx_hdr, HEADER_BYTES); some(r.rx_buf.data(), r.rx_count); }
    if (r.rx_state == RxState::SKIP) { a(r.rx_count); a(r.rx_crc); some(r.rx_hdr, HEADER_BYTES); }
    a(r.last); a(r.need); a(r.asked); a(r.wrong); a(r.ack_owed); a(r.stop_sent);
    if (r.asked) a(r.ask_timer);
    a(r.tx_state);
    if (r.tx_state == TxState::HEADER) {
        a(r.tx_count); some(r.tx_hdr + r.tx_count, HEADER_BYTES - r.tx_count); a(r.tx_data); a(r.tx_pattern);
        if (r.tx_data && !r.tx_pattern) { a(r.tx_bytes); some(&r.data[size_t(r.tx_slot) * slot_bytes()], r.tx_bytes); }
    }
    if (r.tx_state == TxState::DATA) {
        const uint32_t left = r.tx_bytes - r.tx_count; a(left); a(r.tx_pattern);
        if (!r.tx_pattern) some(&r.data[size_t(r.tx_slot) * slot_bytes() + r.tx_count], left);
    }
    if (r.tx_state == TxState::ZEROS) { const uint32_t left = r.tx_bytes - r.tx_count; a(left); }
    a(r.acked); a(r.next); a(r.tail); a(r.peer_stop); a(r.prep_key); a(r.prep_left);
    for (uint32_t i = 0; i < kept() && i < p.window; i++) {
        const size_t slot = (r.head + i) % p.window;
        some(&r.meta[slot * META], HL_PLANT(p, CHECKSUM_KEPT) ? META : 3);
        some(&r.data[slot * slot_bytes()], size_t(r.meta[slot * META + 1]) * WORD_BYTES);
    }
    a(r.lost); a(r.answer); a(r.restart); a(r.restart_answer); a(r.error); a(r.timer_dead); a(r.answered);
    if (!running()) a(r.round);
    a(r.due); a(r.status_timer);
}

Taken LinkEnd::step(const In& in) {
    Taken t;
    tap = Tap(); ev = Events();
    if (in.power_on) { power_on(); return t; }
    r.rx_valid = false;
    receive(in);
    timers(in);
    send(in, t);
    count.most_kept = std::max(count.most_kept, kept());
    return t;
}

// ---- the receiver ----

void LinkEnd::receive(const In& in) {
    const uint8_t b = in.rx_byte;
    r.zero_run = b ? 0 : std::min(r.zero_run + 1, p.flush_zeros);
    switch (r.rx_state) {
    case RxState::FLUSH:                   // out of step until zeros that no packet can hold have gone by
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
    case RxState::SKIP:                    // the data of a packet with a field that cannot be: checked, and not kept
        r.rx_crc = crc_step(r.rx_crc, b);
        if (++r.rx_count == r.rx_bytes) { r.rx_state = RxState::HUNT; bad_fields(unpack(r.rx_hdr), crc_result(r.rx_crc) == unpack(r.rx_hdr).dsum); }
        break;
    }
}

// HL, "A packet made wrong": one flag. The packet expected has now failed a field test with both checks passing
// (or come with a length that cannot be). The first time the flag is set; if it is set already, this is the second
// time running: the error, and the link starts again. Returns true if it did.
bool LinkEnd::made_wrong() {
    if (!p.rules.wrong_twice) return false;
    if (r.wrong && p.rules.restart) { count.made_wrong++; ev.wrong_twice = true; declare(); return true; }
    r.wrong = true; count.wrong_once++;
    return false;
}

// The header's last byte has arrived. HL's order: the header CRC, then the fields.
void LinkEnd::header_end(const In& in) {
    if (crc_result(r.rx_crc) != CRC_GOOD_HEADER) { lose_place(); return; }
    const Header h = unpack(r.rx_hdr);
    const bool heeds = !r.lost && !r.restart && !r.restart_answer;
    if (h.length > p.max_words) {          // how far to pass over is not known: a lost place, and counted like a wrong packet
        ev.rx_bad_fields = true; count.rx_bad_fields++;
        if (heeds && h.number == expect() && made_wrong()) {             // the second time: the link starts again, and the
            r.rx_state = p.rules.flush ? RxState::FLUSH : RxState::HUNT;  // receiver waits for the zeros of its rounds
            return;
        }
        lose_place();
        return;
    }
    const bool is_restart = (h.flags & F_RESTART) != 0;
    const bool cannot_be = (h.stream != 1 && h.stream != 2) || h.reserved != 0 || (is_restart && !p.rules.restart) ||
                           (in.rx_forwards && h.hops == 15 && h.length > 0 && !is_restart);
    r.rx_bytes = WORD_BYTES * h.length; r.rx_count = 0; r.rx_crc = CRC_START;   // the unit set back: all ones, not zero
    if (h.length == 0) {
        r.rx_state = RxState::HUNT;        // of no data the checksum is 0, and it is checked
        if (cannot_be) bad_fields(h, h.dsum == 0); else packet_end(in, h.dsum == 0);
    } else {
        r.rx_state = cannot_be ? RxState::SKIP : RxState::DATA;
    }
}

// A header CRC failed: the packet's length is unknown, and this end has lost its place (HL, "When a packet is bad").
void LinkEnd::lose_place() {
    ev.rx_bad_header = true; count.rx_bad_header++;
    r.rx_state = p.rules.flush ? RxState::FLUSH : RxState::HUNT;
    const bool was_running = running();
    if (!r.lost) { r.lost = true; ev.mode_change = true; }
    if (!p.rules.lost_answers) r.answer = false;           // without that rule, a lost side only says "lost"
    mode_changed(was_running);
}

// The link's mode may have changed: a recovery starts with its zeros; back in step, a status goes out at once, so
// that the other side sees an ordinary acknowledgement (HL, "Recovery").
void LinkEnd::mode_changed(bool was_running) {
    if (was_running && !running()) r.round = 0;
    if (!was_running && running()) {
        r.round = 0; r.due = true;
        if (r.tx_state == TxState::ZEROS) r.tx_state = TxState::IDLE;     // zeros cut short are idle
    }
}

// A good header with a field that cannot be, its data now passed over. With the data's check failing it is a
// damaged packet like any other. With both checks passing it is HL's "packet made wrong": asked for again, and
// counted if it carried the number expected.
void LinkEnd::bad_fields(const Header& h, bool data_ok) {
    ev.rx_bad_fields = true; count.rx_bad_fields++;
    if (r.lost || r.restart || r.restart_answer) return;
    if (!data_ok) { thrown_away(h.number, h.length > 0); return; }
    if (h.number == expect()) {
        if (made_wrong()) return;
        r.need = true; r.asked = false;                    // "the first time, it is asked for again, as any bad packet is"
    } else thrown_away(h.number, true);
}

// A packet with a credible header was thrown away: a resend is owed, if what was lost can be sent again.
void LinkEnd::thrown_away(uint32_t number, bool has_data) {
    if (HL_PLANT(p, WRONG_FLAG_CLEARED)) r.wrong = false;
    if (r.lost || !running() || !has_data) return;
    if (number == expect()) { r.need = true; r.asked = false; }
    else if (nums.ahead(expect(), number)) { r.need = true; if (r.ask_timer >= p.reask) r.asked = false; }
}

// A packet's last byte has arrived, its header good and its fields possible. HL's order: the data checksum, the number.
void LinkEnd::packet_end(const In& in, bool data_ok) {
    const Header h = unpack(r.rx_hdr);
    if (h.flags & F_RESTART) {             // acted on only if the header, the data checksum and the pattern all agree
        bool whole = data_ok && h.length == p.restart_words;
        for (unsigned i = 0; whole && i < r.rx_bytes; i++) whole = r.rx_buf[i] == restart_byte(i);
        if (!whole && !HL_PLANT(p, RESTART_ANY_DATA)) {    // "a damaged packet", like any other
            ev.rx_bad_data = true; count.rx_bad_data++;
            thrown_away(h.number, h.length > 0);
            return;
        }
        ev.rx_good = true; count.rx_good++;
        good_packet(in, h);
        return;
    }
    if (!data_ok) {
        ev.rx_bad_data = true; count.rx_bad_data++;
        if (HL_PLANT(p, ACK_FROM_BAD_DATA)) take_ack(h.ack);
        thrown_away(h.number, h.length > 0);
        return;
    }
    ev.rx_good = true; count.rx_good++;
    good_packet(in, h);
}

// The sender goes back to the oldest packet not acknowledged.
void LinkEnd::go_back() {
    if (r.next != base()) { ev.went_back = true; count.went_back++; }
    r.next = base();
}

// An acknowledge field from a good packet: what it covers is released. Returns 0 if it was taken (or was the same);
// 1 if it is behind the last one taken; 2 if it claims more than was ever sent. Neither of those releases anything.
int LinkEnd::take_ack(uint32_t ack) {
    if (ack == r.acked) return 0;
    const uint32_t have = kept();
    const uint32_t k = (ack == 0 || ack > nums.last) ? nums.last : nums.dist_ack(r.acked, ack);
    if (k > have) {
        if (k <= nums.last / 2) { count.ack_beyond++; return 2; }
        count.ack_behind++; return 1;
    }
    const uint32_t sent_past = nums.dist(base(), r.next);
    r.head = (r.head + k) % p.window; r.acked = ack;
    if (sent_past < k) r.next = base();                    // the next to send was among those released
    return 0;
}

// The link starts again under this end: its numbers, what it kept, and its mode, as at power-on. The packet in
// hand is finished; the receiver keeps its place in the bytes.
void LinkEnd::start_again(bool report) {
    r.last = 0; r.need = r.asked = r.wrong = r.ack_owed = false; r.ask_timer = 0;
    r.acked = 0; r.next = r.tail = 1; r.head = 0; r.prep_key = 0; r.prep_left = 0;
    for (unsigned s = 0; s < STREAMS; s++) { r.peer_stop[s] = false; r.stop_sent[s] = false; }
    r.lost = r.answer = false; r.round = 0; r.status_timer = 0; r.due = false;
    if (report) r.error = true;
    ev.restarted = true; count.restarts++;
}

// This end has found that the link cannot go on as it is: it starts again, says so (the error), and asks the other
// end to do the same.
void LinkEnd::declare() {
    const bool was_running = running();
    start_again(true);
    r.restart = true; ev.mode_change = true;
    mode_changed(was_running);
}

void LinkEnd::good_packet(const In& in, const Header& h) {
    const bool was_running = running(), was_lost = r.lost;
    const bool f_lost = h.flags & F_LOST, f_rec = h.flags & F_RECOVERY;

    // HL, "Starting a link again": with the restart flag, "lost" reads "I have started again" and "recovery" reads
    // "so have I". The packet takes no number and its data goes to no queue.
    if (h.flags & F_RESTART) {
        const bool in_handshake = r.restart || r.restart_answer;        // nothing of this life has gone by yet
        if (r.lost) { r.lost = false; ev.mode_change = true; }            // its place is found
        if (f_rec && r.restart) { r.restart = false; ev.mode_change = true; }
        if (f_lost) {
            if (!in_handshake) start_again(true);                         // what this end kept is gone, and it says so
            if (!r.restart_answer) { r.restart_answer = true; ev.mode_change = true; }
        } else if (r.restart_answer) { r.restart_answer = false; ev.mode_change = true; }
        mode_changed(was_running);
        return;
    }
    if (r.restart) return;                                 // from the other end's life before: nothing in it counts
    if (r.restart_answer) { r.restart_answer = false; ev.mode_change = true; }   // an ordinary packet: the other end is up

    // The link's mode.
    if (!f_lost) r.answered = false;
    const bool hears_lost = f_lost && (!was_lost || p.rules.lost_answers) && !r.answered;
    if (f_rec && r.lost) { r.lost = false; ev.mode_change = true; }
    if (hears_lost) { if (!r.answer) { r.answer = true; ev.mode_change = true; } }
    else if (!f_lost && r.answer) { r.answer = false; ev.mode_change = true; }
    mode_changed(was_running);

    // The acknowledge field, the stop bits, and a going back.
    if (!was_lost || p.rules.lost_uses_ack || f_rec || hears_lost) {
        const int ack = take_ack(h.ack);
        if (ack == 1 && HL_PLANT(p, ERROR_ON_RELEASE)) { count.not_kept++; ev.not_kept = true; declare(); return; }
        for (unsigned s = 0; s < STREAMS; s++) {
            ev.stop_taken[s] = h.stop[s] ? 1 : 0;
            r.peer_stop[s] = h.stop[s] && !HL_PLANT(p, STOP_IGNORED);
        }
        if (hears_lost || (h.flags & F_RESEND)) {
            // HL, "a false acknowledgement": the other end asks for the packet after its acknowledge field, and that
            // field is behind what this end has let go. It cannot send it: the error, and the link starts again.
            if (ack == 1 && p.rules.ask_not_kept && p.rules.restart) { count.not_kept++; ev.not_kept = true; declare(); return; }
            go_back();
        }
    }

    // The number, which means something only now.
    if (r.lost) return;                                    // out of step, nothing is taken
    const uint32_t want = expect();
    if (h.length == 0) {                                   // the number the sender puts on the wire next
        if (h.number == want) r.need = r.asked = false;
        else if (p.rules.zero_number && nums.ahead(want, h.number)) {
            ev.ahead = true; count.ahead++;
            r.need = true; if (r.ask_timer >= p.reask) r.asked = false;
        }
        return;
    }
    const unsigned s = h.stream - 1u;
    bool take = false;
    if (h.number == want) {
        if (!p.rules.refuse || in.rx_free[s] >= h.length) take = true;
        else { r.need = true; r.asked = false; ev.refused = true; count.refused++; }
    } else if (nums.ahead(want, h.number)) {
        ev.ahead = true; count.ahead++;
        if (p.rules.resend_ahead) { r.need = true; if (p.rules.reask_any && r.ask_timer >= p.reask) r.asked = false; }
        if (HL_PLANT(p, WRONG_FLAG_CLEARED)) r.wrong = false;
    } else {
        ev.behind = true; count.behind++; r.ack_owed = true;
        if (HL_PLANT(p, DUPLICATE_TAKEN) && nums.dist(h.number, want) == 1) take = true;
    }
    if (take) {
        if (h.number == want) { r.last = h.number; r.need = r.asked = r.wrong = false; }
        r.ack_owed = true;
        r.rx_valid = true; r.rx_stream = uint8_t(s); r.rx_length = h.length; r.rx_hops = h.hops;
        ev.delivered = true; count.delivered++;
    }
}

// ---- the timers ----

void LinkEnd::timers(const In&) {
    const bool between = r.tx_state == TxState::IDLE || r.tx_state == TxState::ZEROS;
    // HL: "a timer runs from the end of the last packet sent in that direction"; at its end the status goes again.
    if (between && !r.timer_dead && ++r.status_timer >= p.status_timer) {
        r.status_timer = 0; r.due = true;
        if (HL_PLANT(p, TIMER_NOT_RESTARTED)) r.timer_dead = true;
    }
    if (r.asked && r.ask_timer < p.reask) r.ask_timer++;
}

// ---- the sender ----

void LinkEnd::packet_sent() { r.tx_state = TxState::IDLE; tap.end = true; r.status_timer = 0; }

// The packet with data that goes next, if any: one to be sent again before a new one; of the new ones the
// foreground's before the background's. A stream that is stopped has no packets sent (HL).
LinkEnd::Pick LinkEnd::pick(const In& in) const {
    Pick c;
    if (!running()) return c;
    if (r.next != r.tail) {
        const unsigned slot = slot_of(r.next), s = slot_stream(slot);
        if (r.peer_stop[s] && p.rules.stop_holds_again) return c;       // and everything behind it waits with it
        c.kind = 1; c.stream = s; c.length = r.meta[size_t(slot) * META + 1]; c.key = 1ull << 63 | r.next;
        return c;
    }
    if (kept() >= p.window && !HL_PLANT(p, WINDOW_NOT_KEPT)) return c;
    for (unsigned s = 0; s < STREAMS; s++) {
        if (r.peer_stop[s] || !in.offer[s]) continue;
        c.kind = 2; c.stream = s; c.length = in.offer[s]->length; c.key = uint64_t(s + 1) << 32 | in.offer[s]->id;
        return c;
    }
    return c;
}

void LinkEnd::send(const In& in, Taken& t) {
    // The sending unit passes over the data of the packet that goes next, to have its checksum for the header: as
    // long as the data, at p.pass bytes a cycle. It runs while the packet before is going out.
    const Pick c = pick(in);
    if (c.kind == 0) { r.prep_key = 0; r.prep_left = 0; }
    else if (c.key != r.prep_key) { r.prep_key = c.key; r.prep_left = p.pass ? (WORD_BYTES * c.length + p.pass - 1) / p.pass : 0; }
    else if (r.prep_left) r.prep_left--;
    if (r.tx_state == TxState::IDLE) choose(in, t, c);
    switch (r.tx_state) {
    case TxState::IDLE: r.tx_byte = 0; break;
    case TxState::HEADER:
        r.tx_byte = r.tx_hdr[r.tx_count++];
        if (r.tx_count == HEADER_BYTES) { if (r.tx_data) { r.tx_state = TxState::DATA; r.tx_count = 0; } else packet_sent(); }
        break;
    case TxState::DATA:
        r.tx_byte = r.tx_pattern ? restart_byte(r.tx_count) : r.data[size_t(r.tx_slot) * slot_bytes() + r.tx_count];
        if (++r.tx_count == r.tx_bytes) packet_sent();
        break;
    case TxState::ZEROS:
        r.tx_byte = 0;
        if (++r.tx_count == r.tx_bytes) r.tx_state = TxState::IDLE;
        break;
    }
}

// The sender is free: what it starts now.
void LinkEnd::choose(const In& in, Taken& t, const Pick& c) {
    // A recovery or a restart: rounds of zeros and one packet. No data of the streams'.
    if (!running()) {
        if (r.round == 0) {
            r.round = 1; count.rounds++;
            if (p.zeros && !HL_PLANT(p, NO_ZEROS)) {
                r.tx_state = TxState::ZEROS; r.tx_count = 0; r.tx_bytes = p.zeros; ev.zeros_start = true;
                return;
            }
        }
        r.round = 0;
        // An end that answers a restart and has lost its place says "lost" first: the other end may be out of the
        // handshake already, and only its recovery's zeros let this end hear anything again.
        if (r.restart || (r.restart_answer && !r.lost)) {
            start_header(in, FG, uint8_t(F_RESTART | (r.restart ? F_LOST : 0) | (r.restart_answer ? F_RECOVERY : 0)),
                         uint8_t(p.restart_words), 0, restart_sum(), Kind::RESTART);
        } else {
            const Kind kind = r.lost && r.answer ? Kind::BOTH : r.lost ? Kind::LOST : Kind::RECOVERY;
            start_header(in, FG, uint8_t((r.lost ? F_LOST : 0) | (r.answer ? F_RECOVERY : 0)), 0, 0, 0, kind);
            if (HL_PLANT(p, RECOVERY_ONCE) && !r.lost) { r.answer = false; r.answered = true; mode_changed(false); }
        }
        return;
    }
    // In step. A packet with data, if one is ready; else an empty one if the status is due: on a change of a stop
    // bit, a resend owed, an acknowledgement owed, or the timer.
    if (c.kind && r.prep_left == 0) {
        const bool again = c.kind == 1;
        if (!again) {                      // a new packet: into the window, to be kept until it is acknowledged
            if (kept() == p.window) { r.head = (r.head + 1) % p.window; r.acked = nums.succ(r.acked); }   // only the planted bug gets here
            const unsigned slot = (r.head + kept()) % p.window;
            const Offer& o = *in.offer[c.stream];
            uint8_t* m = &r.meta[size_t(slot) * META];
            m[0] = uint8_t(c.stream); m[1] = o.length; m[2] = o.hops;
            memcpy(&r.data[size_t(slot) * slot_bytes()], o.data, size_t(o.length) * WORD_BYTES);
            const uint32_t first = crc32c(o.data, size_t(o.length) * WORD_BYTES);
            memcpy(m + 3, &first, 4);
            r.tail = (HL_PLANT(p, WRAP_WRONG) && r.tail == nums.last) ? r.tail + 1 : nums.succ(r.tail);
            t.tx[c.stream] = true;
        }
        r.tx_slot = uint8_t(slot_of(r.next));
        const uint8_t* m = &r.meta[size_t(r.tx_slot) * META];
        // Both checks are made afresh each time a packet is sent, from the data as it is held (HL).
        uint32_t dsum = crc32c(&r.data[size_t(r.tx_slot) * slot_bytes()], size_t(m[1]) * WORD_BYTES);
        if (HL_PLANT(p, CHECKSUM_KEPT) && again) memcpy(&dsum, m + 3, 4);
        start_header(in, m[0], 0, m[1], m[2], dsum, again ? Kind::RESENT : Kind::DATA);
        r.next = (HL_PLANT(p, WRAP_WRONG) && r.next == nums.last) ? r.next + 1 : nums.succ(r.next);
        r.prep_key = 0;
        return;
    }
    if (c.kind) count.pass_wait++;
    bool due = r.due || (r.ack_owed && p.rules.ack_now) || (r.need && !r.asked);
    for (unsigned s = 0; s < STREAMS; s++) due = due || r.stop_sent[s] != in.say_stop[s];
    if (!due) return;
    start_header(in, FG, 0, 0, 0, 0, Kind::STATUS);
    if (HL_PLANT(p, ZERO_TAKES_NUMBER) && r.next == r.tail) r.next = r.tail = nums.succ(r.tail);
}

// A header is written, for the link as it stands now: the acknowledge field, the stop bits and "resend" are this
// cycle's, on a packet sent again too.
void LinkEnd::start_header(const In& in, unsigned stream, uint8_t flags, uint8_t length, uint8_t hops, uint32_t dsum, Kind kind) {
    Header h;
    h.stream = uint8_t(stream + 1); h.flags = flags; h.length = length; h.hops = hops; h.dsum = dsum;
    h.number = kind == Kind::RESTART ? 1 : r.next;         // of a packet with data: its own; of an empty one: the next
    h.ack = HL_PLANT(p, ACK_ONE_OFF) && r.last ? nums.succ(r.last) : r.last;
    for (unsigned s = 0; s < STREAMS; s++) { h.stop[s] = in.say_stop[s]; r.stop_sent[s] = h.stop[s]; }
    if (running() && r.need && !r.asked) { h.flags |= F_RESEND; r.asked = true; r.ask_timer = 0; }
    r.ack_owed = false; r.due = false;
    pack(h, r.tx_hdr);
    if (HL_PLANT(p, NOT_SEALED_AGAIN) && length && hops > 0 && kind != Kind::RESTART) {   // the CRC of the header before hops was counted
        Header was = h; was.hops = uint8_t(hops - 1);
        uint8_t b[HEADER_BYTES]; pack(was, b); memcpy(r.tx_hdr + 12, b + 12, 4);
    }
    r.tx_state = TxState::HEADER; r.tx_count = 0; r.tx_data = length > 0; r.tx_pattern = kind == Kind::RESTART;
    r.tx_bytes = WORD_BYTES * length;
    tap.start = true; tap.kind = kind; tap.bytes = HEADER_BYTES + r.tx_bytes; memcpy(tap.hdr, r.tx_hdr, HEADER_BYTES);
    count.sent[unsigned(kind)]++;
    (kind == Kind::DATA || kind == Kind::RESENT ? count.bytes_data : kind == Kind::STATUS ? count.bytes_status : count.bytes_round) += tap.bytes;
}

}  // namespace hl
