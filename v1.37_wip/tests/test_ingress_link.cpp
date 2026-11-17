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
#include "ingress_bench.h"
#include "packet_writer.h"
#include "units/host_link.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/regmap.h"

using namespace units;
using namespace ingress_bench;

namespace {

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

// HL's own figures, as main has them (8385dacd): its table's values, its example packet byte for byte, its cross-check.
TEST(link_documents_example) {
    using hostlink::crc32c;
    uint8_t zeros[16] = {};
    CHECK_EQ(crc32c(zeros, 16), 0x42709AEAu);              // "of 16 zero bytes"
    // "stream 2, no flags, length 1, number 1, acknowledge 0, the data one zero word"
    static const uint8_t example[16] = {0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0xA0, 0x8C, 0x06, 0xE1};
    CHECK_EQ(crc32c(example, 12), 0xE1068CA0u);            // the header CRC, over bytes 0 to 11
    CHECK_EQ(crc32c(example, 16), 0x48674BC7u);            // "the CRC-32C of all 16 header bytes of a good packet"
    std::vector<uint8_t> bytes(example, example + 16);
    bytes.insert(bytes.end(), 16, 0);
    hostlink::Reader reader;
    reader.open(bytes.data(), bytes.size());
    hostlink::Packet p;
    hostlink::Check check;
    size_t taken = 0;
    CHECK(reader.next(p, check, taken) == hostlink::Reader::Next::PACKET);
    CHECK(check == hostlink::Check::OK);
    CHECK_EQ(p.stream, 2u); CHECK_EQ(p.length, 1u); CHECK_EQ(p.number, 1u); CHECK_EQ(p.acknowledge, 0u); CHECK_EQ(p.flags, 0u);
    CHECK_EQ(taken, size_t(32));
    CHECK(reader.next(p, check, taken) == hostlink::Reader::Next::END);
    std::vector<uint8_t> written;                          // and the tests' writer makes the same 32 bytes
    packet_writer::Fields f;
    f.length = 1; f.number = 1;
    packet_writer::packet(written, f, zeros);
    CHECK(written == bytes);
    // the same packet after one hop, and with number 0x2ABCD, acknowledge 0x15555 and hops 3 (owner, 2026-11-17):
    // the writer's bytes, the reader's fields, and the black box takes them
    static const uint8_t hop1[16] = {0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0x9E, 0xBC, 0x0A, 0x21};
    static const uint8_t hop3[16] = {0x02, 0x01, 0xCD, 0xAB, 0x56, 0x55, 0x35, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0xC9, 0xC4, 0x23, 0x30};
    const struct { const uint8_t* h; uint32_t number, acknowledge; unsigned hops; } more[] = {{hop1, 1, 0, 1}, {hop3, 0x2ABCD, 0x15555, 3}};
    for (const auto& m : more) {
        std::vector<uint8_t> w;
        packet_writer::Fields g;
        g.length = 1; g.number = m.number; g.acknowledge = m.acknowledge; g.hops = m.hops;
        packet_writer::packet(w, g, zeros);
        CHECK(w.size() == 32 && std::equal(m.h, m.h + 16, w.begin()));
        CHECK_EQ(crc32c(m.h, 16), 0x48674BC7u);
        hostlink::Reader rd;
        rd.open(w.data(), w.size());
        rd.expect[1] = m.number;
        CHECK(rd.next(p, check, taken) == hostlink::Reader::Next::PACKET);
        CHECK(check == hostlink::Check::OK);
        CHECK_EQ(p.number, m.number); CHECK_EQ(p.acknowledge, m.acknowledge); CHECK_EQ(p.hops, m.hops);
        Bench b(w);
        b.ie().link.expect[1] = m.number;
        b.run(3);
        CHECK_EQ(b.load(P_ERRORS), 0u);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1u);
    }
}

