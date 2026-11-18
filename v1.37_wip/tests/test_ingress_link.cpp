// test_ingress_link: Ingress's host side on the real link (v1.37 stage 4, the INTEGRATOR): WORKER8's Node behind
// Ingress/Egress (ingress_egress.h, THE LINK), the host's End at its far end (tests/host_end.h), from 17.6.3 to 17.6.7
// and chapter 28: the rule for a service flag; the word counts, a link packet's words landing a word a cycle from the
// cycle the Node takes it; reading out by the DMA, in pieces; the forward count; both queues; the queue's room and the
// stop bit holding the host back; the link's rate; noise on the line, recovered; the host's link starting again,
// link_error; the reset. Before the stage, the black box's link (its checks, its numbers, its errors) was tested here;
// a damaged link packet is no error now (28.14). The packet file's reader (units/host_link.h), which the tests' writer
// and gold's packetizer share and which says each link packet's stream and words, is tested here still.
//
// THE PACKETS are made by the tests' writer (tests/packet_writer.h), into a packet file the host's End sends link
// packet by link packet (HostEnd::from_packet_file). THE BENCH is the master tile's slot driven directly (SlotCore):
// Ingress/Egress as peripheral 1, a load, a store and a consuming load of EVENTS at cycle t as the slot's own calls,
// then the slot's cycle t. CYCLES are relative to the one the Node takes a link packet in (Bench::taken_at), since the
// link's own timing (its start, its status packets, its acknowledgements) is chapter 28's and not this test's.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"
#include "host_end.h"
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
// n packets of `words` words on stream 2 from number `first`, tagged first, first + 1, ...
void adds(std::vector<uint8_t>& file, uint32_t first, unsigned n, unsigned words) { for (uint32_t k = first; k < first + n; k++) add(file, 2, k, k, words); }
// The cycle whose loads first see queue q's flag, from now: ticks until then (or `limit` cycles).
int64_t flag_seen(Bench& b, unsigned q = 1, int64_t limit = 400000) {
    for (int64_t k = 0; k < limit; k++) { if (b.load(P_EVENTS_PEEK) & (EVENT_FOREGROUND_SERVICE << q)) return b.t; b.tick(); }
    return -1;
}
bool flag(Bench& b, unsigned q = 1) { return (b.load(P_EVENTS_PEEK) & (EVENT_FOREGROUND_SERVICE << q)) != 0; }
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

