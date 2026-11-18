// standin_asic: an ASIC on v1.37's chain of processes made of WORKER8's Node alone (hostlink.h), to test the driver
// and the wire without a chip (the INTEGRATOR's stage 6). Started by wire-launch as a client:
//
//   wire-launch --clients N -- driver FRAMES ... -- standin_asic FRAMES [--asics A] [--segment S] [--rows-traced R]
//               [--delay CYCLES]
//
// It clocks its Node every cycle on the wire's two ends (the one below only if an ASIC follows), takes the host's
// words (every ASIC gets them all) and checks them against FRAMES byte for byte; once they are all in, and --delay
// cycles later, it pushes a reply made from gold's files (chain_reply.h: what the chip's would be in the words gold
// gives), three link packets. It finishes, as a chip's run does (tests/proof_bench.cpp: settled), once its Node has
// first sent towards the driver its own reply's link packets and those of every ASIC below it, three each, and a
// margin of cycles more. S defaults to the client number; A to 64; R to 64 x 65,536. `--silent K`: client K never
// replies (and then never finishes): the driver must fail at its limit.
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

int main(int argc, char** argv) {
    wire::WireArgs wa;
    char err[256];
    if (!wire::wire_parse_args(&argc, argv, &wa, err, sizeof err)) { std::printf("FAIL standin_asic: %s\n", err); return 2; }
    std::string path;
    unsigned asics = 64;
    int segment = -1;
    uint32_t rows = 64u * 65536u;
    uint64_t delay = 1000, margin = 4096;
    int silent = -1;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--asics" && more) asics = unsigned(std::atoi(argv[++i]));
        else if (a == "--segment" && more) segment = std::atoi(argv[++i]);
        else if (a == "--rows-traced" && more) rows = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--delay" && more) delay = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--silent" && more) silent = std::atoi(argv[++i]);
        else if (path.empty() && a[0] != '-') path = a;
        else { std::fprintf(stderr, "usage: standin_asic FRAMES [--asics A] [--segment S] [--rows-traced R] [--delay CYCLES] (with the wire's --client k)\n"); return 2; }
    }
    if (path.empty() || !wa.present || wa.driver) { std::fprintf(stderr, "standin_asic: a client on the wire, with a frame file\n"); return 2; }
    wire::Wire w;
    if (!wire::wire_attach(&w, wa, err, sizeof err)) { std::printf("FAIL standin_asic: %s\n", err); return 2; }
    const unsigned k = unsigned(wa.client), chips = w.clients(), B = w.lane_bytes();
    const unsigned s = segment >= 0 ? unsigned(segment) : k;
    frames::File f;
    std::string ferr;
    if (!f.read(path, ferr)) { std::printf("FAIL standin_asic %u: %s\n", k, ferr.c_str()); w.finish(); return 1; }

    wire::LinkEnd* up = w.up();
    wire::LinkEnd* down = w.down();
    ::hostlink::Node node(down != nullptr);
    uint8_t out_up[wire::kMaxLaneBytes] = {}, out_down[wire::kMaxLaneBytes] = {}, in_up[wire::kMaxLaneBytes], in_down[wire::kMaxLaneBytes] = {};
    const size_t words = f.bytes.size() / 16;
    size_t taken = 0, bad_at = ~size_t(0);
    uint64_t all_in_at = 0, sent_at = 0, t = 0;
    std::vector<::hostlink::Word> reply;
    size_t pushed = 0;
    const uint64_t want = 3ull * (chips - k);
    for (;; t++) {
        up->exchange(out_up, in_up);
        if (down) down->exchange(out_down, in_down);
        for (unsigned b = 0; b < B; b++) {
            const ::hostlink::Node::Bytes o = node.clock(in_up[b], in_down[b]);
            out_up[b] = o.up; out_down[b] = o.down;
        }
        ::hostlink::Word word;
        while (node.take(::hostlink::BACKGROUND, word)) {
            if (taken < words && bad_at == ~size_t(0) && std::memcmp(word.byte, &f.bytes[16 * taken], 16) != 0) bad_at = taken;
            if (++taken == words) all_in_at = t;
        }
        while (node.take(::hostlink::FOREGROUND, word)) {}
        if (taken == words && reply.empty() && t >= all_in_at + delay && int(k) != silent) reply = chain::make_reply(f, s, asics, rows, t);
        while (pushed < reply.size()) {                               // 32, 32 and 8 words, as Egress sends them
            const unsigned n = unsigned(reply.size() - pushed < 32 ? reply.size() - pushed : 32);
            if (!node.push(&reply[pushed], n)) break;
            pushed += n;
        }
        if (!sent_at && node.counts_above().sent_data >= want) sent_at = t;
        if (sent_at && t >= sent_at + margin) break;
    }
    w.finish();
    int bad = 0;
    if (taken != words) { std::printf("FAIL standin_asic %u: %zu of the frames' %zu words\n", k, taken, words); bad++; }
    if (bad_at != ~size_t(0)) { std::printf("FAIL standin_asic %u: the frames' word %zu came wrong\n", k, bad_at); bad++; }
    std::printf("%s standin_asic %u (segment %u): the frames' %zu words by cycle %llu, the reply pushed, %llu link packets passed up; %llu cycles\n",
                bad ? "FAIL" : "PASS", k, s, taken, (unsigned long long)all_in_at, (unsigned long long)node.counts_above().sent_data, (unsigned long long)t);
    return bad ? 1 : 0;
}
