// test_packet_vectors: Ingress's host side against gold's packetizer (v1.37). The reader (src/units/host_link.h) and
// the black box (ingress_egress.h) were written from ../v1.4_wip/planning/HOST-LINK.md and INGRESS-INTERFACE.md, not
// from gold's Java; gold's packetizer, its test vectors and its small packet files are WORKER2's
// (../updater_wip/pipeline/gold/test/packets/). The two are meant to check each other, and this is where they meet:
//   crc32c   every value of VECTORS.txt;
//   packet   every packet of VECTORS.txt, alone, with the number its stream expects: the reader's verdict (gold's
//            checks header-crc and data-checksum are ERRORS.crc_error; stream, length and reserved, format_error;
//            number, sequence_error), a good packet's fields and data, and the number expected after it; then the
//            same through the black box: the packet presented with its words, or its error bit;
//   file     every packet file: its packets, words and idle bytes by stream as VECTORS.txt counts them, its
//            background stream's data byte for byte the frame file; then through the black box, every packet read out
//            by the DMA;
//   damage   every file with one byte changed: the check that refuses it, at which packet, and where that packet's
//            header was taken to start; through the black box, its error bit, with the packets before it presented.
// Gold's frame-end and truncated are checks of whole files and frames, which Ingress does not know (a frame is the
// master tile's program's; a file that ends inside a packet is a packet that never arrives).
//
// THE FILES are found in $PACKET_VECTORS, or beside gold; where they are not (before WORKER2's work is on this
// branch), the tests print SKIPPED and pass.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "harness.h"
#include "ingress_bench.h"

using namespace units;
using namespace ingress_bench;

namespace {

struct PacketVector {
    std::string name, expect;
    uint32_t next_number = 1, next_after = 0;
    unsigned stream = 0, flags = 0, length = 0, reserved = 0;
    uint32_t number = 0, acknowledge = 0;
    unsigned hops = 0;
    std::vector<uint8_t> bytes, data;
};
struct FileVector {
    std::string name, frames_file, expect;
    std::map<std::string, uint64_t> count;
};
struct Damage { std::string file, expect; size_t byte = 0, at = 0; unsigned mask = 0, packet = 0; };
struct Vectors {
    bool present = false;
    std::string dir;
    std::vector<std::pair<std::vector<uint8_t>, uint32_t>> crc;
    std::vector<PacketVector> packets;
    std::vector<FileVector> files;
    std::vector<Damage> damage;
};

std::vector<uint8_t> hex(const std::string& s) {
    std::vector<uint8_t> out;
    if (s == "-") return out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(uint8_t(std::strtoul(s.substr(i, 2).c_str(), nullptr, 16)));
    return out;
}
bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

const Vectors& vectors() {
    static Vectors v;
    static bool read = false;
    if (read) return v;
    read = true;
    const char* env = std::getenv("PACKET_VECTORS");
    v.dir = env && *env ? env : "../updater_wip/pipeline/gold/test/packets";
    std::ifstream in(v.dir + "/VECTORS.txt");
    if (!in) {
        std::printf("test_packet_vectors: SKIPPED: no %s/VECTORS.txt (gold's packetizer, WORKER2's; PACKET_VECTORS names another folder)\n", v.dir.c_str());
        return v;
    }
    v.present = true;
    PacketVector* p = nullptr;
    FileVector* f = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream words(line);
        std::string key, a;
        if (!(words >> key)) continue;
        if (key == "end") { p = nullptr; f = nullptr; continue; }
        words >> a;
        const uint32_t number = uint32_t(std::strtoul(a.c_str(), nullptr, 0));
        if (key == "crc32c") { std::string value; words >> value; v.crc.push_back({hex(a), uint32_t(std::strtoul(value.c_str(), nullptr, 0))}); }
        else if (key == "packet") { v.packets.emplace_back(); p = &v.packets.back(); p->name = a; }
        else if (key == "file") { v.files.emplace_back(); f = &v.files.back(); f->name = a; }
        else if (key == "damage") {
            Damage d;
            d.file = a;
            std::string k, value;
            while (words >> k >> value) {
                if (k == "byte") d.byte = size_t(std::strtoul(value.c_str(), nullptr, 0));
                else if (k == "xor") d.mask = unsigned(std::strtoul(value.c_str(), nullptr, 0));
                else if (k == "expect") d.expect = value;
                else if (k == "packet") d.packet = unsigned(std::strtoul(value.c_str(), nullptr, 0));
                else if (k == "at") d.at = size_t(std::strtoul(value.c_str(), nullptr, 0));
            }
            v.damage.push_back(d);
        } else if (p) {
            if (key == "next-number") p->next_number = number;
            else if (key == "stream") p->stream = number;
            else if (key == "flags") p->flags = number;
            else if (key == "length") p->length = number;
            else if (key == "number") p->number = number;
            else if (key == "acknowledge") p->acknowledge = number;
            else if (key == "hops") p->hops = number;
            else if (key == "reserved") p->reserved = number;
            else if (key == "data") { const auto b = hex(a); p->data.insert(p->data.end(), b.begin(), b.end()); }
            else if (key == "bytes") { const auto b = hex(a); p->bytes.insert(p->bytes.end(), b.begin(), b.end()); }
            else if (key == "expect") p->expect = a;
            else if (key == "next-number-after") p->next_after = number;
        } else if (f) {
            if (key == "expect") f->expect = a;
            else if (key == "frames-file") f->frames_file = a;
            else if (key != "sha256") f->count[key] = number;
        }
    }
    return v;
}