// ================================================================================================ the link's checks
TEST(link_crc32c) {
    using units::hostlink::crc32c;
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

// Chapter 28's own figures (spec/host-link.md 28.5, 28.6): its examples byte for byte, the mask, its cross-check.
TEST(link_documents_example) {
    using units::hostlink::crc32c;
    uint8_t zeros[16] = {};
    CHECK_EQ(crc32c(zeros, 16), 0x42709AEAu);              // "of 16 zero bytes"
    // "stream 2, no flags, length 1, number 1, acknowledge 0, the data one zero word"
    static const uint8_t example[16] = {0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0xA0, 0x8C, 0x06, 0xE1};
    CHECK_EQ(crc32c(example, 12), 0xE1068CA0u);            // the header CRC, over bytes 0 to 11
    CHECK_EQ(crc32c(example, 16), 0x48674BC7u);            // "the CRC-32C of all 16 header bytes of a good packet"
    // on the line the zero word is the mask itself (28.6's example 1: the register from 0xE1078CA0)
    static const uint8_t line[16] = {0x2A, 0x54, 0xC0, 0x74, 0xC0, 0x1C, 0x58, 0x2F, 0xA6, 0x5D, 0x27, 0x37, 0x39, 0xC9, 0x47, 0x1F};
    std::vector<uint8_t> bytes(example, example + 16);
    bytes.insert(bytes.end(), line, line + 16);
    units::hostlink::Reader reader;
    reader.open(bytes.data(), bytes.size());
    units::hostlink::Packet p;
    units::hostlink::Check check;
    size_t taken = 0;
    CHECK(reader.next(p, check, taken) == units::hostlink::Reader::Next::PACKET);
    CHECK(check == units::hostlink::Check::OK);
    CHECK_EQ(p.stream, 2u); CHECK_EQ(p.length, 1u); CHECK_EQ(p.number, 1u); CHECK_EQ(p.acknowledge, 0u); CHECK_EQ(p.flags, 0u);
    CHECK_EQ(p.lo[0], 0u); CHECK_EQ(p.hi[0], 0u);           // the mask taken off
    CHECK_EQ(taken, size_t(32));
    CHECK(reader.next(p, check, taken) == units::hostlink::Reader::Next::END);
    std::vector<uint8_t> written;                          // and the tests' writer makes the same 32 bytes
    packet_writer::Fields f;
    f.length = 1; f.number = 1;
    packet_writer::packet(written, f, zeros);
    CHECK(written == bytes);
    // 28.5's examples 2 to 4 and 28.6's 2 and 3: the writer's bytes, the reader's fields
    static const uint8_t hop1[16] = {0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0x9E, 0xBC, 0x0A, 0x21};
    static const uint8_t hop3[16] = {0x11, 0x01, 0xCD, 0xAB, 0x56, 0x55, 0x35, 0x02, 0xEA, 0x9A, 0x70, 0x42, 0xD2, 0xBF, 0xBA, 0x0A};
    static const uint8_t len0[16] = {0x01, 0x00, 0x05, 0x00, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x57, 0x09, 0x4D, 0x4E};
    static const uint8_t m2[32] = {0x01, 0x01, 0x07, 0x00, 0x0C, 0x00, 0x00, 0x00, 0xEB, 0x08, 0xC9, 0xD9, 0x9E, 0x94, 0x2A, 0x36,
                                   0xA4, 0xD0, 0x6E, 0x3C, 0x10, 0xA4, 0x2F, 0x89, 0xF5, 0x74, 0x21, 0x9E, 0xDE, 0x43, 0xE4, 0xC0};
    static const uint8_t m3[32] = {0x01, 0x01, 0x07, 0x00, 0x10, 0x00, 0x10, 0x00, 0xEB, 0x08, 0xC9, 0xD9, 0xA3, 0x2B, 0x76, 0x11,
                                   0x79, 0xF1, 0xF2, 0x82, 0x77, 0xD9, 0xB4, 0xAF, 0x91, 0x8F, 0x7C, 0x00, 0x39, 0x96, 0xA1, 0x51};
    uint8_t up[16];
    for (int i = 0; i < 16; i++) up[i] = uint8_t(i);
    const struct { const uint8_t* line; size_t n; unsigned stream, flags, stops, length; uint32_t number, acknowledge; unsigned hops; const uint8_t* data; } more[] = {
        {hop1, 16, 2, 0, 0, 1, 1, 0, 1, zeros}, {hop3, 16, 1, 1, 2, 1, 0x2ABCD, 0x15555, 3, zeros}, {len0, 16, 1, 0, 1, 0, 5, 4, 0, zeros},
        {m2, 32, 1, 0, 0, 1, 7, 3, 0, up}, {m3, 32, 1, 0, 0, 1, 7, 4, 1, up}};
    for (const auto& m : more) {
        std::vector<uint8_t> w;
        packet_writer::Fields g;
        g.stream = m.stream; g.flags = m.flags; g.stops = m.stops; g.length = m.length; g.number = m.number; g.acknowledge = m.acknowledge; g.hops = m.hops;
        packet_writer::packet(w, g, m.data);
        CHECK(w.size() == 16 + 16 * size_t(m.length) && std::equal(m.line, m.line + m.n, w.begin()));
        CHECK_EQ(crc32c(m.line, 16), 0x48674BC7u);
        units::hostlink::Reader rd;
        rd.open(w.data(), w.size());
        rd.expect = m.number;
        CHECK(rd.next(p, check, taken) == units::hostlink::Reader::Next::PACKET);
        CHECK(check == units::hostlink::Check::OK);
        CHECK_EQ(p.stream, m.stream); CHECK_EQ(p.flags, m.flags); CHECK_EQ(p.stops, m.stops);
        CHECK_EQ(p.number, m.number); CHECK_EQ(p.acknowledge, m.acknowledge); CHECK_EQ(p.hops, m.hops);
        if (m.length) { CHECK_EQ(p.lo[0], 0x0706050403020100ull * (m.data == up)); CHECK_EQ(p.hi[0], 0x0F0E0D0C0B0A0908ull * (m.data == up)); }
    }
}

// A header word's fields where 28.3 puts them, and the two checks over the bytes it names.
TEST(link_header_fields) {
    packet_writer::Fields f;
    f.stream = 2; f.flags = 0x5; f.length = 3;   // resend and recovery: restart (0x8) is the restart link packet's
    f.number = 0x2BCDE; f.acknowledge = 0x3A5A5; f.hops = 0xC; f.stops = 3;
    const std::vector<uint8_t> d = data_of(7, 3);
    std::vector<uint8_t> file;
    packet_writer::packet(file, f, d.data());
    CHECK_EQ(file.size(), size_t(16 + 48));
    CHECK_EQ(file[0], 0x52u);                               // [3:0] the stream, [7:4] the flags
    CHECK_EQ(file[1], 3u);                                  // [15:8] the length
    CHECK_EQ(uint32_t(file[2]) | uint32_t(file[3]) << 8 | uint32_t(file[4] & 3) << 16, 0x2BCDEu);          // [33:16]
    CHECK_EQ(uint32_t(file[4] >> 2) | uint32_t(file[5]) << 6 | uint32_t(file[6] & 15) << 14, 0x3A5A5u);   // [51:34]
    CHECK_EQ(uint32_t(file[6] >> 4), 0xCu);                                                             // [55:52] hops
    CHECK_EQ(file[7], 3u);                                  // [56] fg_stop, [57] bg_stop; [63:58] reserved
    auto u32 = [&](size_t at) { return uint32_t(file[at]) | uint32_t(file[at + 1]) << 8 | uint32_t(file[at + 2]) << 16 | uint32_t(file[at + 3]) << 24; };
    CHECK_EQ(u32(8), units::hostlink::crc32c(d.data(), 48));                    // [95:64] over the data as held
    CHECK(!std::equal(d.begin(), d.end(), file.begin() + 16));                  // which goes masked
    CHECK_EQ(u32(12), units::hostlink::crc32c(file.data(), 12));                // [127:96] over bits [95:0]
    units::hostlink::Reader r;
    r.open(file.data(), file.size());
    r.expect = 0x2BCDE;
    units::hostlink::Packet p;
    units::hostlink::Check check;
    size_t taken;
    CHECK(r.next(p, check, taken) == units::hostlink::Reader::Next::PACKET);
    CHECK(check == units::hostlink::Check::OK);
    CHECK_EQ(taken, size_t(64));
    CHECK_EQ(p.stream, 2u); CHECK_EQ(p.flags, 5u); CHECK_EQ(p.length, 3u); CHECK_EQ(p.number, 0x2BCDEu); CHECK_EQ(p.acknowledge, 0x3A5A5u);
    CHECK_EQ(p.hops, 0xCu); CHECK_EQ(p.stops, 3u);
    CHECK_EQ(p.lo[2], uint64_t(7) << 32 | 2);
    CHECK_EQ(p.hi[2], ~(uint64_t(7) << 32 | 2));
    CHECK(r.next(p, check, taken) == units::hostlink::Reader::Next::END);
}

// ================================================================================================ the interface
TEST(ingress_link_reset_values) {
    Bench b({});
    b.run(3);
    CHECK_EQ(b.load(P_PARAMS), 1u | 1u << 8);               // type 1, version 1 (17.6.1)
    CHECK_EQ(b.load(P_STATUS), 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 0u);              // 0 at the reset
    uint32_t v;
    CHECK(b.s.peek(b.t, 1, P_DMA_HDR, v));                  // the DMA's words are there
    CHECK(b.s.peek(b.t, 1, W_BACKGROUND_WORDS, v));
    CHECK(!b.s.peek(b.t, 1, 68, v));                        // 68 on is unmapped, the rest of block 4 (17.6.1)
    CHECK(!b.s.peek(b.t, 1, 80, v));                        // (II's words 80 to 82)
    CHECK(b.s.write(b.t, 1, W_DMA_FORWARD_COUNT, 0));       // R/W
    CHECK(!b.s.write(b.t, 1, W_BACKGROUND_WORDS, 0));       // R
    // its mask holds nine events (17.6.1: [8] egress_credit)
    b.s.write(b.t, 1, P_EVENTS_MASK, 0xFFFFFFFF); b.tick();
    CHECK_EQ(b.load(P_EVENTS_MASK), 0x1FFu);
}

// 28.1, 28.9: the link starts by itself from power-on, both ends, and the first data goes once it is in step; while
// it starts, nothing moves. A link packet of 2 words on the foreground: taken once the link is in step, its words
// landing from that cycle, seen by the loads of the next.
TEST(ingress_link_starts_and_takes_the_first_link_packet) {
    std::vector<uint8_t> file;
    add(file, 1, 1, 0xA, 2);
    Bench b(file);
    const int64_t t = b.until_taken(1);
    CHECK(t > 0);
    CHECK(b.ie().node->above().in_step);
    CHECK(b.host.end.status().in_step);
    std::printf("test_ingress_link: the first link packet taken in cycle %lld, the link started\n", (long long)t);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 1u);               // the first word landed at the end of t
    b.tick();
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 2u);
    CHECK_EQ(b.load(P_STATUS), ST_FOREGROUND_READY);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    const ::hostlink::LinkCounts n = b.ie().node->counts_above();
    CHECK_EQ(n.taken, 1u);
    CHECK_EQ(n.bad_header + n.bad_data + n.bad_fields + n.refused + n.restarts, 0u);
}

