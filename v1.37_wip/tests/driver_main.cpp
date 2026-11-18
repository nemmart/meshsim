// driver: the host on v1.37's chain of processes (MILESTONE-v1.37.md, "The driver"; the INTEGRATOR's stage 6). The
// process at the head of the chain, started by wire-launch with --driver:
//
//   wire-launch --clients N -- driver FRAMES [--asics A] [--segments S0,S1,...] [--rows-traced R] [--limit CYCLES]
//               -- proof_run FRAMES ... (or standin_asic FRAMES ...)
//
//   1. reads gold's frame file and sends it through WORKER8's End (hostlink.h), each frame starting a new link packet
//      of at most 32 words, on the background stream, at the link's rate (the wire's lane: B bytes a cycle);
//   2. obeys the stop bits: the End does, holding back what the first ASIC cannot take;
//   3. takes what comes back sorted by hops (End::take_from), each ASIC's reply rebuilt;
//   4. checks each against gold's files in the words they give (chain_reply.h) and exits 0 only if every ASIC's has
//      come, whole and right, within --limit cycles (a count of cycles, never of seconds, so that the verdict is the
//      same every run; any limit in real time is the launcher's).
//
// A is the proof's N, its ASICs (64 by default); ASIC k runs segment Sk, k by default. R is the rows each traced (the
// segment's windows times its rows: 64 x 65,536 by default). The End's output byte of a cycle is a flip-flop: what it
// made in the last cycle, as a chip's ports are.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "chain_reply.h"
#include "frames.h"
#include "hostlink.h"
#include "wire.h"
#include "wire_args.h"

namespace {

struct Packet { unsigned n; ::hostlink::Word w[::hostlink::MAX_WORDS]; };

int usage() {
    std::fprintf(stderr, "usage: driver FRAMES [--asics A] [--segments S0,S1,...] [--rows-traced R] [--limit CYCLES] (with the wire's --driver)\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    wire::WireArgs wa;
    char err[256];
    if (!wire::wire_parse_args(&argc, argv, &wa, err, sizeof err)) { std::printf("FAIL driver: %s\n", err); return 2; }
    std::string path, segments;
    unsigned asics = 64;
    uint32_t rows = 64u * 65536u;
    uint64_t limit = uint64_t(1) << 32;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--asics" && more) asics = unsigned(std::atoi(argv[++i]));
        else if (a == "--segments" && more) segments = argv[++i];
        else if (a == "--rows-traced" && more) rows = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--limit" && more) limit = std::strtoull(argv[++i], nullptr, 10);
        else if (path.empty() && a[0] != '-') path = a;
        else return usage();
    }
    if (path.empty() || !wa.present || !wa.driver) return usage();
    wire::Wire w;
    if (!wire::wire_attach(&w, wa, err, sizeof err)) { std::printf("FAIL driver: %s\n", err); return 2; }
    const unsigned chips = w.clients(), B = w.lane_bytes();
    std::vector<unsigned> seg(chips);
    for (unsigned k = 0; k < chips; k++) seg[k] = k;
    if (!segments.empty()) {
        size_t at = 0;
        for (unsigned k = 0; k < chips && at <= segments.size(); k++) {
            seg[k] = unsigned(std::strtoul(segments.c_str() + at, nullptr, 10));
            const size_t comma = segments.find(',', at);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    frames::File f;
    std::string ferr;
    if (!f.read(path, ferr)) { std::printf("FAIL driver: %s\n", ferr.c_str()); w.finish(); return 1; }

    // the frames as link packets: each frame a new one, at most 32 words each
    std::vector<Packet> packets;
    for (const frames::Frame& fr : f.frame) {
        const size_t words = 1 + size_t(fr.length);
        for (size_t at = 0; at < words; at += ::hostlink::MAX_WORDS) {
            Packet p;
            p.n = unsigned(words - at < ::hostlink::MAX_WORDS ? words - at : ::hostlink::MAX_WORDS);
            for (unsigned k = 0; k < p.n; k++) std::memcpy(p.w[k].byte, &f.bytes[fr.at + 16 * (at + k)], 16);
            packets.push_back(p);
        }
    }

    ::hostlink::End end;
    wire::LinkEnd* down = w.down();
    std::vector<std::vector<::hostlink::Word>> got(chips);
    std::vector<std::vector<size_t>> ends(chips);
    std::vector<uint64_t> done_at(chips, 0);
    uint8_t out[wire::kMaxLaneBytes] = {}, in[wire::kMaxLaneBytes];
    size_t next = 0;
    uint64_t t = 0;
    unsigned complete = 0;
    for (; complete < chips && t < limit; t++) {
        while (next < packets.size() && end.room(::hostlink::BACKGROUND) >= packets[next].n) {
            if (!end.push(::hostlink::BACKGROUND, packets[next].w, packets[next].n)) { std::printf("FAIL driver: the End refused a link packet it had room for\n"); w.finish(); return 1; }
            next++;
        }
        down->exchange(out, in);
        for (unsigned k = 0; k < B; k++) out[k] = end.clock(in[k]);
        for (unsigned h = 0; h < chips; h++) {
            ::hostlink::Word word;
            bool last = false;
            while (end.take_from(h, word, &last)) {
                got[h].push_back(word);
                if (last) ends[h].push_back(got[h].size());
                if (got[h].size() == chain::REPLY_WORDS && last) { done_at[h] = t; complete++; }
            }
        }
    }
    int bad = 0;
    if (next < packets.size()) { std::printf("FAIL driver: %zu of %zu link packets sent by cycle %llu\n", next, packets.size(), (unsigned long long)t); bad++; }
    for (unsigned h = 0; h < chips; h++) {
        const std::string who = "driver: ASIC " + std::to_string(h) + "'s reply (segment " + std::to_string(seg[h]) + ")";
        if (!done_at[h]) { std::printf("FAIL %s: not come in %llu cycles (%zu words)\n", who.c_str(), (unsigned long long)t, got[h].size()); bad++; continue; }
        const int b = chain::check_reply(who, f, seg[h], asics, rows, got[h], ends[h]);
        bad += b;
        if (!b) std::printf("driver: ASIC %u's reply (segment %u): 72 words in three link packets at cycle %llu, as gold's files give\n", h, seg[h], (unsigned long long)done_at[h]);
    }
    {   // the wire loses nothing: a damaged link packet, or one ahead of its number, is a fault of an end
        const ::hostlink::LinkCounts c = end.counts();
        const uint64_t damaged = c.bad_header + c.bad_data + c.bad_fields + c.ahead;
        if (damaged) { std::printf("FAIL driver: %llu damaged link packets, or ahead, on a wire that loses nothing\n", (unsigned long long)damaged); bad++; }
    }
    if (end.waiting(::hostlink::FOREGROUND) != 0) { std::printf("FAIL driver: %u words from an ASIC not on the chain\n", end.waiting(::hostlink::FOREGROUND)); bad++; }
    const ::hostlink::LinkCounts n = end.counts();
    std::printf("driver: %zu link packets sent (%llu again), %llu taken; the wire's waits %llu (%.3f s); %llu cycles\n", packets.size(),
                (unsigned long long)n.sent_again, (unsigned long long)n.taken, (unsigned long long)w.stats().waits, double(w.stats().wait_ns) * 1e-9,
                (unsigned long long)t);
    w.finish();
    std::printf("%s driver: %u ASICs, %d checks failed\n", bad ? "FAIL" : "PASS", chips, bad);
    return bad ? 1 : 0;
}