// gold's check's name as Ingress's error.
hostlink::Check check_of(const std::string& expect) {
    if (expect == "ok") return hostlink::Check::OK;
    if (expect == "header-crc" || expect == "data-checksum") return hostlink::Check::CRC;
    if (expect == "stream" || expect == "length" || expect == "reserved") return hostlink::Check::FORMAT;
    if (expect == "number") return hostlink::Check::SEQUENCE;
    FATAL("test_packet_vectors: VECTORS.txt expects '%s', which is no check of a packet", expect.c_str());
}
uint32_t error_of(hostlink::Check c) {
    return c == hostlink::Check::CRC ? ERR_CRC : c == hostlink::Check::FORMAT ? ERR_FORMAT : c == hostlink::Check::SEQUENCE ? ERR_SEQUENCE : 0;
}
uint64_t half(const std::vector<uint8_t>& d, size_t at) { uint64_t v = 0; for (unsigned b = 0; b < 8; b++) v |= uint64_t(d[at + b]) << (8 * b); return v; }

// A file through the black box, every packet read out by the DMA as it asks: the data of each stream as it left, the
// packets that asked, and ERRORS at the end.
struct Played { std::vector<uint8_t> data[2]; unsigned asked[2] = {}; uint32_t errors = 0; uint64_t packets = 0; };
Played play(const std::vector<uint8_t>& file, uint32_t first_number = 1) {
    Played out;
    Bench b(file);
    b.ie().link.expect[0] = b.ie().link.expect[1] = first_number;
    b.mask(EVENT_FOREGROUND_SERVICE | EVENT_BACKGROUND_SERVICE);
    for (int quiet = 0; quiet < 8;) {
        b.tick();
        const uint32_t ev = b.events();
        bool any = false;
        for (unsigned q = 0; q < 2; q++) {
            if (!(ev & (EVENT_FOREGROUND_SERVICE << q))) continue;
            const uint32_t words = b.load(W_FOREGROUND_WORDS + q);
            const size_t from = b.out.size();
            b.tick();                                       // (the consuming load's cycle)
            b.dma(q, words, 64, 0x100, 1);
            for (size_t k = from; k < b.out.size(); k++) for (uint64_t h : {b.out[k].lo, b.out[k].hi}) for (unsigned i = 0; i < 8; i++) out.data[q].push_back(uint8_t(h >> (8 * i)));
            out.asked[q]++;
            any = true;
        }
        quiet = any ? 0 : quiet + 1;
    }
    out.errors = b.load(P_ERRORS);
    out.packets = b.ie().link_packets;
    return out;
}

}  // namespace

TEST(vectors_crc32c) {
    const Vectors& v = vectors();
    if (!v.present) return;
    CHECK(v.crc.size() >= 8);
    for (const auto& c : v.crc) CHECK_EQ(hostlink::crc32c(c.first.data(), c.first.size()), c.second);
}