// 17.6.7, line by line, for the foreground (no count):
//   1. a word into an empty queue: the flag;
//   3. not for words that arrive while it already asks;
//   2. a DMA transfer ending with words left: the flag;
//   and no flag while a transfer is in flight, nor at the end of one that took every word. The host's third link
// packet held back (HostEnd::gate) until the first two are read out.
TEST(ingress_link_service_flag_rule) {
    std::vector<uint8_t> file;
    add(file, 1, 1, 0xA, 2);
    add(file, 1, 2, 0xB, 1);
    add(file, 1, 3, 0xC, 32);
    Bench b(file);
    bool third = false;
    b.host.gate = [&](size_t i, int64_t) { return i < 2 || third; };
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    const int64_t t1 = b.until_taken(1);
    CHECK_EQ(flag_seen(b, 0), t1 + 1);                       // line 1: the first word landed at the end of t1
    CHECK_EQ(b.events() & EVENT_FOREGROUND_SERVICE, EVENT_FOREGROUND_SERVICE);   // cleared by the load
    b.tick();
    CHECK(!flag(b, 0));
    b.until_taken(2);
    b.run(3);                                               // the second packet: the queue already asks, so nothing (line 3)
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 3u);
    CHECK(!flag(b, 0));
    // a transfer of 2 of the 3: in flight, it does not ask; at its end, with one left, the flag (line 2)
    b.dma(0, 2, 64, 0x100, 1);
    CHECK(b.sent(0, 0xA, 0, 2, 0x100, 64, 1));
    CHECK(flag(b, 0));
    b.events(); b.tick();
    // the last one: at the transfer's end nothing is left, and nothing asks; then the third packet into the empty
    // queue asks (line 1 again)
    b.dma(0, 1, 64, 0x100, 1);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK(!flag(b, 0));
    third = true;
    const int64_t t3 = b.until_taken(3);
    CHECK_EQ(flag_seen(b, 0), t3 + 1);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 1u);               // 17.6.3: a word a cycle
    b.run(31);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 32u);
    // C6: a transfer of 0 words is a transfer: at its end, with words left, the flag
    b.events(); b.tick();
    CHECK(!flag(b, 0));
    b.dma(0, 0, 64, 0x100, 1);
    CHECK(flag(b, 0));
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// The background's line: it asks only with the count at 0. Words arriving while the count runs ask nothing; the count
// reaching 0 with words there posts the flag.
TEST(ingress_link_service_flag_rule_background) {
    std::vector<uint8_t> file;
    adds(file, 1, 3, 32);
    Bench b(file);
    b.mask(EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.ready = Bench::NEVER;                                 // the Updater takes nothing
    b.store(W_DMA_FORWARD_COUNT, 40);
    b.until_taken(3);
    b.run(40);
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
    // a store of 0 with words waiting: the flag; with the flag taken, another store of 0 posts nothing
    b.events(); b.tick();
    b.store(W_DMA_FORWARD_COUNT, 5);
    CHECK(!flag(b));
    b.store(W_DMA_FORWARD_COUNT, 0);
    CHECK_EQ(b.ported.size(), size_t(41));
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 55u);
    CHECK(flag(b));
    b.events(); b.tick();
    b.store(W_DMA_FORWARD_COUNT, 0);
    CHECK(!flag(b));
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// 17.6.5: the count stored before the words arrive: a word a cycle from the cycle after each lands (17.6.3), the
// sink ready; the count read as what remains; the flag when it reaches 0 with words left.
TEST(ingress_link_forward_count_before_the_words) {
    std::vector<uint8_t> file;
    adds(file, 1, 2, 32);
    Bench b(file);
    bool second = false;
    b.host.gate = [&](size_t i, int64_t) { return i < 1 || second; };
    b.mask(EVENT_BACKGROUND_SERVICE);
    b.store(W_DMA_FORWARD_COUNT, 40);
    const int64_t t1 = b.until_taken(1);
    b.run(40);
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 8u);              // a load gives what remains
    CHECK_EQ(b.ported.size(), size_t(32));
    CHECK_EQ(b.ported[0].t, t1 + 1);                        // landed at the end of t1, moved in the next
    CHECK_EQ(b.ported[31].t, t1 + 32);
    CHECK(!flag(b));                                        // the queue is empty, and the count not 0
    second = true;
    const int64_t t2 = b.until_taken(2);
    CHECK_EQ(flag_seen(b), t2 + 9);                         // 8 words moved in t2 + 1 to t2 + 8: the count 0, words left
    CHECK_EQ(b.ported.size(), size_t(40));
    CHECK_EQ(b.ported[32].t, t2 + 1);
    CHECK_EQ(b.ported[39].t, t2 + 8);
    CHECK(ported(b, 0, 1, 0, 32) && ported(b, 32, 2, 0, 8));
    b.run(30);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 24u);
    CHECK_EQ(b.ie().link_forwarded, 40u);
}