// A header word's fields where HL puts them, and the two checks over the bytes HL names.
TEST(link_header_fields) {
    packet_writer::Fields f;
    f.stream = 2; f.flags = 0x9; f.length = 3; f.number = 0x2BCDE; f.acknowledge = 0x3A5A5; f.hops = 0xC;
    const std::vector<uint8_t> d = data_of(7, 3);
    std::vector<uint8_t> file;
    packet_writer::packet(file, f, d.data());
    CHECK_EQ(file.size(), size_t(16 + 48));
    CHECK_EQ(file[0], 0x92u);                               // [3:0] the stream, [7:4] the flags
    CHECK_EQ(file[1], 3u);                                  // [15:8] the length
    CHECK_EQ(uint32_t(file[2]) | uint32_t(file[3]) << 8 | uint32_t(file[4] & 3) << 16, 0x2BCDEu);          // [33:16]
    CHECK_EQ(uint32_t(file[4] >> 2) | uint32_t(file[5]) << 6 | uint32_t(file[6] & 15) << 14, 0x3A5A5u);   // [51:34]
    CHECK_EQ(uint32_t(file[6] >> 4), 0xCu);                                                             // [55:52] hops
    CHECK_EQ(file[7], 0u);                                  // [63:56] reserved
    auto u32 = [&](size_t at) { return uint32_t(file[at]) | uint32_t(file[at + 1]) << 8 | uint32_t(file[at + 2]) << 16 | uint32_t(file[at + 3]) << 24; };
    CHECK_EQ(u32(8), hostlink::crc32c(file.data() + 16, 48));            // [95:64] over the data
    CHECK_EQ(u32(12), hostlink::crc32c(file.data(), 12));                // [127:96] over bits [95:0]
    hostlink::Reader r;
    r.open(file.data(), file.size());
    r.expect[1] = 0x2BCDE;
    hostlink::Packet p;
    hostlink::Check check;
    size_t taken;
    CHECK(r.next(p, check, taken) == hostlink::Reader::Next::PACKET);
    CHECK(check == hostlink::Check::OK);
    CHECK_EQ(taken, size_t(64));
    CHECK_EQ(p.stream, 2u); CHECK_EQ(p.flags, 9u); CHECK_EQ(p.length, 3u); CHECK_EQ(p.number, 0x2BCDEu); CHECK_EQ(p.acknowledge, 0x3A5A5u);
    CHECK_EQ(p.hops, 0xCu);
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
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 0u);              // 0 at the reset
    uint32_t v;
    CHECK(b.s.peek(b.t, 1, P_DMA_HDR, v));                  // the DMA's words are there
    CHECK(b.s.peek(b.t, 1, W_BACKGROUND_WORDS, v));
    CHECK(!b.s.peek(b.t, 1, 83, v));                        // 83 is unmapped (it was a threshold), and the rest of block 5
    CHECK(!b.s.peek(b.t, 1, 84, v));
    CHECK(b.s.write(b.t, 1, W_DMA_FORWARD_COUNT, 0));       // R/W
    CHECK(!b.s.write(b.t, 1, W_BACKGROUND_WORDS, 0));       // R
    // its mask holds ten events
    b.s.write(b.t, 1, P_EVENTS_MASK, 0xFFFFFFFF); b.tick();
    CHECK_EQ(b.load(P_EVENTS_MASK), 0x3FFu);
}

// Without the host link the unit is v1.36's: no DMA, eight events, words 80 to 82 unmapped.
TEST(ingress_without_the_host_link_is_v136s) {
    SlotCore s;
    s.tile = lw::CC1;
    s.install(1, PT_INGRESS);
    uint32_t v;
    CHECK(!s.peek(1, 1, P_DMA_HDR, v));
    CHECK(!s.peek(1, 1, W_FOREGROUND_WORDS, v));
    CHECK(!s.peek(1, 1, W_BACKGROUND_WORDS, v));
    CHECK(!s.peek(1, 1, W_DMA_FORWARD_COUNT, v));
    CHECK_EQ(s.ingress_egress(1).out_queues(), 0);
    CHECK(!s.ingress_egress(1).port_source().valid);
    s.write(1, 1, P_EVENTS_MASK, 0xFFFFFFFF); s.run_cycle(1); s.end_cycle(1);
    s.peek(2, 1, P_EVENTS_MASK, v);
    CHECK_EQ(v, 0xFFu);
}

namespace {
// The cycle whose loads first see the stream's flag, from now: ticks until then (or `limit` cycles).
int64_t flag_seen(Bench& b, unsigned q = 1, int64_t limit = 100000) {
    for (int64_t k = 0; k < limit; k++) { if (b.load(P_EVENTS_PEEK) & (EVENT_FOREGROUND_SERVICE << q)) return b.t; b.tick(); }
    return -1;
}
bool flag(Bench& b, unsigned q = 1) { return (b.load(P_EVENTS_PEEK) & (EVENT_FOREGROUND_SERVICE << q)) != 0; }
// n packets of `words` words on stream 2 from number `first`, tagged first, first + 1, ...
void adds(std::vector<uint8_t>& file, uint32_t first, unsigned n, unsigned words) { for (uint32_t k = first; k < first + n; k++) add(file, 2, k, k, words); }
// The words the sink took from `from` on are packet `tag`'s words first .. first + n - 1, in order.
bool ported(const Bench& b, size_t from, uint32_t tag, unsigned first, unsigned n) {
    if (b.ported.size() < from + n) return false;
    for (unsigned k = 0; k < n; k++) {
        const uint64_t lo = uint64_t(tag) << 32 | (first + k);
        if (b.ported[from + k].lo != lo || b.ported[from + k].hi != ~lo) return false;
    }
    return true;
}
}  // namespace

