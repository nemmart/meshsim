// test_ingress_link: Ingress's host side, the black box (v1.37; src/units/host_link.h and ingress_egress.h, THE HOST
// LINK), from ../v1.4_wip/planning/INGRESS-INTERFACE.md ("II") and HOST-LINK.md ("HL"): the link's checks; the rule for
// a service flag; the status bits and the word counts; reading a packet out by the DMA, in pieces; both streams; the
// errors, and that nothing is presented after one; the plain timing.
//
// THE PACKETS are made by the tests' provisional writer (tests/packet_writer.h: its header says why it is provisional,
// and what replaces it). THE BENCH is the master tile's slot driven directly (SlotCore, as test_tracer_slot's reference
// run drives it): Ingress/Egress with the host link as peripheral 1, a load, a store and a consuming load of EVENTS at
// cycle t as the slot's own calls, then the slot's cycle t. The DMA's packets are kept as they leave it.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"
#include "packet_writer.h"
#include "units/host_link.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/regmap.h"

using namespace units;

namespace {

// II's numbers
constexpr uint32_t ST_FOREGROUND_READY = 1u << 3, ST_BACKGROUND_READY = 1u << 4;
constexpr uint32_t EVENT_DMA_DONE = 1u << 1, EVENT_FOREGROUND_SERVICE = 1u << 8, EVENT_BACKGROUND_SERVICE = 1u << 9;
constexpr uint32_t ERR_CRC = 1u << 1, ERR_SEQUENCE = 1u << 2, ERR_FORMAT = 1u << 3;
constexpr unsigned W_FOREGROUND_WORDS = 80, W_BACKGROUND_WORDS = 81;
constexpr uint32_t EVENT_IDLE = 1u << 5 | 1u << 7;            // 17.6: uart_tx and stream_out read 1 while it is idle

// A packet's data: `words` words, word w's low half `tag` << 32 | w, its high half the complement.
std::vector<uint8_t> data_of(uint32_t tag, unsigned words) {
    std::vector<uint8_t> d;
    for (unsigned w = 0; w < words; w++) {
        const uint64_t lo = uint64_t(tag) << 32 | w, hi = ~lo;
        for (uint64_t half : {lo, hi}) for (unsigned b = 0; b < 8; b++) d.push_back(uint8_t(half >> (8 * b)));
    }
    return d;
}
void add(std::vector<uint8_t>& file, unsigned stream, uint32_t number, uint32_t tag, unsigned words) {
    packet_writer::Fields f;
    f.stream = stream; f.number = number; f.length = words;
    const std::vector<uint8_t> d = data_of(tag, words);
    packet_writer::packet(file, f, d.data());
}

struct Landed { int64_t t; uint64_t lo, hi; uint32_t addr; unsigned dst, counter, mode, size; };

struct Bench {
    SlotCore s;
    std::vector<uint8_t> file;
    std::vector<Landed> out;
    int64_t t = 1;
    explicit Bench(std::vector<uint8_t> bytes, int64_t cycles_per_byte = 0) : file(std::move(bytes)) {
        s.tile = lw::CC1;
        PeriphSlotConfig c;
        c.host_link = true;
        s.install(1, PT_INGRESS, c);
        ie().link_cycles_per_byte = cycles_per_byte;
        ie().set_link(file.data(), file.size());
    }
    IngressEgress& ie() { return s.ingress_egress(1); }
    void tick() {
        s.run_cycle(t);
        for (unsigned k = 0; k < s.n_out; k++) {
            const uint64_t h = s.out[k].h;
            out.push_back({t, s.out[k].lo, s.out[k].hi, lw::addr(h), lw::dst(h), lw::counter(h), lw::mode(h), lw::size(h)});
        }
        s.end_cycle(t);
        t++;
    }
    void run(int64_t cycles) { for (int64_t k = 0; k < cycles; k++) tick(); }
    uint32_t load(unsigned w) { uint32_t v = 0; s.peek(t, 1, w, v); return v; }
    uint32_t events() { uint32_t v = 0; s.read(t, 1, P_EVENTS, v); return v; }     // a consuming load
    void mask(uint32_t m) { s.write(t, 1, P_EVENTS_MASK, m); tick(); }
    // A transfer of n words of stream `queue` to `addr` at tile `dst`, counted on `counter`: to its dma_done.
    void dma(unsigned queue, unsigned n, unsigned dst, uint32_t addr, unsigned counter) {
        s.write(t, 1, P_DMA_HDR, lw::SIZE_128 | counter << HDR_COUNTER_LO | lw::M_WRITE << HDR_MODE_LO | dst << HDR_DST_LO | n << DMA_HDR_COUNT_LO | queue << DMA_HDR_QUEUE_LO);
        tick();
        s.write(t, 1, P_DMA_DEST_ADDR, addr);
        tick();
        s.write(t, 1, P_DMA_START, 1);
        tick();
        for (int k = 0; k < 200 && (load(P_STATUS) & 2); k++) tick();   // STATUS.dma_busy falls at the transfer's end
    }
    // The words the DMA sent since `from`, checked against a packet's data from word `first`: true if they are those.
    bool sent(size_t from, uint32_t tag, unsigned first, unsigned n, uint32_t addr, unsigned dst, unsigned counter) const {
        if (out.size() != from + n) return false;
        for (unsigned k = 0; k < n; k++) {
            const Landed& l = out[from + k];
            const uint64_t lo = uint64_t(tag) << 32 | (first + k);
            if (l.lo != lo || l.hi != ~lo || l.addr != addr + 16 * k || l.dst != dst || l.counter != counter || l.mode != lw::M_WRITE || l.size != lw::SIZE_128) return false;
        }
        return true;
    }
};

}  // namespace

