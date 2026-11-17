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
    CHECK_EQ(b.load(W_FOREGROUND_THRESHOLD), 1u);           // the thresholds, 1 at the reset
    CHECK_EQ(b.load(W_BACKGROUND_THRESHOLD), 1u);
    CHECK(!b.s.peek(b.t, 1, 84, v));                        // the rest of block 5 is unmapped
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
    CHECK(!s.peek(1, 1, W_FOREGROUND_THRESHOLD, v));
    CHECK(!s.peek(1, 1, W_BACKGROUND_THRESHOLD, v));
    CHECK_EQ(s.ingress_egress(1).out_queues(), 0);
    s.write(1, 1, P_EVENTS_MASK, 0xFFFFFFFF); s.run_cycle(1); s.end_cycle(1);
    s.peek(2, 1, P_EVENTS_MASK, v);
    CHECK_EQ(v, 0xFFu);
}

// II, "The rule for a service flag", with the threshold at 1 (its reset value): the words waiting in every packet
// that has arrived; the flag cleared by the load of EVENTS; read out in pieces, one across a packet's end; no further
// flag until every word the register showed is read out.
TEST(ingress_link_service_flag_rule) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 5);
    add(file, 2, 2, 0xB, 32);
    Bench b(file);
    b.run(2);
    // both packets are there: the words of both, the ready bit, one flag
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 37u);
    CHECK_EQ(b.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE | EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(b.ie().link_stream[1].owed, 5u);               // posted at the first one's arrival: the register showed 5
    CHECK_EQ(b.s.live_events(b.t), 0u);                     // masked: it wakes nothing
    b.mask(EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    CHECK_EQ(b.s.live_events(b.t), 1u);                     // the tile's EVENTS.peripheral_1
    // the flag is cleared by the load of EVENTS
    CHECK_EQ(b.events(), EVENT_IDLE | EVENT_BACKGROUND_SERVICE);
    b.tick();
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(P_STATUS), ST_BACKGROUND_READY);
    // one word out, to the master's own memory
    b.dma(1, 1, 64, 0x00000100, 3);
    CHECK(b.sent(0, 0xA, 0, 1, 0x100, 64, 3));
    CHECK_EQ(b.events() & EVENT_DMA_DONE, EVENT_DMA_DONE);
    b.run(50);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);            // no further flag
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 36u);              // the count is the words still waiting
    CHECK_EQ(b.ie().link_stream[1].owed, 4u);
    // six more, to the Updater's segment queue: across the first packet's end. When the fifth word, the last the
    // register showed, is taken, the stream is looked at again: 32 wait, and the flag is posted
    b.dma(1, 6, 64, 0x05100000, 1);
    CHECK(b.sent_part(1, 0xA, 1, 4, 0x05100000, 64, 1));
    CHECK(b.sent(5, 0xB, 0, 2, 0x05100000 + 16 * 4, 64, 1));
    CHECK_EQ(b.events() & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(b.ie().link_stream[1].owed, 30u);
    b.tick();
    b.run(50);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 30u);
    // the rest in one transfer, to another tile, uncounted: then nothing waits and nothing asks
    const size_t from = b.out.size();
    b.dma(1, 30, 66, 0x03000040, 0);
    CHECK(b.sent(from, 0xB, 2, 30, 0x03000040, 66, 0));
    b.events();
    b.run(5);
    CHECK_EQ(b.load(P_STATUS), 0u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK), EVENT_IDLE);              // the 30 owed are out, and nothing waits
    CHECK_EQ(b.ie().link_stream[1].owed, 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

namespace {
// The cycle whose loads first see the stream's flag, from now: ticks until then (or `limit` cycles).
int64_t flag_seen(Bench& b, unsigned q = 1, int64_t limit = 100000) {
    for (int64_t k = 0; k < limit; k++) { if (b.load(P_EVENTS_PEEK) & (EVENT_FOREGROUND_SERVICE << q)) return b.t; b.tick(); }
    return -1;
}
// n packets of `words` words on stream 2 from number `first`, tagged first, first + 1, ...
void adds(std::vector<uint8_t>& file, uint32_t first, unsigned n, unsigned words) { for (uint32_t k = first; k < first + n; k++) add(file, 2, k, k, words); }
}  // namespace

// "After a flag, none until the core has read out the words the register showed" (II): the count at the post is what
// is owed. A packet arriving while words are owed posts nothing; the last owed word's read-out looks again, and a flag
// is posted for what has come since.
TEST(ingress_link_second_flag) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);                                // 48 bytes: cycles 1 to 48
    add(file, 2, 2, 0xB, 1);                                // 32 bytes: to 80
    Bench b(file, 1);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    CHECK_EQ(flag_seen(b), int64_t(49));
    b.events(); b.tick();                                   // taken
    CHECK_EQ(b.ie().link_stream[1].owed, 2u);
    b.run(40);                                              // the second packet arrives
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 3u);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);   // nothing: two words are owed
    b.dma(1, 1, 64, 0x100, 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);   // one still owed
    b.dma(1, 1, 64, 0x100, 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);   // at once, for the third
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1u);
    CHECK_EQ(b.ie().link_stream[1].owed, 1u);
    // A core that read all three in one transfer, having loaded the register after the second came: the flag for the
    // third is posted when the second owed word is taken, and the transfer then takes the third. The flag is there with
    // nothing waiting (WORKER4's C3, C7: the firmware loads the count after a flag, and finds 0).
    Bench c(file, 1);
    c.mask(EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    flag_seen(c);
    c.events(); c.tick();
    c.run(40);
    c.dma(1, 3, 64, 0x100, 1);
    CHECK_EQ(c.events() & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(c.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(c.ie().link_stream[1].owed, 0u);
    c.tick(); c.run(500);
    CHECK_EQ(c.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
}

// Thresholds of 1, of a part of a packet, of several packets, and of the whole buffer, at a byte a cycle: a packet of 32
// words is 528 bytes, so the k-th arrives at the end of cycle 528 k.
TEST(ingress_link_thresholds) {
    std::vector<uint8_t> file;
    adds(file, 1, 33, 32);
    struct Case { uint32_t threshold; int64_t seen; uint32_t words; };
    for (const Case& k : {Case{1, 529, 32}, Case{5, 529, 32}, Case{32, 529, 32}, Case{33, 1057, 64}, Case{100, 4 * 528 + 1, 128},
                          Case{1024, 32 * 528 + 1, 1024}}) {
        Bench b(file, 1);
        b.store(W_BACKGROUND_THRESHOLD, k.threshold);       // in cycle 1, before anything has arrived
        CHECK_EQ(b.load(W_BACKGROUND_THRESHOLD), k.threshold);
        CHECK_EQ(flag_seen(b), k.seen);
        CHECK_EQ(b.load(W_BACKGROUND_WORDS), k.words);
        CHECK_EQ(b.ie().link_stream[1].owed, k.words);
    }
    // a threshold of 0 acts as 1 (C4): a flag needs a word
    Bench z(file, 1);
    z.store(W_BACKGROUND_THRESHOLD, 0);
    CHECK_EQ(z.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    CHECK_EQ(flag_seen(z), int64_t(529));
}

// The buffer: 1,024 words a stream. With it full the link waits; room comes back word by word as words are read out,
// and the next packet is taken when its data fits; its bytes start in the cycle after the one that made room.
TEST(ingress_link_buffer_holds_1024_words) {
    std::vector<uint8_t> file;
    adds(file, 1, 33, 32);
    Bench b(file, 1);
    b.run(32 * 528 + 2000);
    CHECK_EQ(b.ie().link_packets, 32u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1024u);
    b.dma(1, 31, 64, 0x100, 1);                             // 31 words of room: the 33rd needs 32
    b.run(1000);
    CHECK_EQ(b.ie().link_packets, 32u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 993u);
    const size_t from = b.out.size();
    b.dma(1, 1, 64, 0x100, 1);
    const int64_t room = b.out[from].t - 1;                 // the cycle the word was taken in: room at its end
    while (b.t <= room + 528) { CHECK_EQ(b.ie().link_packets, 32u); b.tick(); }
    CHECK_EQ(b.ie().link_packets, 33u);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1024u);
    CHECK_EQ(int64_t(b.ie().link_room_cycles), room - 32 * 528);   // the link waited from the 32nd's end to the room
    // and the words come out in order, across the 32 packets and into the 33rd
    const size_t at = b.out.size();
    b.dma(1, 1024, 64, 0x100, 1);
    CHECK_EQ(b.out.size(), at + 1024);
    for (unsigned k = 0; k < 1024; k++) {
        const unsigned word = 32 + k, packet = word / 32 + 1, w = word % 32;
        CHECK_EQ(b.out[at + k].lo, uint64_t(packet) << 32 | w);
    }
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// A threshold written while words wait: lower, the flag at once; higher, none; written while words are owed, nothing
// until they are read out (II: "then, and whenever the threshold is written, the stream is looked at again").
TEST(ingress_link_threshold_written_while_words_wait) {
    std::vector<uint8_t> file;
    adds(file, 1, 4, 32);
    Bench b(file, 1);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.store(W_BACKGROUND_THRESHOLD, 200);
    b.run(4 * 528 + 10);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 128u);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    b.store(W_BACKGROUND_THRESHOLD, 129);                   // still above: nothing
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    // lower: a store presented in p takes effect at the end of p, and its flag is seen by the loads of p + 1
    const int64_t p = b.t;
    b.store(W_BACKGROUND_THRESHOLD, 128);
    CHECK_EQ(b.t, p + 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    CHECK_EQ(b.ie().link_stream[1].owed, 128u);
    b.events(); b.tick();
    // written while 128 are owed: nothing, even at 1
    b.store(W_BACKGROUND_THRESHOLD, 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    b.dma(1, 100, 64, 0x100, 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    b.store(W_BACKGROUND_THRESHOLD, 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    // raised while owed, then the owed words out: 28 wait, below 50: nothing; lowered to 28: the flag
    b.store(W_BACKGROUND_THRESHOLD, 50);
    b.dma(1, 28, 64, 0x100, 1);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 0u);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    CHECK_EQ(b.load(P_ERRORS), 0u);
}

// One transfer of every word waiting, across several packets; then the next is at the threshold again.
TEST(ingress_link_one_transfer_across_packets) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 1, 3);
    add(file, 2, 2, 2, 32);
    add(file, 2, 3, 3, 7);
    add(file, 2, 4, 4, 1);
    Bench b(file);
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    b.store(W_BACKGROUND_THRESHOLD, 42);
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 43u);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
    b.events(); b.tick();
    b.dma(1, 42, 64, 0x200, 2);
    CHECK(b.sent_part(0, 1, 0, 3, 0x200, 64, 2));
    CHECK(b.sent_part(3, 2, 0, 32, 0x200 + 16 * 3, 64, 2));
    CHECK(b.sent(35, 3, 0, 7, 0x200 + 16 * 35, 64, 2));
    CHECK_EQ(b.load(W_BACKGROUND_WORDS), 1u);               // the fourth: below the threshold
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    b.store(W_BACKGROUND_THRESHOLD, 1);
    CHECK_EQ(b.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, EVENT_BACKGROUND_SERVICE);
}

// The two streams are independent, each with its own threshold.
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
    // the foreground's second packet asked when the first's two were out, and the same transfer then took it (C3)
    CHECK_EQ(b.events() & (EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE), EVENT_FOREGROUND_SERVICE);
    b.dma(1, 7, 64, 0x400, 2);
    CHECK(b.sent_part(3, 0xB1, 0, 4, 0x400, 64, 2));
    CHECK(b.sent(7, 0xB2, 0, 3, 0x400 + 64, 64, 2));
    CHECK_EQ(b.load(P_STATUS), 0u);
    // both at once, with different thresholds: a foreground at 1 and a background at 64, on one link
    std::vector<uint8_t> two;
    add(two, 2, 1, 0xB1, 32);                               // 528 bytes
    add(two, 1, 1, 0xF1, 1);                                // 32: to 560
    add(two, 2, 2, 0xB2, 32);                               // to 1088
    add(two, 1, 2, 0xF2, 1);                                // to 1120
    add(two, 2, 3, 0xB3, 1);                                // to 1152
    Bench c(two, 1);
    c.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE | EVENT_DMA_DONE);
    c.store(W_BACKGROUND_THRESHOLD, 64);
    CHECK_EQ(flag_seen(c, 0), int64_t(561));
    CHECK_EQ(c.load(P_EVENTS_PEEK) & EVENT_BACKGROUND_SERVICE, 0u);
    c.events(); c.tick();
    c.dma(0, 1, 64, 0x100, 1);
    CHECK_EQ(flag_seen(c, 1), int64_t(1089));               // 64 background words
    CHECK_EQ(c.load(W_BACKGROUND_WORDS), 64u);
    CHECK_EQ(c.load(W_FOREGROUND_WORDS), 0u);
    CHECK_EQ(flag_seen(c, 0), int64_t(1121));               // the foreground's second, its flag its own
    CHECK_EQ(c.load(W_BACKGROUND_WORDS), 64u);
    c.run(100);
    CHECK_EQ(c.load(W_BACKGROUND_WORDS), 65u);
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

DEATH_TEST(ingress_link_a_transfer_past_the_words_waiting_is_ub, "a DMA transfer of 3 words from the background stream, which holds 2") {
    std::vector<uint8_t> file;
    add(file, 2, 1, 0xA, 2);
    Bench b(file);
    b.run(3);
    b.dma(1, 3, 64, 0x100, 1);
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