// A count across many packets of every length, sent and discarded; the discard runs whatever the sink does.
TEST(ingress_link_forward_count_across_packets_send_and_discard) {
    std::vector<uint8_t> file;
    uint32_t total = 0, number = 1, packets = 0;
    for (uint32_t k = 1; k <= 40; k++) {
        const unsigned n = 1 + (k * 7) % 32;
        add(file, 2, number++, k, n); total += n; packets++;
    }
    for (bool discard : {false, true}) {
        Bench b(file);
        b.mask(EVENT_BACKGROUND_SERVICE);
        if (discard) b.ready = Bench::NEVER;
        b.store(W_DMA_FORWARD_COUNT, (discard ? DISCARD : 0) | (total - 5));
        CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), (discard ? DISCARD : 0) | (total - 5));
        const int64_t seen = flag_seen(b, 1, 600000);
        CHECK(seen > 0);
        CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), discard ? DISCARD : 0u);   // [31] as written (C3)
        b.run(40);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 5u);
        CHECK_EQ(b.ie().link_discarded, discard ? total - 5 : 0u);
        CHECK_EQ(b.ie().link_forwarded, discard ? 0u : total - 5);
        CHECK_EQ(b.ie().link_packets, uint64_t(packets));
        if (!discard) {
            size_t at = 0;
            for (uint32_t k = 1; k <= 40; k++) {
                const unsigned n = 1 + (k * 7) % 32, take = at + n <= total - 5 ? n : unsigned(total - 5 - at);
                CHECK(ported(b, at, k, 0, take));
                at += take;
            }
        }
        const size_t from = b.out.size();
        b.dma(1, 5, 64, 0x100, 1);
        const unsigned last = 1 + (40 * 7) % 32;
        CHECK(b.sent(from, 40, last - 5, 5, 0x100, 64, 1));
    }
}