// ================================================================================================ the link's checks
TEST(link_crc32c) {
    using hostlink::crc32c;
    CHECK_EQ(crc32c(reinterpret_cast<const uint8_t*>("123456789"), 9), 0xE3069283u);
    CHECK_EQ(crc32c(nullptr, 0), 0u);                      // over no data
    uint8_t b[32];
    std::memset(b, 0, 32);    CHECK_EQ(crc32c(b, 32), 0x8A9136AAu);     // RFC 3720, B.4: 32 bytes of zeros,
    std::memset(b, 0xFF, 32); CHECK_EQ(crc32c(b, 32), 0x62A8AB43u);     // of ones,
    for (int i = 0; i < 32; i++) b[i] = uint8_t(i);
    CHECK_EQ(crc32c(b, 32), 0x46DD794Eu);                                // 0 to 31,
    for (int i = 0; i < 32; i++) b[i] = uint8_t(31 - i);
    CHECK_EQ(crc32c(b, 32), 0x113FDB5Cu);                                // and 31 to 0
    CHECK_EQ(packet_writer::crc32c(b, 32), 0x113FDB5Cu);                 // the writer's, table-driven, agrees
}

// A header word's fields where HL puts them, and the two checks over the bytes HL names.
TEST(link_header_fields) {
    packet_writer::Fields f;
    f.stream = 2; f.flags = 0x9; f.length = 3; f.number = 0xABCDE; f.acknowledge = 0x12345;
    const std::vector<uint8_t> d = data_of(7, 3);
    std::vector<uint8_t> file;
    packet_writer::packet(file, f, d.data());
    CHECK_EQ(file.size(), size_t(16 + 48));
    CHECK_EQ(file[0], 0x92u);                               // [3:0] the stream, [7:4] the flags
    CHECK_EQ(file[1], 3u);                                  // [15:8] the length
    CHECK_EQ(uint32_t(file[2]) | uint32_t(file[3]) << 8 | uint32_t(file[4] & 15) << 16, 0xABCDEu);   // [35:16]
    CHECK_EQ(uint32_t(file[4] >> 4) | uint32_t(file[5]) << 4 | uint32_t(file[6]) << 12, 0x12345u);   // [55:36]
    CHECK_EQ(file[7], 0u);                                  // [63:56] reserved
    auto u32 = [&](size_t at) { return uint32_t(file[at]) | uint32_t(file[at + 1]) << 8 | uint32_t(file[at + 2]) << 16 | uint32_t(file[at + 3]) << 24; };
    CHECK_EQ(u32(8), hostlink::crc32c(file.data() + 16, 48));            // [95:64] over the data
    CHECK_EQ(u32(12), hostlink::crc32c(file.data(), 12));                // [127:96] over bits [95:0]
    hostlink::Reader r;
    r.open(file.data(), file.size());
    r.expect[1] = 0xABCDE;
    hostlink::Packet p;
    hostlink::Check check;
    size_t taken;
    CHECK(r.next(p, check, taken) == hostlink::Reader::Next::PACKET);
    CHECK(check == hostlink::Check::OK);
    CHECK_EQ(taken, size_t(64));
    CHECK_EQ(p.stream, 2u); CHECK_EQ(p.flags, 9u); CHECK_EQ(p.length, 3u); CHECK_EQ(p.number, 0xABCDEu); CHECK_EQ(p.acknowledge, 0x12345u);
    CHECK_EQ(p.lo[2], uint64_t(7) << 32 | 2);
    CHECK_EQ(p.hi[2], ~(uint64_t(7) << 32 | 2));
    CHECK(r.next(p, check, taken) == hostlink::Reader::Next::END);
}