// II, "The rule for a service flag", line by line, for the foreground (no count) at a byte a cycle: a packet of w words
// is 16 + 16 w bytes, and one whose last byte arrives in c is seen by the loads of c + 1.
//   1. a word into an empty queue: the flag;
//   3. not for words that arrive while it already asks (a flag too many, if the rule looked at arrivals);
//   2. a DMA transfer ending with words left: the flag (a flag too few, if it looked only at arrivals);
//   and no flag while a transfer is in flight, nor at the end of one that took every word.
TEST(ingress_link_service_flag_rule) {
    std::vector<uint8_t> file;
    add(file, 1, 1, 0xA, 2);                                // 48 bytes: cycles 1 to 48
    add(file, 1, 2, 0xB, 1);                                // 32: to 80
    add(file, 1, 3, 0xC, 32);                               // 528: to 608
    Bench b(file, 1);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    CHECK_EQ(flag_seen(b, 0), int64_t(49));                 // line 1
    CHECK_EQ(b.events() & EVENT_FOREGROUND_SERVICE, EVENT_FOREGROUND_SERVICE);   // cleared by the load
    b.tick();
    CHECK(!flag(b, 0));
    b.run(40);                                              // the second packet: the queue already asks, so nothing (line 3)
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 3u);
    CHECK(!flag(b, 0));
    // a transfer of 2 of the 3: in flight, it does not ask; at its end, with one left, the flag (line 2)
    b.dma(0, 2, 64, 0x100, 1);
    CHECK(b.sent(0, 0xA, 0, 2, 0x100, 64, 1));
    CHECK(flag(b, 0));
    b.events(); b.tick();
    // a transfer of the last one, while the third packet arrives: at its end nothing is left, and nothing asks; then the
    // packet into the empty queue asks (line 1 again)
    b.dma(0, 1, 64, 0x100, 1);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK(!flag(b, 0));
    CHECK_EQ(flag_seen(b, 0), int64_t(609));
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 32u);
    b.events(); b.tick();
    // a packet that arrives while a transfer is in flight: the flag at the transfer's end, not at the arrival
    std::vector<uint8_t> two;
    add(two, 1, 1, 0xA, 32);                                // to 528
    add(two, 1, 2, 0xB, 1);                                 // to 560
    Bench c(two, 1);
    c.mask(EVENT_FOREGROUND_SERVICE | EVENT_DMA_DONE);
    CHECK_EQ(flag_seen(c, 0), int64_t(529));
    c.events(); c.tick();
    c.run(10);                                              // 540: a transfer of 32 words from here takes to about 575
    const size_t from = c.out.size();
    c.dma(0, 32, 64, 0x100, 1);                             // the second packet arrives at 560, in flight: no flag then
    CHECK(c.out[from + 31].t > 561);
    CHECK_EQ(c.load(W_FOREGROUND_WORDS), 1u);
    CHECK(flag(c, 0));                                      // at the transfer's end, for the word that came during it
    // C6: a transfer of 0 words is a transfer: at its end, with words left, the flag
    c.events(); c.tick();
    CHECK(!flag(c, 0));
    c.dma(0, 0, 64, 0x100, 1);
    CHECK(flag(c, 0));
    CHECK_EQ(c.load(W_FOREGROUND_WORDS), 1u);
    CHECK_EQ(c.load(P_ERRORS), 0u);
}