// A store of 0 mid-frame stops the forwarding; a store replaces a running count; the words come out in order, once.
TEST(ingress_link_forward_count_stopped_and_replaced) {
    std::vector<uint8_t> file;
    adds(file, 1, 2, 32);
    Bench b(file);
    b.mask(EVENT_BACKGROUND_SERVICE);
    b.store(W_DMA_FORWARD_COUNT, 60);
    const int64_t t1 = b.until_taken(1);
    while (b.t < t1 + 12) b.tick();                         // t1 + 1 to t1 + 11: 11 words
    b.store(W_DMA_FORWARD_COUNT, 0);
    CHECK_EQ(b.ported.size(), size_t(11));
    CHECK(flag(b));                                         // the count at 0, words waiting: it asks
    b.run(60);
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
    CHECK_EQ(b.load(W_DMA_FORWARD_COUNT), 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// The sink's ready coming and going at random: a word moves exactly in the cycles both are 1, every word once, in
// order; the link held back by the stop bit meanwhile (60 packets, more than the queue).
TEST(ingress_link_forward_count_ready_at_random) {
    std::vector<uint8_t> file;
    adds(file, 1, 60, 32);
    for (unsigned rate : {1u, 4u}) {
        Bench b(file, rate);
        b.ready = Bench::RANDOM;
        b.mask(EVENT_BACKGROUND_SERVICE);
        b.store(W_DMA_FORWARD_COUNT, 1900);
        int64_t moves = 0, offered_ready = 0;
        for (int k = 0; k < 600000 && b.ported.size() < 1900; k++) {
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
        CHECK(flag_seen(b, 1, 100000) > 0);
        b.run(40);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), 20u);
        CHECK_EQ(b.load(P_ERRORS), 0u);
        CHECK_EQ(b.ie().node->counts_above().refused, 0u);
    }
}

// Forwarding and a DMA transfer from the foreground queue move words in the same cycles, each from its own queue.
TEST(ingress_link_forward_count_beside_a_foreground_transfer) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xB1, 32);                               // one run of numbers for the link (28.4)
    add(file, 1, 2, 0xF1, 32);
    add(file, 2, 3, 0xB2, 32);
    add(file, 1, 4, 0xF2, 16);
    Bench b(file);
    b.until_taken(4);
    b.run(40);
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
    int both = 0;
    for (const Ported& p : b.ported) for (const Landed& l : b.out) if (l.t == p.t + 1) both++;
    CHECK(both > 40);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// 17.6.3, 28.10: the queue is INGRESS_QUEUE_WORDS, 1,024 words; its stop bit goes up at half full, of the words