// ================================================================================================ the interface
TEST(ingress_link_reset_values) {
    Bench b({});
    b.run(3);
    CHECK_EQ(b.load(P_PARAMS), 1u);                         // still type 1
    CHECK_EQ(b.load(P_STATUS), 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    uint32_t v;
    CHECK(b.s.peek(b.t, 1, P_DMA_HDR, v));                  // the DMA's words are there
    CHECK(b.s.peek(b.t, 1, W_BACKGROUND_WORDS, v));
    CHECK(!b.s.peek(b.t, 1, 82, v));                        // the rest of block 5 is unmapped
    // its mask holds ten events
    b.s.write(b.t, 1, P_EVENTS_MASK, 0xFFFFFFFF); b.tick();
    CHECK_EQ(b.load(P_EVENTS_MASK), 0x3FFu);
}

// Without the host link the unit is v1.36's: no DMA, eight events, words 80 and 81 unmapped.
TEST(ingress_without_the_host_link_is_v136s) {
    SlotCore s;
    s.tile = lw::CC1;
    s.install(1, PT_INGRESS);
    uint32_t v;
    CHECK(!s.peek(1, 1, P_DMA_HDR, v));
    CHECK(!s.peek(1, 1, W_FOREGROUND_WORDS, v));
    CHECK(!s.peek(1, 1, W_BACKGROUND_WORDS, v));
    CHECK_EQ(s.ingress_egress(1).out_queues(), 0);
    s.write(1, 1, P_EVENTS_MASK, 0xFFFFFFFF); s.run_cycle(1); s.end_cycle(1);
    s.peek(2, 1, P_EVENTS_MASK, v);
    CHECK_EQ(v, 0xFFu);
}

// II, "The rule for a service flag", steps 1 to 5, on the background stream; and "Reading words out": a packet read in
// pieces, one word and then the rest.
TEST(ingress_link_service_flag_rule) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 5);
    add(file, 2, 2, 0xB, 32);
    Bench b(file);
    b.run(2);
    // 1, 2: the packet at the front: its words, its ready bit, its flag
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 5u);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(b.s.live_events(b.t), 0u);                     // masked: it wakes nothing
    b.mask(EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    CHECK_EQ(b.s.live_events(b.t), 1u);                     // the tile's EVENTS.peripheral_1
    // 3: the flag is cleared by the load of EVENTS
    CHECK_EQ(b.events(), EVENT_IDLE | EVENT_BACKGROUND_SERVICE);
    b.tick();
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);        // the packet still waits
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 5u);
    // 4: one word out, to the master's own memory
    b.dma(1, 1, 64, 0x00000100, 3);
    CHECK(b.sent(0, 0xA, 0, 1, 0x100, 64, 3));
    CHECK_EQ(b.events() & EVENT_DMA_DONE, EVENT_DMA_DONE);
    b.run(50);
    // 5: no further flag, and the count holds, until every word is out
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 5u);
    b.dma(1, 3, 64, 0x05100000, 1);                         // three more, to the Updater's segment queue
    CHECK(b.sent(1, 0xA, 1, 3, 0x05100000, 64, 1));
    b.events();
    b.run(50);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 5u);
    b.dma(1, 1, 66, 0x03000040, 0);                         // the last, to another tile, uncounted
    CHECK(b.sent(4, 0xA, 4, 1, 0x03000040, 66, 0));
    // then the next packet is at the front, and asks
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 32u);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(b.events() & (EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE), EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.dma(1, 32, 64, 0x05100000, 1);                        // in one transfer
    CHECK(b.sent(5, 0xB, 0, 32, 0x05100000, 64, 1));
    b.events();
    b.run(5);
    CHECK_EQ(b.load(P_STATUS), 0u);                         // nothing waits
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// The two streams are independent: a packet waiting on one does not hold the other.
TEST(ingress_link_two_streams) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xB1, 4);
    add(file, 1, 1, 0xF1, 2);
    add(file, 2, 2, 0xB2, 3);
    add(file, 1, 2, 0xF2, 1);
    Bench b(file);
    b.run(2);
    CHECK_EQ(b.load(P_STATUS), ST_FOREGROUND_READY | ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 2u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 4u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.events(); b.tick();
    // the foreground read out, both its packets, with the background's first still waiting
    b.dma(0, 2, 64, 0x200, 2);
    CHECK(b.sent(0, 0xF1, 0, 2, 0x200, 64, 2));
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 1u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 4u);
    CHECK_EQ(b.events() & (EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE), EVENT_FOREGROUND_SERVICE);
    b.dma(0, 1, 64, 0x300, 2);
    CHECK(b.sent(2, 0xF2, 0, 1, 0x300, 64, 2));
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    b.events();
    b.dma(1, 4, 64, 0x400, 2);
    CHECK(b.sent(3, 0xB1, 0, 4, 0x400, 64, 2));
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 3u);
    CHECK_EQ(b.events() & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// A stream holds four packets; the file's next waits for room.
TEST(ingress_link_stream_holds_four) {
    std::vector<uint8_t> file;
    for (uint32_t k = 1; k <= 7; k++) add(file, 2, k, k, 2);
    Bench b(file);
    b.run(10);
    CHECK_EQ(b.ie().link_packets, 4u);
    b.dma(1, 2, 64, 0x100, 1);                              // the first out: room for the fifth
    b.run(2);
    CHECK_EQ(b.ie().link_packets, 5u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 2u);
    for (uint32_t k = 2; k <= 7; k++) { const size_t from = b.out.size(); b.dma(1, 2, 64, 0x100, 1); CHECK(b.sent(from, k, 0, 2, 0x100, 64, 1)); }
    b.run(2);
    CHECK_EQ(b.ie().link_packets, 7u);
    CHECK_EQ(b.load(P_STATUS), 0u);
}

// A packet of length 0 is checked and asks for no service. It takes no packet number: its field holds the number the
// stream's next packet with data will carry (owner, 2026-11-16).
TEST(ingress_link_length_0) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 1);
    add(file, 2, 2, 0, 0);                                  // no data: the next with data is 2
    add(file, 2, 2, 0, 0);                                  // and again
    add(file, 2, 2, 0xB, 1);
    add(file, 1, 1, 0, 0);                                  // the foreground's, with nothing sent yet: 1
    Bench b(file);
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.ie().link_packets, 5u);
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);        // none of the empty ones waits
    b.dma(1, 1, 64, 0x100, 1);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1u);
    const size_t from = b.out.size();
    b.dma(1, 1, 64, 0x100, 1);
    CHECK(b.sent(from, 0xB, 0, 1, 0x100, 64, 1));
    // an empty packet whose number is not the next data packet's
    std::vector<uint8_t> bad;
    add(bad, 2, 1, 0xA, 1);
    add(bad, 2, 3, 0, 0);
    Bench c(bad);
    c.run(3);
    CHECK_EQ(c.load(P_ERRORS), ERR_SEQUENCE);
}