TEST(vectors_packets_by_the_reader) {
    const Vectors& v = vectors();
    if (!v.present) return;
    CHECK(v.packets.size() >= 40);
    unsigned good = 0;
    for (const PacketVector& p : v.packets) {
        hostlink::Reader r;
        r.open(p.bytes.data(), p.bytes.size());
        r.expect[0] = r.expect[1] = p.next_number;
        hostlink::Packet got;
        hostlink::Check check;
        size_t taken;
        if (r.next(got, check, taken) != hostlink::Reader::Next::PACKET) { ::test::fail(__FILE__, __LINE__, "packet %s: no packet read", p.name.c_str()); return; }
        if (check != check_of(p.expect)) { ::test::fail(__FILE__, __LINE__, "packet %s: gold expects %s, the reader says %d", p.name.c_str(), p.expect.c_str(), int(check)); return; }
        if (check != hostlink::Check::OK) continue;
        good++;
        CHECK_EQ(taken, p.bytes.size());
        CHECK_EQ(got.stream, p.stream); CHECK_EQ(got.flags, p.flags); CHECK_EQ(got.length, p.length);
        CHECK_EQ(got.number, p.number); CHECK_EQ(got.acknowledge, p.acknowledge); CHECK_EQ(got.hops, p.hops);
        CHECK_EQ(p.data.size(), size_t(16) * p.length);
        for (unsigned w = 0; w < p.length; w++) { CHECK_EQ(got.lo[w], half(p.data, 16 * w)); CHECK_EQ(got.hi[w], half(p.data, 16 * w + 8)); }
        CHECK_EQ(r.expect[p.stream - 1], p.next_after);
    }
    CHECK(good >= 12);
}

// HL's three example headers ("The two checks"; owner, 2026-11-17), copied from the document: gold's vectors of them
// hold exactly those bytes, and the reader takes each (the black box does too: the test below plays every vector).
TEST(vectors_host_links_examples) {
    const Vectors& v = vectors();
    if (!v.present) return;
    const struct { const char* name; uint8_t header[16]; uint32_t number, acknowledge; unsigned hops; } ex[] = {
        {"host-link-example", {0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0xA0, 0x8C, 0x06, 0xE1}, 1, 0, 0},
        {"host-link-example-one-hop", {0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0x9E, 0xBC, 0x0A, 0x21}, 1, 0, 1},
        {"host-link-example-hops-3", {0x02, 0x01, 0xCD, 0xAB, 0x56, 0x55, 0x35, 0x00, 0xEA, 0x9A, 0x70, 0x42, 0xC9, 0xC4, 0x23, 0x30}, 0x2ABCD, 0x15555, 3},
    };
    for (const auto& e : ex) {
        const PacketVector* found = nullptr;
        for (const PacketVector& p : v.packets) if (p.name == e.name) found = &p;
        CHECK(found != nullptr);
        if (!found) continue;
        CHECK(found->bytes.size() == 32 && std::equal(e.header, e.header + 16, found->bytes.begin()));
        CHECK(found->expect == "ok");
        hostlink::Reader r;
        r.open(found->bytes.data(), found->bytes.size());
        r.expect[1] = e.number;
        hostlink::Packet got;
        hostlink::Check check;
        size_t taken;
        CHECK(r.next(got, check, taken) == hostlink::Reader::Next::PACKET);
        CHECK(check == hostlink::Check::OK);
        CHECK_EQ(got.number, e.number); CHECK_EQ(got.acknowledge, e.acknowledge); CHECK_EQ(got.hops, e.hops);
    }
}

TEST(vectors_packets_through_the_black_box) {
    const Vectors& v = vectors();
    if (!v.present) return;
    for (const PacketVector& p : v.packets) {
        const Played got = play(p.bytes, p.next_number);
        const hostlink::Check want = check_of(p.expect);
        if (got.errors != error_of(want)) { ::test::fail(__FILE__, __LINE__, "packet %s: gold expects %s, ERRORS is 0x%x", p.name.c_str(), p.expect.c_str(), got.errors); return; }
        if (want != hostlink::Check::OK) { CHECK_EQ(got.packets, 0u); CHECK_EQ(got.asked[0] + got.asked[1], 0u); continue; }
        CHECK_EQ(got.packets, 1u);
        const unsigned q = p.stream - 1;
        // a packet of length 0 asks for no service; one with words asks at least once: its words land a word a cycle
        // (17.6.3), and the player reads what has landed at each flag, so a transfer may end with words left (17.6.7)
        if (p.length > 0) CHECK(got.asked[q] >= 1u); else CHECK_EQ(got.asked[q], 0u);
        CHECK_EQ(got.asked[1 - q], 0u);
        CHECK(got.data[q] == p.data);
    }
}