// The background's line: it asks only with the count at 0. Words arriving while the count runs ask nothing (a flag too
// many, if the count were not looked at); the count reaching 0 with words there posts the flag (a flag too few, if only
// arrivals posted).
TEST(ingress_link_service_flag_rule_background) {
    std::vector<uint8_t> file;
    adds(file, 1, 3, 32);                                   // 528 bytes each: to 528, 1056, 1584
    Bench b(file, 1);
    b.mask(EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.ready = Bench::NEVER;                                 // the Updater takes nothing
    b.store(W_DMA_FORWARD_COUNT, 40);
    b.run(2000);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 96u);
    CHECK(!flag(b));                                        // three packets in, the count not 0: no flag
    CHECK(b.ported.empty());
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 40u);
    b.ready = Bench::ALWAYS;
    const int64_t p = b.t;
    b.run(39);
    CHECK(!flag(b));
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 1u);
    b.tick();                                               // the 40th word moves in p + 39: the count at 0, 56 words left
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 0u);
    CHECK(flag(b));
    CHECK_EQ(b.ported.size(), size_t(40));
    CHECK_EQ(b.ported[0].t, p);
    CHECK_EQ(b.ported[39].t, p + 39);
    CHECK(ported(b, 0, 1, 0, 32));
    CHECK(ported(b, 32, 2, 0, 8));
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 56u);
    b.run(100);
    CHECK_EQ(b.ported.size(), size_t(40));                  // nothing more: the count is 0
    // a store of 0 with words waiting: the flag (it starts to ask); with the flag taken, another store of 0 posts nothing
    b.events(); b.tick();
    b.store(W_DMA_FORWARD_COUNT, 5);                        // a word moves in the store's cycle (the bench applies a store
    CHECK(!flag(b));                                        //   before the slot's cycle: periph_slot.h's level 0)
    b.store(W_DMA_FORWARD_COUNT, 0);                        // replaced with 4 left: stopped, 55 waiting
    CHECK_EQ(b.ported.size(), size_t(41));
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 55u);
    CHECK(flag(b));
    b.events(); b.tick();
    b.store(W_DMA_FORWARD_COUNT, 0);
    CHECK(!flag(b));
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// The count written before the words arrive, while they arrive, and after: a word a cycle once there is one and the
// sink is ready; the count read as what remains; the flag when it reaches 0 with words left.
TEST(ingress_link_forward_count_before_during_after) {
    std::vector<uint8_t> file;
    adds(file, 1, 2, 32);                                   // to 528 and 1056
    // before: the count in cycle 1; packet 1's words move in 529 to 560, packet 2's first 8 in 1057 to 1064
    {
        Bench b(file, 1);
        b.mask(EVENT_BACKGROUND_SERVICE);
        b.store(W_DMA_FORWARD_COUNT, 40);
        while (b.t < 600) b.tick();
        CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 8u);          // a load gives what remains
        CHECK_EQ(b.ported.size(), size_t(32));
        CHECK_EQ(b.ported[0].t, int64_t(529));
        CHECK_EQ(b.ported[31].t, int64_t(560));
        CHECK(!flag(b));                                    // the queue is empty, and the count not 0
        CHECK_EQ(flag_seen(b), int64_t(1065));
        CHECK_EQ(b.ported.size(), size_t(40));
        CHECK_EQ(b.ported[32].t, int64_t(1057));
        CHECK_EQ(b.ported[39].t, int64_t(1064));
        CHECK(ported(b, 0, 1, 0, 32) && ported(b, 32, 2, 0, 8));
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 24u);
        CHECK_EQ(b.ie().link_forwarded, 40u);
    }
    // during: the count in cycle 700, between the packets
    {
        Bench b(file, 1);
        b.mask(EVENT_BACKGROUND_SERVICE);
        CHECK_EQ(flag_seen(b), int64_t(529));               // the count is 0: packet 1 asks
        b.events();
        while (b.t < 700) b.tick();
        b.store(W_DMA_FORWARD_COUNT, 40);
        CHECK_EQ(b.ported.size(), size_t(1));               // the store's cycle: the first word moves
        CHECK_EQ(b.ported[0].t, int64_t(700));
        CHECK_EQ(flag_seen(b), int64_t(1065));
        CHECK_EQ(b.ported[31].t, int64_t(731));
        CHECK_EQ(b.ported[32].t, int64_t(1057));
        CHECK(ported(b, 0, 1, 0, 32) && ported(b, 32, 2, 0, 8));
    }
    // after: both packets in, the count in cycle 1100: 40 words in 1100 to 1139
    {
        Bench b(file, 1);
        b.mask(EVENT_BACKGROUND_SERVICE);
        while (b.t < 1100) b.tick();
        b.events();
        b.store(W_DMA_FORWARD_COUNT, 40);
        CHECK_EQ(flag_seen(b), int64_t(1140));
        CHECK_EQ(b.ported.size(), size_t(40));
        CHECK_EQ(b.ported[39].t, int64_t(1139));
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 24u);
    }
}