// Idle zeros between packets, in counts that are not multiples of 16, and before the first.
TEST(ingress_link_idle_zeros) {
    std::vector<uint8_t> file(5, 0);
    add(file, 2, 1, 0xA, 2);
    file.insert(file.end(), 21, 0);
    add(file, 2, 2, 0xB, 1);
    file.insert(file.end(), 16, 0);
    add(file, 2, 3, 0xC, 1);
    file.insert(file.end(), 1024 + 3, 0);                   // and at the end: the link idle
    Bench b(file);
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.ie().link_packets, 3u);
    CHECK(!b.ie().link_cut);
    for (uint32_t k = 0; k < 3; k++) { const size_t from = b.out.size(); b.dma(1, k == 0 ? 2 : 1, 64, 0x100, 1); CHECK(b.sent(from, 0xA + k, 0, k == 0 ? 2 : 1, 0x100, 64, 1)); }
}

// The packet number: 1 first, and after 0xFFFFE, 1 again.
TEST(ingress_link_number_wraps) {
    std::vector<uint8_t> file;
    add(file, 2, 0xFFFFD, 1, 1);
    add(file, 2, 0xFFFFE, 2, 1);
    add(file, 2, 1, 3, 1);
    add(file, 2, 2, 4, 1);
    Bench b(file);
    b.ie().link.expect[1] = 0xFFFFD;                        // as if 0xFFFFC packets had come
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.ie().link_packets, 4u);
    std::vector<uint8_t> bad;                               // 0xFFFFF is no packet number
    add(bad, 2, 0xFFFFE, 1, 1);
    add(bad, 2, 0xFFFFF, 2, 1);
    Bench c(bad);
    c.ie().link.expect[1] = 0xFFFFE;
    c.run(3);
    CHECK_EQ(c.load(P_ERRORS), ERR_SEQUENCE);
    std::vector<uint8_t> zero;                              // nor is 0, and the first is 1
    add(zero, 2, 0, 1, 1);
    Bench d(zero);
    d.run(3);
    CHECK_EQ(d.load(P_ERRORS), ERR_SEQUENCE);
}