// landed and still to land, and holds the host back: link packets with no number yet wait; those already on their way
// (the window) still come, and fit, since half the queue is kept for them. Nothing draining the queue, 40 link packets
// of 32 words: the queue fills to past half and no further than half and the window, the stop bit up, the host waiting,
// the chip's end refusing nothing. Then the queue drained: the stop bit falls and every word comes, in order.
TEST(ingress_link_queue_stop_at_half_holds_the_host_back) {
    const unsigned Q = INGRESS_QUEUE_WORDS_DEFAULT;
    std::vector<uint8_t> file;
    adds(file, 1, 40, 32);
    Bench b(file);
    b.ready = Bench::NEVER;
    b.store(W_DMA_FORWARD_COUNT, 40 * 32);
    b.run(60000);
    const unsigned held = b.load(W_BACKGROUND_WORDS);
    CHECK(held >= Q / 2 && held <= Q / 2 + 8 * 32);
    CHECK(b.ie().node->above().stop_said[1]);
    CHECK(!b.ie().node->above().stop_said[0]);              // the foreground's own: a full background never holds it
    CHECK(b.host.end.status().stop_heard[1]);
    CHECK(b.host.stop_cycles > 0);
    CHECK_EQ(b.ie().node->counts_above().refused, 0u);
    std::printf("test_ingress_link: nothing draining, the queue held at %u words, the stop bit up\n", held);
    b.ready = Bench::ALWAYS;
    for (int k = 0; k < 400000 && b.ported.size() < 40 * 32; k++) b.tick();
    CHECK_EQ(b.ported.size(), size_t(40 * 32));
    for (uint32_t k = 1; k <= 40; k++) CHECK(ported(b, 32 * (k - 1), k, 0, 32));
    CHECK(!b.ie().node->above().stop_said[1]);
    CHECK_EQ(b.ie().node->counts_above().refused, 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}
// INGRESS_QUEUE_WORDS is a parameter (19): a power of two from 64 to 4,096; the Node's queue the same.
TEST(ingress_link_queue_words_is_a_parameter) {
    for (unsigned q : {512u, 4096u}) {
        std::vector<uint8_t> file;
        adds(file, 1, q / 32, 32);
        Bench b(file, 1, q);
        b.ready = Bench::NEVER;
        b.run(int64_t(q) * 40 + 20000);
        CHECK(b.load(W_BACKGROUND_WORDS) >= q / 2);
        CHECK(b.load(W_BACKGROUND_WORDS) <= q / 2 + 8 * 32);
        CHECK_EQ(b.ie().node->counts_above().refused, 0u);
    }
}
DEATH_TEST(ingress_link_queue_words_not_a_power_of_two_is_refused, "INGRESS_QUEUE_WORDS must be a power of two") {
    Bench b({}, 1, 1000);
}

// The two queues are independent: each asks for itself; the count holds the background only.
TEST(ingress_link_two_streams) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xB1, 4);                                // one run of numbers for the link (28.4)
    add(file, 1, 2, 0xF1, 2);
    add(file, 2, 3, 0xB2, 3);
    add(file, 1, 4, 0xF2, 1);
    Bench b(file);
    b.until_taken(4);
    b.run(12);
    CHECK_EQ(b.load(P_STATUS), ST_FOREGROUND_READY | ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 3u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 7u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.events(); b.tick();
    b.dma(0, 3, 64, 0x200, 2);
    CHECK(b.sent_part(0, 0xF1, 0, 2, 0x200, 64, 2));
    CHECK(b.sent(2, 0xF2, 0, 1, 0x200 + 32, 64, 2));
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 7u);
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    CHECK_EQ(b.events() & (EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE), 0u);
    b.tick();
    b.dma(1, 7, 64, 0x400, 2);
    CHECK(b.sent_part(3, 0xB1, 0, 4, 0x400, 64, 2));
    CHECK(b.sent(7, 0xB2, 0, 3, 0x400 + 64, 64, 2));
    CHECK_EQ(b.load(P_STATUS), 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// ================================================================================================ the link's own
// 28.1, the line's rate: at 1, 2, 4 and 8 link cycles a chip cycle, and a link cycle every 4 chip cycles, the link
// starts and takes the same link packets in the same order; in link cycles it is the same link but for the wire,
// which is a chip cycle long (host_end.h), so a round trip is a few link cycles more at a higher rate; in chip cycles
// each rate is faster than the one below it.
TEST(ingress_link_rates) {
    std::vector<uint8_t> file;
    adds(file, 1, 3, 32);
    std::vector<int64_t> before;
    std::vector<uint64_t> at1;
    for (unsigned rate : {1u, 2u, 4u, 8u}) {
        Bench b(file, rate);
        b.store(W_DMA_FORWARD_COUNT, 96);
        b.until_taken(3);
        b.run(40);
        CHECK_EQ(b.taken_at.size(), size_t(3));
        CHECK_EQ(b.ported.size(), size_t(96));
        for (uint32_t k = 1; k <= 3; k++) CHECK(ported(b, 32 * (k - 1), k, 0, 32));
        if (rate == 1) at1 = b.taken_link_cycle;
        for (size_t k = 0; k < 3 && k < at1.size() && k < b.taken_link_cycle.size(); k++) {
            CHECK(b.taken_link_cycle[k] >= at1[k] && b.taken_link_cycle[k] <= at1[k] + 64);
            if (!before.empty()) CHECK(b.taken_at[k] < before[k]);
        }
        std::printf("test_ingress_link: at %u link cycles a chip cycle, the link packets taken in chip cycles %lld %lld %lld (link cycles %llu %llu %llu)\n",
                    rate, (long long)b.taken_at[0], (long long)b.taken_at[1], (long long)b.taken_at[2], (unsigned long long)b.taken_link_cycle[0],
                    (unsigned long long)b.taken_link_cycle[1], (unsigned long long)b.taken_link_cycle[2]);
        before = b.taken_at;
    }
    Bench slow(file, 1, INGRESS_QUEUE_WORDS_DEFAULT, 4);
    slow.until_taken(3, 1600000);
    CHECK(slow.taken_link_cycle == at1);                    // a link cycle every 4 chip cycles: the wire is a link cycle
    for (size_t k = 0; k < 3 && k < at1.size() && k < slow.taken_at.size(); k++) CHECK_EQ(uint64_t(slow.taken_at[k]), 4 * at1[k]);
}

// 28.8, 28.14: noise on the line is no error. A bit of a byte the chip receives changed, one byte in 2,000 for the
// link's first 100,000 cycles: link packets damaged and thrown away, asked for again and sent again, the link in step
// again; every word arrives once and in order, and ERRORS stays 0.
TEST(ingress_link_noise_on_the_line_is_recovered_and_no_error) {
    std::vector<uint8_t> file;
    adds(file, 1, 20, 32);
    Bench b(file);
    b.host.noise = [](int64_t t, unsigned, uint8_t x) {
        const uint64_t h = uint64_t(t) * 0x9E3779B97F4A7C15ull;
        return t < 100000 && (h >> 40) % 2000 == 0 ? uint8_t(x ^ (1u << (h >> 61))) : x;
    };
    b.store(W_DMA_FORWARD_COUNT, 20 * 32);
    for (int k = 0; k < 600000 && b.ported.size() < 20 * 32; k++) b.tick();
    CHECK_EQ(b.ported.size(), size_t(20 * 32));
    for (uint32_t k = 1; k <= 20; k++) CHECK(ported(b, 32 * (k - 1), k, 0, 32));
    const ::hostlink::LinkCounts n = b.ie().node->counts_above();
    CHECK(n.bad_header + n.bad_data >= 3);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    std::printf("test_ingress_link: noise: %llu bad headers, %llu bad data, %llu ahead, %llu behind, %llu sent again by the host\n",
                (unsigned long long)n.bad_header, (unsigned long long)n.bad_data, (unsigned long long)n.ahead, (unsigned long long)n.behind,
                (unsigned long long)b.host.end.counts().sent_again);
}

// 28.9, 17.6.1: the host's end starting its link again (its power_on) once the link is in step: the chip's link end
// hears "I have started again", starts its link again too, and ERRORS.link_error is 1, sticky (nothing clears it but
// the reset, 28.13.2), with format_error 0; the tile's ERRORS.peripheral_1. The reset clears it: the chip's link end
// starts again itself, the host's hears it.
TEST(ingress_link_the_host_starting_again_is_link_error_until_the_reset) {
    std::vector<uint8_t> file;
    adds(file, 1, 4, 8);
    Bench b(file);
    const int64_t t1 = b.until_taken(1);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    b.host.hook = [t1](::hostlink::End& end, int64_t t) { if (t == t1 + 2000) end.power_on(); };
    b.run(30000);
    CHECK_EQ(b.load(P_ERRORS), ERR_LINK);
    CHECK_EQ(b.s.live_errors(b.t), 1u);
    CHECK(b.ie().node->above().other_end);
    CHECK(!b.ie().node->above().made_wrong);
    b.run(30000);
    CHECK_EQ(b.load(P_ERRORS), ERR_LINK);                   // sticky
    b.host.hook = nullptr;
    b.host.end.clear_error();
    b.s.reset(b.t);
    CHECK_EQ(b.load(P_ERRORS), 0u);
    CHECK_EQ(b.load(P_STATUS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    b.run(30000);
    CHECK(b.ie().node->above().in_step);
    CHECK(b.host.end.status().other_end);                   // the chip's reset said "I have started again"
    CHECK_EQ(b.load(P_ERRORS), 0u);                         // under the chip, no error: it started itself
}

// ================================================================================================ the UB
DEATH_TEST(ingress_link_a_transfer_past_the_words_waiting_is_ub, "a DMA transfer of 3 words from the background queue, which holds 2") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    Bench b(file);
    b.until_taken(1);
    b.run(3);
    b.dma(1, 3, 64, 0x100, 1);
}
DEATH_TEST(ingress_link_a_background_transfer_with_the_count_running_is_ub, "a DMA transfer from the background queue started with DMA_FORWARD_COUNT at 3") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    Bench b(file);
    b.ready = Bench::NEVER;
    b.until_taken(1);
    b.run(3);
    b.store(W_DMA_FORWARD_COUNT, 3);
    b.dma(1, 1, 64, 0x100, 1);
}
DEATH_TEST(ingress_link_a_count_stored_with_a_background_transfer_in_flight_is_ub, "while a DMA transfer from the background queue is in flight") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 32);
    Bench b(file);
    b.until_taken(1);
    b.run(40);
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
    b.until_taken(1);
    b.run(40);
    b.dma(1, 30, 64, 0x100, 1);
    b.store(W_DMA_FORWARD_COUNT, 2);
    b.run(5);
    CHECK(ported(b, 0, 0xA, 30, 2));
    CHECK_EQ(b.load(P_ERRORS), 0u);
}
DEATH_TEST(ingress_link_a_transfer_with_no_word_is_ub, "from the foreground queue, which holds 0") {
    Bench b({});
    b.run(3);
    b.dma(0, 1, 64, 0x100, 1);
}

// ================================================================================================ a whole file
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
    for (int guard = 0; guard < 2000000 && back.size() < frames.size(); guard++) {
        b.tick();
        const uint32_t words = b.load(W_BACKGROUND_WORDS);
        if (words == 0) continue;
        b.events();
        b.tick();
        b.dma(1, b.load(W_BACKGROUND_WORDS), 64, 0x100, 1);
        for (; taken < b.out.size(); taken++) for (uint64_t half : {b.out[taken].lo, b.out[taken].hi}) for (unsigned k = 0; k < 8; k++) back.push_back(uint8_t(half >> (8 * k)));
    }
    CHECK(back == frames);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}