// A count across many packets of every length, sent and discarded; the discard runs at the link's rate whatever the
// sink does.
TEST(ingress_link_forward_count_across_packets_send_and_discard) {
    std::vector<uint8_t> file;
    uint32_t total = 0, number = 1;
    for (uint32_t k = 1; k <= 40; k++) { add(file, 2, number, k, k % 33); total += k % 33; number += k % 33 != 0; }   // 0 to 32 words: a length-0 packet takes no number
    for (bool discard : {false, true}) {
        Bench b(file, 1);
        b.mask(EVENT_BACKGROUND_SERVICE);
        if (discard) b.ready = Bench::NEVER;
        b.store(W_DMA_FORWARD_COUNT, (discard ? DISCARD : 0) | (total - 5));
        CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), (discard ? DISCARD : 0) | (total - 5));
        const int64_t seen = flag_seen(b, 1, 200000);
        CHECK(seen > 0);
        CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), discard ? DISCARD : 0u);   // [31] as written (C3)
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 5u);
        CHECK_EQ(b.ie().link_discarded, discard ? total - 5 : 0u);
        CHECK_EQ(b.ie().link_forwarded, discard ? 0u : total - 5);
        if (!discard) {
            size_t at = 0;
            for (uint32_t k = 1; k <= 40; k++) {
                const unsigned n = k % 33, take = at + n <= total - 5 ? n : unsigned(total - 5 - at);
                CHECK(ported(b, at, k, 0, take));
                at += take;
            }
        }
        CHECK(b.ported.size() == (discard ? 0u : size_t(total - 5)));
        // what is left is the last packet's last 5 words, read out by the DMA
        const size_t from = b.out.size();
        b.dma(1, 5, 64, 0x100, 1);
        CHECK(b.sent(from, 40, 2, 5, 0x100, 64, 1));
    }
}

// A store of 0 mid-frame stops the forwarding; a store replaces a running count; the words come out in order, once.
TEST(ingress_link_forward_count_stopped_and_replaced) {
    std::vector<uint8_t> file;
    adds(file, 1, 2, 32);
    Bench b(file, 1);
    b.mask(EVENT_BACKGROUND_SERVICE);
    b.store(W_DMA_FORWARD_COUNT, 60);
    while (b.t < 540) b.tick();                             // 529 to 539: 11 words
    b.store(W_DMA_FORWARD_COUNT, 0);
    CHECK_EQ(b.ported.size(), size_t(11));
    CHECK(flag(b));                                         // the count at 0, 21 words waiting: it asks
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 21u);
    b.run(30);
    CHECK_EQ(b.ported.size(), size_t(11));
    b.events(); b.tick();
    const int64_t t0 = b.t;
    b.store(W_DMA_FORWARD_COUNT, 100);                      // words move from t0
    b.run(2);
    b.store(W_DMA_FORWARD_COUNT, 5);                        // replaced at t0 + 3, with 97 left: 5 more, t0 + 3 to t0 + 7
    CHECK_EQ(flag_seen(b), t0 + 8);
    CHECK_EQ(b.ported.size(), size_t(19));
    CHECK(ported(b, 0, 1, 0, 19));
    CHECK_EQ(b.ported[18].t, t0 + 7);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 13u);
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// The sink's ready coming and going at random: a word moves exactly in the cycles both are 1, every word once, in order.
TEST(ingress_link_forward_count_ready_at_random) {
    std::vector<uint8_t> file;
    adds(file, 1, 60, 32);                                  // 1,920 words: more than the queue, so the link is held too
    for (int64_t rate : {0, 1}) {
        Bench b(file, rate);
        b.ready = Bench::RANDOM;
        b.mask(EVENT_BACKGROUND_SERVICE);
        b.store(W_DMA_FORWARD_COUNT, 1900);
        int64_t moves = 0, offered_ready = 0;
        for (int k = 0; k < 200000 && b.ported.size() < 1900; k++) {
            const bool valid = b.ie().port_source().valid, ready = (b.random_at(b.t) & 3) != 0;
            const size_t before = b.ported.size();
            b.tick();
            moves += int64_t(b.ported.size() - before);
            CHECK_EQ(b.ported.size() - before, size_t(valid && ready));   // a move is valid and ready, nothing else
            offered_ready += valid && ready;
        }
        CHECK_EQ(moves, int64_t(1900));
        CHECK_EQ(offered_ready, int64_t(1900));
        for (uint32_t k = 1; k <= 59; k++) CHECK(ported(b, 32 * (k - 1), k, 0, 32));
        CHECK(ported(b, 32 * 59, 60, 0, 12));
        CHECK_EQ(flag_seen(b, 1, 100000) > 0, true);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 20u);
        CHECK_EQ(b.load(P_ERRORS), 0u);
    }
}