// ================================================================================================ the errors
// One thing wrong in the second of three packets: its error bit; the first packet is presented and can be read out;
// nothing is presented after it, however long the wait; the bit is sticky and raises the tile's ERRORS.peripheral_1.
namespace {
enum Damage { FIRST_BYTE, FIRST_BYTE_ZERO, HEADER_CRC, DATA_CHECKSUM, DATA_BYTE, LAST_DATA_BIT, NUMBER_FIELD, LENGTH_FIELD, NUMBER_AHEAD, NUMBER_REPEATED,
              STREAM_0, STREAM_3, LENGTH_33, RESERVED_BIT };
void damaged(Damage what, uint32_t want) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 4);
    const size_t second = file.size();
    packet_writer::Fields f;
    f.stream = 2; f.number = 2; f.length = 6;
    std::vector<uint8_t> d = data_of(0xB, 6);
    switch (what) {                                         // a field wrong, with both checks right for it
        case NUMBER_AHEAD: f.number = 3; break;
        case NUMBER_REPEATED: f.number = 1; break;
        case STREAM_0: f.stream = 0; f.flags = 1; break;    // (with a flag: a first byte of 0 would be idle)
        case STREAM_3: f.stream = 3; break;
        case LENGTH_33: f.length = 33; d = data_of(0xB, 33); break;
        case RESERVED_BIT: f.reserved = 0x10; break;
        default: break;
    }
    packet_writer::packet(file, f, d.data());
    switch (what) {                                         // a bit of the packet changed on the link
        case FIRST_BYTE: file[second] ^= 0x01; break;
        case FIRST_BYTE_ZERO: file[second] = 0; break;      // read as idle: the link has lost its place
        case HEADER_CRC: file[second + 13] ^= 0x40; break;
        case DATA_CHECKSUM: file[second + 9] ^= 0x02; break;
        case DATA_BYTE: file[second + 16 + 37] ^= 0x80; break;
        case LAST_DATA_BIT: file[second + 16 + 95] ^= 0x80; break;
        case NUMBER_FIELD: file[second + 2] ^= 0x04; break;
        case LENGTH_FIELD: file[second + 1] ^= 0x01; break;
        default: break;
    }
    add(file, 2, 3, 0xC, 2);
    Bench b(file);
    b.mask(EVENT_BACKGROUND_SERVICE);
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), want);
    CHECK_EQ(b.s.live_errors(b.t), 1u);                     // the tile's ERRORS.peripheral_1: the NMI
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 4u);               // the first packet is there, and asked
    CHECK_EQ(b.events() & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    b.dma(1, 4, 64, 0x100, 1);
    CHECK(b.sent(0, 0xA, 0, 4, 0x100, 64, 1));
    b.run(2000);
    CHECK_EQ(b.load(P_STATUS), 0u);                         // and nothing after it
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & (EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE), 0u);
    CHECK_EQ(b.ie().link_packets, 1u);
    CHECK_EQ(b.load(P_ERRORS), want);
}
}  // namespace