TEST(vectors_files) {
    const Vectors& v = vectors();
    if (!v.present) return;
    CHECK(v.files.size() >= 5);
    for (const FileVector& f : v.files) {
        std::vector<uint8_t> bytes, frames;
        if (!read_file(v.dir + "/" + f.name, bytes)) { ::test::fail(__FILE__, __LINE__, "cannot read %s", f.name.c_str()); return; }
        CHECK(f.expect == "ok");
        CHECK_EQ(bytes.size(), f.count.at("bytes"));
        // by the reader: the counts
        hostlink::Reader r;
        r.open(bytes.data(), bytes.size());
        r.expect[0] = r.expect[1] = uint32_t(f.count.at("first-number"));
        uint64_t packets[2] = {}, words[2] = {}, empty = 0, idle = 0;
        std::vector<uint8_t> data[2];
        for (;;) {
            hostlink::Packet p;
            hostlink::Check check;
            size_t taken;
            const auto next = r.next(p, check, taken);
            if (next == hostlink::Reader::Next::END) { idle += taken; break; }
            CHECK(next == hostlink::Reader::Next::PACKET);
            if (check != hostlink::Check::OK) { ::test::fail(__FILE__, __LINE__, "%s: a packet at byte %zu is refused (%d)", f.name.c_str(), r.header_at, int(check)); return; }
            idle += taken - 16 - 16 * size_t(p.length);
            packets[p.stream - 1]++; words[p.stream - 1] += p.length; empty += p.length == 0;
            for (unsigned w = 0; w < p.length; w++) for (uint64_t h : {p.lo[w], p.hi[w]}) for (unsigned i = 0; i < 8; i++) data[p.stream - 1].push_back(uint8_t(h >> (8 * i)));
        }
        CHECK_EQ(packets[0] + packets[1], f.count.at("packets"));
        CHECK_EQ(packets[1], f.count.at("packets-stream-2"));
        CHECK_EQ(packets[0], f.count.at("packets-stream-1"));
        CHECK_EQ(empty, f.count.at("packets-length-0"));
        CHECK_EQ(idle, f.count.at("idle-bytes"));
        CHECK_EQ(words[1], f.count.at("words-stream-2"));
        CHECK_EQ(words[0], f.count.at("words-stream-1"));
        CHECK(read_file(v.dir + "/" + f.frames_file, frames));
        CHECK(data[1] == frames);                           // the background stream's data is the frame file
        // through the black box: every packet presented and read out
        const Played got = play(bytes, uint32_t(f.count.at("first-number")));
        CHECK_EQ(got.errors, 0u);
        CHECK_EQ(got.packets, f.count.at("packets"));
        CHECK(got.data[1] == frames);
        CHECK(got.data[0] == data[0]);
    }
}

TEST(vectors_damage) {
    const Vectors& v = vectors();
    if (!v.present) return;
    CHECK(v.damage.size() >= 30);
    for (const Damage& d : v.damage) {
        std::vector<uint8_t> bytes;
        if (!read_file(v.dir + "/" + d.file, bytes)) { ::test::fail(__FILE__, __LINE__, "cannot read %s", d.file.c_str()); return; }
        CHECK(d.byte < bytes.size());
        bytes[d.byte] ^= uint8_t(d.mask);
        hostlink::Reader r;
        r.open(bytes.data(), bytes.size());
        unsigned n = 0;
        hostlink::Check check = hostlink::Check::OK;
        for (;;) {
            hostlink::Packet p;
            size_t taken;
            if (r.next(p, check, taken) != hostlink::Reader::Next::PACKET) break;
            n++;
            if (check != hostlink::Check::OK) break;
        }
        if (check != check_of(d.expect) || n != d.packet || r.header_at != d.at) {
            ::test::fail(__FILE__, __LINE__, "%s with byte %zu xor 0x%x: gold expects %s at packet %u, byte %zu; the reader says %d at packet %u, byte %zu", d.file.c_str(),
                         d.byte, d.mask, d.expect.c_str(), d.packet, d.at, int(check), n, r.header_at);
            return;
        }
        const Played got = play(bytes);
        CHECK_EQ(got.errors, error_of(check));
        CHECK_EQ(got.packets, uint64_t(n - 1));             // the packets before it were presented
    }
}