// Forwarding and a DMA transfer from the foreground queue move words in the same cycles (II), each from its own queue.
TEST(ingress_link_forward_count_beside_a_foreground_transfer) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xB1, 32);
    add(file, 1, 1, 0xF1, 32);
    add(file, 2, 2, 0xB2, 32);
    add(file, 1, 2, 0xF2, 16);
    Bench b(file);                                          // as fast as taken: all four are in at once
    b.run(2);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 48u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 64u);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.events(); b.tick();
    b.s.write(b.t, 1, P_DMA_HDR, lw::SIZE_128 | 1u << HDR_COUNTER_LO | lw::M_WRITE << HDR_MODE_LO | 64u << HDR_DST_LO | 48u << DMA_HDR_COUNT_LO | 0u << DMA_HDR_QUEUE_LO);
    b.tick();
    b.s.write(b.t, 1, P_DMA_DEST_ADDR, 0x200); b.tick();
    b.store(W_DMA_FORWARD_COUNT, 64);                       // forwarding from here
    b.s.write(b.t, 1, P_DMA_START, 1); b.tick();            // the foreground transfer from here
    for (int k = 0; k < 200 && (b.load(P_STATUS) & 2); k++) b.tick();
    b.run(20);
    CHECK(b.sent_part(0, 0xF1, 0, 32, 0x200, 64, 1));
    CHECK(b.sent(32, 0xF2, 0, 16, 0x200 + 16 * 32, 64, 1));
    CHECK(ported(b, 0, 0xB1, 0, 32) && ported(b, 32, 0xB2, 0, 32));
    // in the same cycles: the DMA took a word in every cycle from the start's next, and so did the port
    int both = 0;
    for (const Landed& l : b.out) for (const Ported& w : b.ported) both += l.t - 1 == w.t;   // a packet leaves the cycle after its take
    CHECK(both >= 40);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// The queue: 512 words. With it full the link waits; the room comes back word by word, by the port and by the discard,
// and the next packet is taken when its data fits; its bytes start in the cycle after the one that made room.
TEST(ingress_link_queue_holds_512_words) {
    CHECK_EQ(IngressEgress::QUEUE_WORDS, 512u);
    std::vector<uint8_t> file;
    adds(file, 1, 18, 32);
    for (bool discard : {false, true}) {
        Bench b(file, 1);
        b.ready = Bench::NEVER;
        b.run(16 * 528 + 2000);
        CHECK_EQ(b.ie().link_packets, 16u);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 512u);
        CHECK(b.ie().link_room_cycles > 0 || b.ie().link_has_next);
        // 31 words of room: the 17th needs 32
        if (!discard) b.ready = Bench::ALWAYS;
        b.store(W_DMA_FORWARD_COUNT, (discard ? DISCARD : 0) | 31);
        b.run(1000);
        CHECK_EQ(b.ie().link_packets, 16u);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 481u);
        // one more: the room, and 528 cycles later the 17th
        const int64_t room = b.t;                           // the word leaves at the end of this cycle
        b.store(W_DMA_FORWARD_COUNT, (discard ? DISCARD : 0) | 1);
        while (b.t <= room + 528) { CHECK_EQ(b.ie().link_packets, 16u); b.tick(); }
        CHECK_EQ(b.ie().link_packets, 17u);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 512u);
        CHECK_EQ(b.ported.size(), size_t(discard ? 0 : 32));
        CHECK_EQ(b.ie().link_discarded, discard ? 32u : 0u);
        // and the words come out in order across the packets
        b.ready = Bench::ALWAYS;
        b.store(W_DMA_FORWARD_COUNT, 512);
        b.run(600);
        const size_t base = discard ? 0 : 32;
        for (uint32_t k = 2; k <= 17; k++) CHECK(ported(b, base + 32 * (k - 2), k, 0, 32));
        CHECK_EQ(b.load(P_ERRORS), 0u);
    }
}