TEST(ingress_link_crc_error_the_first_byte) { damaged(FIRST_BYTE, ERR_CRC); }
TEST(ingress_link_crc_error_the_first_byte_zeroed) { damaged(FIRST_BYTE_ZERO, ERR_CRC); }
TEST(ingress_link_crc_error_the_header_crc) { damaged(HEADER_CRC, ERR_CRC); }
TEST(ingress_link_crc_error_the_data_checksum) { damaged(DATA_CHECKSUM, ERR_CRC); }
TEST(ingress_link_crc_error_a_data_byte) { damaged(DATA_BYTE, ERR_CRC); }
TEST(ingress_link_crc_error_the_last_data_bit) { damaged(LAST_DATA_BIT, ERR_CRC); }
TEST(ingress_link_crc_error_a_number_bit_on_the_link) { damaged(NUMBER_FIELD, ERR_CRC); }
TEST(ingress_link_crc_error_a_length_bit_on_the_link) { damaged(LENGTH_FIELD, ERR_CRC); }
TEST(ingress_link_sequence_error_a_number_skipped) { damaged(NUMBER_AHEAD, ERR_SEQUENCE); }
TEST(ingress_link_sequence_error_a_number_repeated) { damaged(NUMBER_REPEATED, ERR_SEQUENCE); }
TEST(ingress_link_format_error_stream_0) { damaged(STREAM_0, ERR_FORMAT); }
TEST(ingress_link_format_error_stream_3) { damaged(STREAM_3, ERR_FORMAT); }
TEST(ingress_link_format_error_length_33) { damaged(LENGTH_33, ERR_FORMAT); }
TEST(ingress_link_format_error_a_reserved_bit) { damaged(RESERVED_BIT, ERR_FORMAT); }

// The flags and the acknowledge field need the way back: they are not looked at.
TEST(ingress_link_flags_and_acknowledge_are_not_looked_at) {
    std::vector<uint8_t> file;
    packet_writer::Fields f;
    f.stream = 2; f.number = 1; f.length = 1; f.flags = 0xF; f.acknowledge = 0xFFFFF;
    const std::vector<uint8_t> d = data_of(9, 1);
    packet_writer::packet(file, f, d.data());
    Bench b(file);
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1u);
}

// A file that ends inside a packet: the packet never arrives; no error.
TEST(ingress_link_file_cut) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    add(file, 2, 2, 0xB, 8);
    for (size_t cut : {size_t(48 + 7), size_t(48 + 16), size_t(48 + 16 + 100)}) {
        Bench b(std::vector<uint8_t>(file.begin(), file.begin() + long(cut)));
        b.run(5);
        CHECK_EQ(b.load(P_ERRORS), 0u);
        CHECK_EQ(b.ie().link_packets, 1u);
        CHECK(b.ie().link_cut);
    }
}

// The reset empties the streams, clears the errors and rewinds the file.
TEST(ingress_link_reset) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    add(file, 2, 5, 0xB, 2);                                // out of order
    Bench b(file);
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), ERR_SEQUENCE);
    b.s.reset(b.t);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.load(P_STATUS), 0u);
    b.run(3);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 2u);               // the file again, from its start
    CHECK_EQ(b.load(P_ERRORS), ERR_SEQUENCE);
}

DEATH_TEST(ingress_link_a_transfer_past_the_packet_is_ub, "a DMA transfer of 3 words from the background stream, whose packet has 2 left") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    Bench b(file);
    b.run(3);
    b.dma(1, 3, 64, 0x100, 1);
}
DEATH_TEST(ingress_link_a_transfer_with_no_packet_is_ub, "from the foreground stream, whose packet has 0 left") {
    Bench b({});
    b.run(3);
    b.dma(0, 1, 64, 0x100, 1);
}

