// linkhw.cpp: the link end and the chain node as hardware: every register of the model with the bits it needs at the
// real sizes, every memory with its bytes, and the sums. It prints the tables of
// planning/sessions/WORKER8/HARDWARE.md, which holds them word for word (make check compares).
//
// The list of registers is checked against the model: it must have one row for every field LinkEnd::io() writes,
// so a register added to the model and not to this list fails here.
#include <cstdio>
#include <cstring>
#include <vector>

#include "node.h"

using namespace hl;

namespace {

unsigned bits(unsigned most) { unsigned b = 0; while (most >> b) b++; return b; }   // to hold 0 to most

// A number as the documents write it: 16,384. Each call has its own buffer, so several fit in one printf.
const char* num(unsigned v) {
    static char buf[16][16];
    static unsigned at = 0;
    char* b = buf[at++ % 16];
    if (v < 1000) snprintf(b, 16, "%u", v);
    else if (v < 1000000) snprintf(b, 16, "%u,%03u", v / 1000, v % 1000);
    else snprintf(b, 16, "%u,%03u,%03u", v / 1000000, v / 1000 % 1000, v % 1000);
    return b;
}

struct Row {
    const char* name;      // the model's name (link_end.h, Regs), or what it is
    unsigned bits;         // flip-flops
    unsigned bytes;        // memory
    bool model;            // a field of the model's Regs
    const char* what;
};

struct CountFields {
    unsigned n = 0;
    template <class T> void operator()(T&) { n++; }
    void bytes(std::vector<uint8_t>&, size_t) { n++; }
};

void table(const char* title, const std::vector<Row>& rows, unsigned& ff, unsigned& mem) {
    printf("\n**%s**\n\n| register | flip-flops | memory, bytes | what it holds |\n|---|---|---|---|\n", title);
    unsigned f = 0, m = 0;
    for (const Row& r : rows) {
        printf("| %s%s%s | %s | %s | %s |\n", r.model ? "`" : "", r.name, r.model ? "`" : "", r.bits ? num(r.bits) : "", r.bytes ? num(r.bytes) : "", r.what);
        f += r.bits; m += r.bytes;
    }
    printf("| **in all** | **%s** | **%s** | |\n", num(f), num(m));
    ff += f; mem += m;
}

// Two ends back to back under traffic and noise: does every register stay within the bits the tables give it, and
// are the two the tables drop (rx_bytes, tx_bytes) what the tables say they are? Returns the cycles run, 0 if not.
struct Widths { unsigned rx_count, zero_run, ask_timer, tx_count, slot, prep_left, status_timer, length, number; };

uint64_t rng_state = 0x243F6A8885A308D3ull;
uint64_t rnd() { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return rng_state; }

bool within(const LinkEnd& le, const Widths& w, const char** why) {
    const auto& r = le.r;
    auto fits = [](uint64_t v, unsigned b) { return v < (1ull << b); };
    if (!fits(r.rx_count, w.rx_count)) { *why = "rx_count"; return false; }
    if (!fits(r.zero_run, w.zero_run)) { *why = "zero_run"; return false; }
    if (!fits(r.ask_timer, w.ask_timer)) { *why = "ask_timer"; return false; }
    if (!fits(r.tx_count, w.tx_count)) { *why = "tx_count"; return false; }
    if (!fits(r.head, w.slot) || !fits(r.tx_slot, w.slot)) { *why = "head or tx_slot"; return false; }
    if (!fits(r.prep_left, w.prep_left)) { *why = "prep_left"; return false; }
    if (!fits(r.status_timer, w.status_timer)) { *why = "status_timer"; return false; }
    if (!fits(r.rx_length, w.length) || r.rx_stream > 1 || r.rx_hops > 15) { *why = "the packet handed over"; return false; }
    if (!fits(r.last, w.number) || !fits(r.acked, w.number) || !fits(r.next, w.number) || !fits(r.tail, w.number)) { *why = "a packet number"; return false; }
    if ((r.rx_state == RxState::DATA || r.rx_state == RxState::SKIP) && r.rx_bytes != WORD_BYTES * unpack(r.rx_hdr).length) { *why = "rx_bytes"; return false; }
    if (r.tx_state == TxState::DATA && r.tx_bytes != WORD_BYTES * unpack(r.tx_hdr).length) { *why = "tx_bytes, data"; return false; }
    if (r.tx_state == TxState::ZEROS && r.tx_bytes != le.p.zeros) { *why = "tx_bytes, zeros"; return false; }
    return true;
}

uint64_t run_widths(const Params& p, const Widths& w, uint64_t cycles, uint64_t* packets, uint64_t* rounds) {
    LinkEnd e[2] = {LinkEnd(p), LinkEnd(p)};
    uint8_t wire[2] = {}, data[2][2][MAX_WORDS * WORD_BYTES] = {};
    Offer offer[2][2];
    uint32_t made[2][2] = {};
    bool have[2][2] = {}, stop[2][2] = {};
    uint64_t burst[2] = {};
    for (uint64_t t = 0; t < cycles; t++) {
        uint8_t out[2];
        for (unsigned i = 0; i < 2; i++) {
            In in;
            in.rx_byte = wire[1 - i];
            for (unsigned s = 0; s < STREAMS; s++) {
                if (!have[i][s] && rnd() % 200 == 0) {
                    have[i][s] = true;
                    offer[i][s].length = uint8_t(1 + rnd() % p.max_words); offer[i][s].hops = uint8_t(rnd() % 15); offer[i][s].id = ++made[i][s];
                    for (unsigned b = 0; b < unsigned(offer[i][s].length) * WORD_BYTES; b++) data[i][s][b] = uint8_t(rnd());
                    offer[i][s].data = data[i][s];
                }
                if (rnd() % 5000 == 0) stop[i][s] = !stop[i][s];
                in.say_stop[s] = stop[i][s]; in.rx_free[s] = rnd() % 50 ? 1024 : 8;
                in.offer[s] = have[i][s] ? &offer[i][s] : nullptr;
            }
            const Taken tk = e[i].step(in);
            for (unsigned s = 0; s < STREAMS; s++) if (tk.tx[s]) have[i][s] = false;
            out[i] = e[i].r.tx_byte;
            const char* why = "";
            if (!within(e[i], w, &why)) { printf("linkhw: at cycle %llu, %s is not what the tables say\n", (unsigned long long)t, why); return 0; }
        }
        for (unsigned i = 0; i < 2; i++) {                      // the noise: a flipped byte now and then, and bursts of zeros
            if (!burst[i] && rnd() % 40000 == 0) burst[i] = 1 + rnd() % 3000;
            wire[i] = burst[i] ? (burst[i]--, uint8_t(0)) : rnd() % 20000 == 0 ? uint8_t(out[i] ^ (1 + rnd() % 255)) : out[i];
        }
    }
    *packets = e[0].count.delivered + e[1].count.delivered;
    *rounds = e[0].count.rounds + e[1].count.rounds;
    return cycles;
}

}  // namespace