// The two queues are independent: each asks for itself; the count holds the background only.
TEST(ingress_link_two_streams) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xB1, 4);
    add(file, 1, 1, 0xF1, 2);
    add(file, 2, 2, 0xB2, 3);
    add(file, 1, 2, 0xF2, 1);
    Bench b(file);
    b.run(2);
    CHECK_EQ(b.load(P_STATUS), ST_FOREGROUND_READY | ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 3u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 7u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.events(); b.tick();
    // the foreground read out, both its packets in one transfer, with the background's words still waiting
    b.dma(0, 3, 64, 0x200, 2);
    CHECK(b.sent_part(0, 0xF1, 0, 2, 0x200, 64, 2));
    CHECK(b.sent(2, 0xF2, 0, 1, 0x200 + 32, 64, 2));
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 7u);
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    CHECK_EQ(b.events() & (EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE), 0u);   // nothing left on the foreground
    b.tick();
    b.dma(1, 7, 64, 0x400, 2);
    CHECK(b.sent_part(3, 0xB1, 0, 4, 0x400, 64, 2));
    CHECK(b.sent(7, 0xB2, 0, 3, 0x400 + 64, 64, 2));
    CHECK_EQ(b.load(P_STATUS), 0u);
    // a count running on the background does not hold the foreground's flag
    std::vector<uint8_t> two;
    add(two, 2, 1, 0xB1, 32);                               // 528 bytes
    add(two, 1, 1, 0xF1, 1);                                // to 560
    Bench c(two, 1);
    c.ready = Bench::NEVER;
    c.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    c.store(W_DMA_FORWARD_COUNT, 100);
    CHECK_EQ(flag_seen(c, 0), int64_t(561));
    CHECK(!flag(c, 1));
    CHECK_EQ(c.load(P_ERRORS), 0u);
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

// The packet number, 18 bits (owner, 2026-11-17): 1 first, past 0xFFFF, and after 0x3FFFE, 1 again.
TEST(ingress_link_number_wraps) {
    std::vector<uint8_t> file;
    add(file, 2, 0xFFFF, 5, 1);
    add(file, 2, 0x10000, 6, 1);                            // the first number above 16 bits
    Bench a(file);
    a.ie().link.expect[1] = 0xFFFF;
    a.run(3);
    CHECK_EQ(a.load(P_ERRORS), 0u);
    CHECK_EQ(a.ie().link_packets, 2u);
    file.clear();
    add(file, 2, 0x3FFFD, 1, 1);
    add(file, 2, 0x3FFFE, 2, 1);
    add(file, 2, 1, 3, 1);
    add(file, 2, 2, 4, 1);
    Bench b(file);
    b.ie().link.expect[1] = 0x3FFFD;                        // as if 0x3FFFC packets had come
    b.run(3);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.ie().link_packets, 4u);
    std::vector<uint8_t> bad;                               // 0x3FFFF is no packet number
    add(bad, 2, 0x3FFFE, 1, 1);
    add(bad, 2, 0x3FFFF, 2, 1);
    Bench c(bad);
    c.ie().link.expect[1] = 0x3FFFE;
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

DEATH_TEST(ingress_link_a_transfer_past_the_words_waiting_is_ub, "a DMA transfer of 3 words from the background stream, which holds 2") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    Bench b(file);
    b.run(3);
    b.dma(1, 3, 64, 0x100, 1);
}
// II's undefined cases for the forward count.
DEATH_TEST(ingress_link_a_background_transfer_with_the_count_running_is_ub, "a DMA transfer from the background queue started with DMA_FORWARD_COUNT at 3") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    Bench b(file);
    b.ready = Bench::NEVER;
    b.run(3);
    b.store(W_DMA_FORWARD_COUNT, 3);
    b.dma(1, 1, 64, 0x100, 1);
}
DEATH_TEST(ingress_link_a_count_stored_with_a_background_transfer_in_flight_is_ub, "while a DMA transfer from the background queue is in flight") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);
    Bench b(file);
    b.run(3);
    b.s.write(b.t, 1, P_DMA_HDR, lw::SIZE_128 | lw::M_WRITE << HDR_MODE_LO | 64u << HDR_DST_LO | 32u << DMA_HDR_COUNT_LO | 1u << DMA_HDR_QUEUE_LO); b.tick();
    b.s.write(b.t, 1, P_DMA_START, 1); b.tick();
    b.run(10);
    b.store(W_DMA_FORWARD_COUNT, 5);
}
// In flight until dma_done (C1): a count stored in the cycle after dma_done is defined.
TEST(ingress_link_a_count_after_dma_done_is_defined) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);
    Bench b(file);
    b.run(3);
    b.dma(1, 30, 64, 0x100, 1);                             // to the cycle STATUS.dma_busy falls
    b.store(W_DMA_FORWARD_COUNT, 2);
    b.run(5);
    CHECK(ported(b, 0, 0xA, 30, 2));
    CHECK_EQ(b.load(P_ERRORS), 0u);
}
DEATH_TEST(ingress_link_a_transfer_with_no_word_is_ub, "from the foreground stream, which holds 0") {
    Bench b({});
    b.run(3);
    b.dma(0, 1, 64, 0x100, 1);
}