// ================================================================================================ the timing
// A byte a cycle from cycle 1: a packet whose last byte arrives in cycle c is seen by the loads of c + 1; an error at
// the cycle its packet's last byte would have arrived; a packet that waited for room starts after the wait.
TEST(ingress_link_a_byte_a_cycle) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);                               // 528 bytes: cycles 1 to 528
    file.insert(file.end(), 10, 0);                         // idle: 529 to 538
    add(file, 2, 2, 0xB, 1);                                // 32 bytes: 539 to 570
    for (uint32_t k = 3; k <= 4; k++) add(file, 2, k, k, 1);   // 571 to 602, to 634: the stream holds four
    add(file, 2, 5, 5, 2);                                  // waits for room
    add(file, 2, 7, 7, 1);                                  // out of order
    Bench b(file, 1);
    while (b.t <= 528) { CHECK_EQ(b.load(P_STATUS), 0u); CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE); b.tick(); }
    CHECK_EQ(b.t, int64_t(529));
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 32u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_BACKGROUND_SERVICE);
    while (b.t <= 570) { CHECK_EQ(b.ie().link_packets, 1u); b.tick(); }
    CHECK_EQ(b.ie().link_packets, 2u);
    while (b.t <= 602) { CHECK_EQ(b.ie().link_packets, 2u); b.tick(); }
    while (b.t <= 634) { CHECK_EQ(b.ie().link_packets, 3u); b.tick(); }
    CHECK_EQ(b.ie().link_packets, 4u);
    b.run(1000);
    CHECK_EQ(b.ie().link_packets, 4u);                      // the fifth waits: four are at the stream
    CHECK_EQ(b.load(P_ERRORS), 0u);
    // the first read out in one transfer: DMA_START at cycle p, a word a cycle from p + 1; the last is taken in p + 32
    const int64_t p = b.t + 2;
    b.dma(1, 32, 64, 0x100, 1);
    CHECK(b.sent(0, 0xA, 0, 32, 0x100, 64, 1));
    CHECK_EQ(b.out[0].t, p + 2);                            // (taken in p + 1, leaving the DMA the cycle after)
    const int64_t room = b.out[31].t - 1;                   // the cycle its last word was taken in: room at its end
    CHECK_EQ(room, p + 32);
    while (b.t <= room + 48) { CHECK_EQ(b.ie().link_packets, 4u); b.tick(); }   // 48 bytes, from the cycle after
    CHECK_EQ(b.ie().link_packets, 5u);
    while (b.t <= room + 48 + 32) { CHECK_EQ(b.load(P_ERRORS), 0u); b.tick(); }
    CHECK_EQ(b.load(P_ERRORS), ERR_SEQUENCE);               // at the cycle the bad packet's last byte arrived
}

// As fast as taken (0 cycles a byte): a packet is there as soon as there is room.
TEST(ingress_link_as_fast_as_taken) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);
    Bench b(file, 0);
    b.tick();
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 32u);
}

// A whole frame file, packetized, read out packet by packet: the frames come back byte for byte.
TEST(ingress_link_a_frame_file) {
    std::vector<uint8_t> frames, packets;
    FILE* f = std::fopen("tests/updater/kin-lru.frames", "rb");
    CHECK(f != nullptr);
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) frames.insert(frames.end(), buf, buf + n);
    std::fclose(f);
    std::string err;
    CHECK(packet_writer::packetize(frames, packets, err));
    Bench b(packets);
    b.mask(EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    std::vector<uint8_t> back;
    size_t taken = 0;
    for (int guard = 0; guard < 100000; guard++) {
        b.tick();
        const uint32_t words = b.load(W_BACKGROUND_WORDS);
        if (words == 0) break;
        CHECK_EQ(b.events() & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
        b.dma(1, words, 64, 0x100, 1);
        CHECK_EQ(b.out.size(), taken + words);
        for (; taken < b.out.size(); taken++) for (uint64_t half : {b.out[taken].lo, b.out[taken].hi}) for (unsigned k = 0; k < 8; k++) back.push_back(uint8_t(half >> (8 * k)));
    }
    CHECK(back == frames);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}