int main() {
    const Params p;
    const unsigned packet = p.max_words * WORD_BYTES;                       // a packet's data at most, in bytes
    const unsigned nb = p.number_bits;
    const unsigned slot_meta = 1 + bits(p.max_words) + 4;                   // a kept packet's stream, length and hops

    const std::vector<Row> rx = {
        {"rx_state", 3, 0, true, "HUNT, HEADER, DATA, SKIP or FLUSH"},
        {"rx_count", bits(packet), 0, true, "the bytes so far of the header (to 16) or of the data (to 512)"},
        {"rx_bytes", 0, 0, true, "† no register: the data's length in bytes is 16 × the length field held in `rx_hdr`"},
        {"rx_crc", 32, 0, true, "**the slow unit's register**"},
        {"rx_mask", 32, 0, true, "**the mask register** for the data arriving"},
        {"zero_run", bits(p.flush_zeros), 0, true, "the zero bytes in a row on the wire, stopping at 528"},
        {"rx_hdr", 96, 0, true, "† the header's bytes 0 to 11, until the packet is judged. Bytes 12 to 15 need no place: they go into the unit and into `rx_mask` as they arrive"},
        {"rx_valid", 1, 0, true, "a packet is being handed to the owner"},
        {"rx_stream", 1, 0, true, "of the packet handed over: its stream"},
        {"rx_length", bits(p.max_words), 0, true, "its length in words, 1 to 32"},
        {"rx_hops", 4, 0, true, "its hops"},
        {"rx_buf", 0, packet, true, "**the packet buffer:** the data of the one packet being checked"},
        {"last", nb, 0, true, "the last packet number taken in order, 0 for none yet: the acknowledge field this end sends"},
        {"need", 1, 0, true, "a resend is owed: a packet was thrown away, and the one expected has not come"},
        {"asked", 1, 0, true, "\"resend\" has gone out for it"},
        {"ask_timer", bits(p.reask), 0, true, "cycles since it went, stopping at 2,048: asked again only then"},
        {"wrong", 1, 0, true, "the packet expected has failed a field test once (\"A packet made wrong\")"},
        {"ack_owed", 1, 0, true, "a packet was taken since the last header sent"},
        {"stop_sent", 2, 0, true, "the stop bits in the last header sent, to see a change"},
    };
    const std::vector<Row> tx = {
        {"tx_state", 2, 0, true, "IDLE, HEADER, DATA or ZEROS"},
        {"tx_count", bits(p.zeros), 0, true, "the bytes sent of the header (to 16), the data (to 512) or the zeros (to 1,024)"},
        {"tx_bytes", 0, 0, true, "† no register: 16 × the length in `tx_hdr`, or 1,024 for the zeros"},
        {"tx_hdr", 128, 0, true, "the header going out. It is latched when it starts, since the fields behind it can change while it goes"},
        {"tx_slot", bits(p.window - 1), 0, true, "which kept packet is going out"},
        {"tx_data", 1, 0, true, "the packet going out has data"},
        {"tx_pattern", 1, 0, true, "and the data is the restart packet's, from its table"},
        {"tx_mask", 32, 0, true, "**the mask register** for the data going out"},
        {"tx_byte", 8, 0, true, "the wire"},
        {"acked", nb, 0, true, "the last acknowledge field taken: the oldest packet kept is the one after it"},
        {"next", nb, 0, true, "the number that goes on the wire next: behind `tail` while packets are being sent again"},
        {"tail", nb, 0, true, "the number the next new packet takes"},
        {"head", bits(p.window - 1), 0, true, "the slot of the oldest packet kept"},
        {"peer_stop", 2, 0, true, "the other end's stop bits, from the last good header"},
        {"meta", p.window * slot_meta, 0, true, "† for each of the 8 kept packets: its stream (1), length (6) and hops (4)"},
        {"data", 0, p.window * packet, true, "**the kept packets:** 8 slots of 32 words"},
        {"prep_key", 5, 0, true, "† which packet the fast unit has passed over: none, one to send again or a new one (2); the slot, or the stream and whether it is forwarded or the ASIC's own (3)"},
        {"prep_left", bits((packet + p.pass - 1) / p.pass), 0, true, "the cycles of that pass still to go, 32 at most"},
    };
    const std::vector<Row> mode = {
        {"lost", 1, 0, true, "this end has lost its place in the bytes"},
        {"answer", 1, 0, true, "the other end has said \"lost\": this end answers with recovery rounds"},
        {"restart", 1, 0, true, "this end has started again and waits for the answer"},
        {"restart_answer", 1, 0, true, "the other end has started again: this end answers"},
        {"error", 1, 0, true, "**the error output.** Sticky: the link started again under this end"},
        {"round", 1, 0, true, "in a round: the zeros come next, or the packet"},
        {"due", 1, 0, true, "a status is due: the timer ran out, or the link came back into step"},
        {"status_timer", bits(p.status_timer - 1), 0, true, "cycles since the end of the last packet sent, to 512"},
        {"timer_dead", 0, 0, true, "none: a planted bug's"},
        {"answered", 0, 0, true, "none: a planted bug's"},
    };
    const std::vector<Row> more = {
        {"the fast unit's register", 32, 0, false, "† **the fast unit:** the CRC so far of the pass, then the data checksum for the header"},
        {"the handing-over counter", bits(p.max_words), 0, false, "† which word of the packet buffer is going to the queue and the forwarding buffer"},
        {"the pattern flag", 1, 0, false, "† a restart packet's data has matched the table so far"},
    };

    CountFields cf;
    LinkEnd le(p);
    le.io(cf);
    unsigned model_rows = 0;
    for (const auto* t : {&rx, &tx, &mode, &more}) for (const Row& r : *t) model_rows += r.model;
    if (cf.n != model_rows) { printf("linkhw: the model has %u registers and this list has %u\n", cf.n, model_rows); return 1; }

    const Widths w = {bits(packet), bits(p.flush_zeros), bits(p.reask), bits(p.zeros), bits(p.window - 1), bits((packet + p.pass - 1) / p.pass),
                      bits(p.status_timer - 1), bits(p.max_words), nb};
    uint64_t packets = 0, rounds = 0;
    const uint64_t ran = run_widths(p, w, 3000000, &packets, &rounds);
    if (!ran) return 1;

    unsigned end_ff = 0, end_mem = 0, more_ff = 0, more_mem = 0;
    table("A link end, the receiver", rx, end_ff, end_mem);
    table("A link end, the sender", tx, end_ff, end_mem);
    table("A link end, the link's mode and timer", mode, end_ff, end_mem);
    table("A link end, what hardware needs and the model does not have", more, more_ff, more_mem);
    printf("\n**Checked against the model:** two ends back to back for %s cycles, with noise: %s packets delivered, %s recovery and restart rounds. "
           "No register held more than its flip-flops here allow, and `rx_bytes` and `tx_bytes` were always what the tables say they are.\n",
           num(unsigned(ran)), num(unsigned(packets)), num(unsigned(rounds)));
    printf("\n**One link end:** %s flip-flops for the model's %u registers and %s more, %s in all; %s bytes of memory.\n",
           num(end_ff), cf.n, num(more_ff), num(end_ff + more_ff), num(end_mem + more_mem));

    // The node: the forwarding buffers. A word FIFO of whole packets.
    const ChainNode node(p, true, 1024);
    const unsigned fw = node.fwd_words, fwd_bytes = fw * WORD_BYTES;
    const unsigned mark_bits = 1 + 4, marks = (fw * mark_bits + 7) / 8;      // a word's "last of its packet", and its hops
    const unsigned fifo_ff = 3 * bits(fw - 1) + bits(fw);
    const std::vector<Row> fwd = {
        {"its words", 0, fwd_bytes, false, "1,024 words: whole packets, first in first out"},
        {"its marks", 0, marks, false, "† 5 bits beside each word: the last word of its packet (1), and the packet's hops (4)"},
        {"read pointer", bits(fw - 1), 0, false, "† the next word to offer the sender"},
        {"write pointer", bits(fw - 1), 0, false, "† where the word being handed over goes"},
        {"whole-packet pointer", bits(fw - 1), 0, false, "† the write pointer after the last whole packet: only whole packets are offered"},
        {"words held", bits(fw), 0, false, "for \"half full\" (the stop bit) and for the room a packet needs"},
    };
    unsigned f_ff = 0, f_mem = 0;
    table("A forwarding buffer (a node has four: each stream, each direction)", fwd, f_ff, f_mem);
    if (f_ff != fifo_ff) return 1;

    const unsigned queue = 1024 * WORD_BYTES;
    const unsigned node_ff = 2 * (end_ff + more_ff) + 4 * f_ff;
    const unsigned node_mem = 2 * (end_mem + more_mem) + 4 * f_mem;
    printf("\n**The budget for one ASIC in the middle of a chain**\n\n| | how many | flip-flops | memory, bytes |\n|---|---|---|---|\n");
    printf("| a link end's registers | 2 | %s | |\n", num(2 * (end_ff + more_ff)));
    printf("| a forwarding buffer's pointers and count | 4 | %s | |\n", num(4 * f_ff));
    printf("| the packet buffer of a receiver | 2 | | %s |\n", num(2 * packet));
    printf("| the kept packets of a sender | 2 | | %s |\n", num(2 * p.window * packet));
    printf("| a forwarding buffer's words | 4 | | %s |\n", num(4 * fwd_bytes));
    printf("| a forwarding buffer's marks | 4 | | %s |\n", num(4 * marks));
    printf("| **the link, in all** | | **%s** | **%s** |\n", num(node_ff), num(node_mem));
    printf("| the chip's own queues behind it (Ingress), 16 KB a stream | 2 | | %s |\n", num(2 * queue));
    printf("| **with those** | | | **%s** |\n", num(node_mem + 2 * queue));
    if (node_mem != 2 * packet + 2 * p.window * packet + 4 * fwd_bytes + 4 * marks) return 1;
    auto share = [&](unsigned bytes) { return (bytes * 1000u / node_mem + 5) / 10; };
    printf("\n**Where the link's memory goes:** the four forwarding buffers' words %u%%, their marks %u%%, the packets kept for sending again %u%%, the two packet buffers %u%%.\n",
           share(4 * fwd_bytes), share(4 * marks), share(2 * p.window * packet), share(2 * packet));

    // What could be smaller.
    const unsigned rule_words = 2 * p.window * p.max_words;                 // twice the window of the end that feeds it
    const unsigned small_marks = (rule_words * mark_bits + 7) / 8;
    printf("\n**What could be smaller**\n\n| | bytes saved | breaks a rule? |\n|---|---|---|\n");
    printf("| the four forwarding buffers at %u words, twice the window that feeds them, not 1,024 | %s | no, but it is exactly the limit, as 8 KB is for a queue |\n", rule_words,
           num(4 * (fwd_bytes - rule_words * WORD_BYTES) + 4 * (marks - small_marks)));
    printf("| no forwarding buffer for the background towards the host | %s (%s if the buffers are %u words) | it needs a new one: a background packet with data from below is a packet made wrong |\n",
           num(fwd_bytes + marks), num(rule_words * WORD_BYTES + small_marks), rule_words);
    printf("| the packets a sender keeps left in the forwarding buffer until acknowledged, not copied to slots (the link away from the host) | %s | no, but it is not built and not searched |\n",
           num(p.window * packet));
    printf("| a packet checked in place at the end of its queue, not in a buffer of its own | %s | no, but it is not built and not searched |\n", num(2 * packet));
    printf("| the chip's own queues at 8 KB | %s | no, but it is exactly the limit: the owner chose 16 KB for that reason |\n", num(2 * queue - 2 * rule_words * WORD_BYTES));
    return 0;
}