// ================================================================================================ the timing
// A byte a cycle from cycle 1: a packet whose last byte arrives in cycle c is seen by the loads of c + 1; an error at
// the cycle its packet's last byte would have arrived. (Waiting for room: ingress_link_buffer_holds_1024_words.)
TEST(ingress_link_a_byte_a_cycle) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);                               // 528 bytes: cycles 1 to 528
    file.insert(file.end(), 10, 0);                         // idle: 529 to 538
    add(file, 2, 2, 0xB, 1);                                // 32 bytes: 539 to 570
    for (uint32_t k = 3; k <= 4; k++) add(file, 2, k, k, 1);   // 571 to 602, to 634
    add(file, 2, 5, 5, 2);                                  // 48 bytes: to 682
    add(file, 2, 7, 7, 1);                                  // out of order: to 714
    Bench b(file, 1);
    while (b.t <= 528) { CHECK_EQ(b.load(P_STATUS), 0u); CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE); b.tick(); }
    CHECK_EQ(b.t, int64_t(529));
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 32u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_BACKGROUND_SERVICE);
    while (b.t <= 570) { CHECK_EQ(b.ie().link_packets, 1u); b.tick(); }
    CHECK_EQ(b.ie().link_packets, 2u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 33u);
    while (b.t <= 602) { CHECK_EQ(b.ie().link_packets, 2u); b.tick(); }
    while (b.t <= 634) { CHECK_EQ(b.ie().link_packets, 3u); b.tick(); }
    while (b.t <= 682) { CHECK_EQ(b.ie().link_packets, 4u); b.tick(); }
    CHECK_EQ(b.ie().link_packets, 5u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 37u);
    while (b.t <= 714) { CHECK_EQ(b.load(P_ERRORS), 0u); b.tick(); }
    CHECK_EQ(b.load(P_ERRORS), ERR_SEQUENCE);               // at the cycle the bad packet's last byte arrived
    CHECK_EQ(b.ie().link_room_cycles, 0u);
    // the words read out in one transfer: DMA_START at cycle p, a word a cycle from p + 1
    const int64_t p = b.t + 2;
    b.dma(1, 37, 64, 0x100, 1);
    CHECK(b.sent_part(0, 0xA, 0, 32, 0x100, 64, 1));
    CHECK(b.sent_part(32, 0xB, 0, 1, 0x100 + 16 * 32, 64, 1));
    CHECK(b.sent(35, 5, 0, 2, 0x100 + 16 * 35, 64, 1));
    CHECK_EQ(b.out[0].t, p + 2);                            // (taken in p + 1, leaving the DMA the cycle after)
    CHECK_EQ(b.out[36].t, p + 38);
}

// Faster links (WORKER4's C12): at 2, 4 and 8 bytes a cycle a packet of b bytes takes ceil(b / rate) cycles after the
// last; idle zeros count as bytes.
TEST(ingress_link_faster) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);                               // 528 bytes
    file.insert(file.end(), 3, 0);                          // 3 idle bytes go with the next packet: 35 bytes
    add(file, 2, 2, 0xB, 1);
    add(file, 2, 3, 0xC, 2);                                // 48 bytes
    for (int64_t rate : {1, 2, 4, 8}) {
        Bench b(file, 1);
        b.ie().link_bytes_per_cycle = rate;
        b.mask(EVENT_BACKGROUND_SERVICE);
        const int64_t first = (528 + rate - 1) / rate, second = first + (35 + rate - 1) / rate, third = second + (48 + rate - 1) / rate;
        CHECK_EQ(flag_seen(b), first + 1);
        b.events(); b.tick();
        while (b.t <= second) { CHECK_EQ(b.ie().link_packets, 1u); b.tick(); }
        CHECK_EQ(b.ie().link_packets, 2u);
        while (b.t <= third) { CHECK_EQ(b.ie().link_packets, 2u); b.tick(); }
        CHECK_EQ(b.ie().link_packets, 3u);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 35u);
    }
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
