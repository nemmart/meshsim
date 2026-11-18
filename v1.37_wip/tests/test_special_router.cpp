// test_special_router.cpp: the router's rules of v1.36's chip (machine-spec 16; the RTL's rtl/tile/base/spec.md, chapter
// 16), each from the specification or, where it is silent, from the RTL's router (rtl/blocks/router/README.md,
// src/router.veryl, src/router_output.veryl). No expectation here comes from the simulator's output.
//   16.1  a hop of four cycles unloaded: a packet at the local input in cycle t that crosses h links, attach links
//         included, is ejected in cycle t + 4h + 2 (the RTL's README, "Links and timing");
//   16.2  a special tile's router: a packet for the tile ejects, every other, from local or a DMA, leaves by its attach
//         port; its broadcast spreads from its attach tile as that tile's own; in the mesh a special tile is routed as its
//         attach tile (0, 7 and 15 at 8 x 8) and a packet for it leaves that tile by its attach port; a packet for a
//         reserved tile id is dropped where it enters, with no error;
//   16.3  the arbiters: round-robin from a pointer that is 0 at the reset, the queues in their inputs' order (local,
//         west, east, north, south, then the attach ports); the loopback queue served only when every other eject queue
//         is empty, the pointer staying; a queue full by its count at the start of the cycle, its push lost and
//         counted; an attach output one packet a cycle;
//   16.4  a special tile's peripheral DMA: an input of its own, the tile's id as src, a packet moved only while the
//         queue it enters has room, and its packets to its own tile at the eject like any arrival, with no token.
// These are of v1.36's RTL as built (the special tiles with no line, a DMA on west). v1.37's line between the special
// tiles (16.1 to 16.4: the line, its routes, its regime, the DMA on south) is the NoC rig with `line` (run_noc's), and
// its tests are the last ones here (the INTEGRATOR's stage 2).
//
// THE RIG. A stand plays everything around a router from a script: the injection point, the four links' last register
// stages, the attach link, a special tile's DMA; and it records every packet the router sends, with its cycle. Every
// wire between the stand and the router is one register, so with cycles counted from 1:
//   - an event's `at` is the cycle its packet is AT THE ROUTER'S INPUT (the stand puts it the cycle before);
//   - a packet is recorded in the cycle it is IN THE OUTPUT'S REGISTER (the RTL's `*_out_valid`, `eject_valid`): two
//     cycles after an unloaded packet was at an input (router.veryl's header: "leaves its output in cycle t + 2").
//   - the DMA's packet is offered from its `at` on until the router takes it (the `packet` interface's valid and ready).
// The second rig is the NoC: the 64 compute tiles' routers and the three special tiles', every link three register
// stages (16.1), a stand at each router's local side.
// A token: the simulator marks a packet that holds a read token (lw::TAG_TOKEN) and the router tells the inject unit
// when one leaves the loopback queue, which is the RTL's `eject_loopback` mark (16.3); the stand records it as TOKEN.
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <tuple>
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/router.h"

using namespace units;

namespace {

// The router's ports in the arbiters' order (16.3); DMA a special tile's peripheral's, TOKEN the loopback mark.
enum Port : unsigned { LOCAL, WEST, EAST, NORTH, SOUTH, ATTACH, DMA, TOKEN };
const char* const kPort[] = {"local", "west", "east", "north", "south", "attach", "dma", "token"};

constexpr unsigned MASTER = 64, MEMORY = 65, TRACER = 66;                 // 2.1
constexpr unsigned kAttachTile[3] = {0, 7, 15};                           // 2.1, 16.1: at 8 x 8
constexpr unsigned ROUTER_QUEUE_DEPTH = 16;                               // 16.3

struct Ev { uint64_t at; unsigned tile, port; uint64_t id, hdr; };        // a packet at input `port` of `tile`'s router
struct Seen {
    uint64_t at; unsigned tile, port; uint64_t id, hdr, hi;               // a packet in output `port`'s register
    bool operator==(const Seen&) const = default;
    auto key() const { return std::tie(at, tile, port, id); }
};

// A header: a WRITE of 128 bits with a counter and an address, so that every field has bits to lose.
uint64_t hdr(unsigned src, unsigned dst, uint64_t tags = 0) { return lw::hdr(src, dst, lw::M_WRITE, 5, 0x03A5F0C4u, lw::SIZE_128) | tags; }
// What event `e` looks like in output `port`'s register of `tile` in cycle `at`, its src `src`.
Seen seen(uint64_t at, unsigned tile, unsigned port, const Ev& e, unsigned src) { return {at, tile, port, e.id, lw::with_src(e.hdr, src), ~e.id}; }
Seen token(uint64_t at, unsigned tile) { return {at, tile, TOKEN, 0, 0, 0}; }

struct Stand {
    sim::LinkOut to_local, to_link[4], to_attach;
    sim::LinkOut to_dma;                                     // the DMA's packet, in the cycle it moves (router.h, A DMA INPUT)
    sim::WireIn dma_room;
    sim::LinkIn from_local, from_link[4], from_attach, from_token;
    sim::CountIn ovf;
    unsigned tile;
    bool links, attach, dma;
    unsigned dma_in = 0;                                     // the DMA's input: 0 west (v1.36's RTL), 3 south (v1.37, 16.4)
    bool line = false;                                       // a special tile's router with the line (16.1): `links` the line's
                                                             //   west and east alone, where the tile has them
    std::vector<Ev> script, dma_script;
    size_t next = 0, dma_next = 0;
    std::vector<Seen> out;
    uint64_t overflows = 0, first_overflow = 0;              // the router's overflow count through the cycle before, and the
                                                             // first cycle that is not 0
    std::vector<uint64_t> lost_in;                           // each cycle the router lost a packet in (its overflow pulse)
    bool has_link(unsigned i) const { return links && (!line || (i == 0 && tile > MASTER) || (i == 1 && tile < TRACER)); }
    Stand(sim::Ports& p, unsigned k, bool with_links, bool with_attach, bool with_dma, const std::vector<Ev>& all, bool with_line = false)
        : tile(k), links(with_links), attach(with_attach), dma(with_dma), line(with_line) {
        if (line) dma_in = 3;
        p.out(to_local, "to-local", 3); p.in(from_local, "eject", 3); p.in(from_token, "token", 0); p.in(ovf, "ovf");
        if (links) for (unsigned i = 0; i < 4; i++) if (has_link(i)) { p.out(to_link[i], "to-link", 3); p.in(from_link[i], "from-link", 3); }
        if (attach) { p.out(to_attach, "to-attach", 3); p.in(from_attach, "from-attach", 3); }
        if (dma) { p.out(to_dma, "to-dma", 3); p.in(dma_room, "dma-room"); }
        for (const Ev& e : all) if (e.tile == tile) (e.port == DMA ? dma_script : script).push_back(e);
        const auto by_cycle = [](const Ev& a, const Ev& b) { return a.at < b.at; };
        std::stable_sort(script.begin(), script.end(), by_cycle);
        std::stable_sort(dma_script.begin(), dma_script.end(), by_cycle);
        for (const Ev& e : all)
            if (e.tile == tile && (e.at < 2 || (e.port == DMA && !dma) || (e.port == ATTACH && !attach) || (e.port >= WEST && e.port <= SOUTH && !has_link(e.port - WEST)))) {
                std::fprintf(stderr, "stand %u: no such event: cycle %" PRIu64 " port %s\n", tile, e.at, kPort[e.port]);
                std::abort();
            }
    }
    void get(sim::Ctx& c, sim::LinkIn p, unsigned port) {
        if (!c.ready(p)) return;
        const sim::Word* w = c.peek(p);
        out.push_back({c.now(), tile, port, w[0], w[1], w[2]});
        c.take(p);
    }
    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        get(c, from_local, LOCAL);
        if (links) for (unsigned i = 0; i < 4; i++) if (has_link(i)) get(c, from_link[i], WEST + i);
        if (attach) get(c, from_attach, ATTACH);
        if (c.ready(from_token)) { out.push_back(token(t, tile)); c.take(from_token); }
        // a packet lost in a cycle is in the count the cycle after (the stand reads it as at the end of the cycle before)
        const uint64_t through = c.through_last(ovf);
        if (through != overflows) lost_in.push_back(t - 1);
        overflows = through;
        if (overflows && !first_overflow) first_overflow = t;
        for (; next < script.size() && script[next].at == t + 1; next++) {
            const Ev& e = script[next];
            const sim::LinkOut p = e.port == LOCAL ? to_local : e.port == ATTACH ? to_attach : to_link[e.port - WEST];
            c.put(p, e.id, e.hdr, ~e.id);
        }
        // the DMA's packet register: the oldest packet not yet taken, offered from its cycle on (16.4); it moves in a
        // cycle the router is ready for it, and is in its queue from the next
        if (dma && dma_next < dma_script.size() && dma_script[dma_next].at <= t &&
            Router::dma_ready(c.read(dma_room), dma_in, tile, lw::dst(dma_script[dma_next].hdr))) {
            const Ev& e = dma_script[dma_next++];
            c.put(to_dma, e.id, e.hdr, ~e.id);
        }
        return c.next();
    }
    uint64_t digest() const { return next + dma_next; }
};

// The stand's wires to its router: one register each. With `links`, the four links and the attach port as well.
void wire(sim::Config& cfg, Stand& st, Router& r) {
    cfg.link(st.to_local, r.inj, 1);
    cfg.link(r.eject, st.from_local, 1);
    cfg.link(r.loop_left, st.from_token, 1);
    cfg.connect(r.ovf, st.ovf);
    if (st.dma) { cfg.link(st.to_dma, r.in[st.dma_in == 3 ? Router::FROM_DOWN : Router::FROM_LEFT], 1); cfg.connect(r.dma_room, st.dma_room); }
    if (st.links) {                                          // west, east, north, south (router.h's numbering)
        static const unsigned in_of[4] = {Router::FROM_LEFT, Router::FROM_RIGHT, Router::FROM_UP, Router::FROM_DOWN};
        static const unsigned out_of[4] = {Router::TO_LEFT, Router::TO_RIGHT, Router::TO_UP, Router::TO_DOWN};
        for (unsigned i = 0; i < 4; i++)
            if (st.has_link(i)) { cfg.link(st.to_link[i], r.in[in_of[i]], 1); cfg.link(r.out[out_of[i]], st.from_link[i], 1); }
    }
    if (st.attach) { cfg.link(st.to_attach, r.from_cc, 1); cfg.link(r.to_cc, st.from_attach, 1); }
}

struct Result {
    std::vector<Seen> out;
    uint64_t overflows = 0, first_overflow = 0;
    unsigned attached = 0;
    std::vector<std::pair<uint64_t, unsigned>> lost;          // (cycle, tile) of each cycle a router lost a packet in (the NoC)
    uint64_t route_errors = 0;                                 // run_router: the router's routes to an output no queue feeds
};

void sort(std::vector<Seen>& v) { std::sort(v.begin(), v.end(), [](const Seen& a, const Seen& b) { return a.key() < b.key(); }); }

// One router of `tile` (a special tile's with a DMA) and its stand, run for `cycles`.
// With `line` (v1.37, 16.1; a special tile): the router with the line, the stand on its west and east line links, the
// DMA on south (16.4).
Result run_router(unsigned tile, const std::vector<Ev>& script, uint64_t cycles, bool line = false) {
    const Hops h = load_hops("data/hops.txt");
    const NetConfig nc;
    sim::Config cfg;
    const bool special = tile >= MASTER;
    auto& r = cfg.add<Router>("router", tile, nc, &h, false, Router::FIFTH_LINK, special ? (line ? 8u : 1u) : 0u, line);   // a DMA on west, or south with the line
    auto& st = cfg.add<Stand>("stand", tile, !special || line, special || r.attached, special, script, line);
    wire(cfg, st, r);
    sim::Sim m = cfg.build();
    m.run(cycles);
    const Stand& s = m.unit<Stand>("stand");
    Result res{s.out, s.overflows, s.first_overflow, 0, {}, m.unit<Router>("router").route_errors};
    sort(res.out);
    return res;
}

// The NoC (16.1): east and west one ring through every tile in id order, south and north the columns, which wrap;
// every link three register stages; each special tile's attach port joined to its attach tile's.
// With `line` (v1.37, 16.1): the special tiles' routers with the line, the master tile's east to the memory tile's west
// and the memory tile's east to the tracer tile's west, each way a link of three stages; their DMAs on south (16.4).
Result run_noc(const std::vector<Ev>& script, uint64_t cycles, bool line = false) {
    const Hops h = load_hops("data/hops.txt");
    const NetConfig nc;
    sim::Config cfg;
    Router* r[67];
    const auto name = [](const char* what, unsigned k) { return what + std::to_string(k); };
    for (unsigned k = 0; k < 67; k++) {
        r[k] = &cfg.add<Router>(name("router", k), k, nc, &h, false, Router::FIFTH_LINK, k >= MASTER ? (line ? 8u : 1u) : 0u, line && k >= MASTER);
        Stand& st = cfg.add<Stand>(name("stand", k), k, false, false, k >= MASTER, script);
        if (line) st.dma_in = 3;
        wire(cfg, st, *r[k]);
    }
    if (line)
        for (unsigned k = MASTER; k < TRACER; k++) {
            cfg.link(r[k]->out[Router::TO_RIGHT], r[k + 1]->in[Router::FROM_LEFT], 3);
            cfg.link(r[k + 1]->out[Router::TO_LEFT], r[k]->in[Router::FROM_RIGHT], 3);
        }
    for (unsigned k = 0; k < 64; k++) {
        cfg.link(r[k]->out[Router::TO_RIGHT], r[(k + 1) % 64]->in[Router::FROM_LEFT], 3);
        cfg.link(r[k]->out[Router::TO_LEFT], r[(k + 63) % 64]->in[Router::FROM_RIGHT], 3);
        cfg.link(r[k]->out[Router::TO_DOWN], r[(k + 8) % 64]->in[Router::FROM_UP], 3);
        cfg.link(r[k]->out[Router::TO_UP], r[(k + 56) % 64]->in[Router::FROM_DOWN], 3);
    }
    for (unsigned s = 0; s < 3; s++) {
        cfg.link(r[kAttachTile[s]]->to_cc, r[MASTER + s]->from_cc, 3);
        cfg.link(r[MASTER + s]->to_cc, r[kAttachTile[s]]->from_cc, 3);
    }
    sim::Sim m = cfg.build();
    m.run(cycles);
    Result res;
    for (unsigned k = 0; k < 67; k++) {
        const Stand& s = m.unit<Stand>(name("stand", k));
        res.out.insert(res.out.end(), s.out.begin(), s.out.end());
        res.overflows += s.overflows;
        for (uint64_t at : s.lost_in) res.lost.push_back({at, k});
        if (k < 64 && m.unit<Router>(name("router", k)).attached) res.attached++;
    }
    sort(res.out);
    return res;
}

// Whether the router sent exactly `want`; the first difference is printed.
bool same(const std::vector<Seen>& got, std::vector<Seen> want) {
    sort(want);
    if (got == want) return true;
    const auto show = [](const char* who, const std::vector<Seen>& v, size_t i) {
        if (i >= v.size()) { std::fprintf(stderr, "  %s: nothing more\n", who); return; }
        std::fprintf(stderr, "  %s: cycle %" PRIu64 " tile %u %s: id %" PRIu64 " header %" PRIx64 " (src %u dst %u) high %" PRIx64 "\n", who, v[i].at,
                     v[i].tile, kPort[v[i].port], v[i].id, v[i].hdr, lw::src(v[i].hdr), lw::dst(v[i].hdr), v[i].hi);
    };
    size_t i = 0;
    while (i < got.size() && i < want.size() && got[i] == want[i]) i++;
    std::fprintf(stderr, "output %zu differs (%zu sent, %zu wanted)\n", i, got.size(), want.size());
    show("wanted", want, i);
    show("sent  ", got, i);
    return false;
}

// ---- the specification's figures, written here ------------------------------------------------------------------------

unsigned mesh_tile(unsigned id) { return id >= MASTER ? kAttachTile[id - MASTER] : id; }   // 16.2: routed as its attach tile

// 16.2, the offset: (a, b) with 8a + b = dst - src modulo 64, |a| and |b| at most 4, |a| + |b| least: its hops.
unsigned unicast_hops(unsigned src, unsigned dst) {
    unsigned best = 99;
    for (int a = -4; a <= 4; a++)
        for (int b = -4; b <= 4; b++)
            if ((8 * a + b - int(dst) + int(src)) % 64 == 0) best = std::min(best, unsigned(std::abs(a) + std::abs(b)));
    return best;
}
// The links a unicast from id `src` to id `dst` crosses: the mesh's hops and each special tile's attach link.
unsigned unicast_links(unsigned src, unsigned dst) {
    if (src == dst) return 0;
    return unicast_hops(mesh_tile(src), mesh_tile(dst)) + (src >= MASTER) + (dst >= MASTER);
}
// 16.2, broadcast: the hops of the copy that compute tile `k` ejects, from source tile `s`. Along the ring, east as far
// as 4 columns and west as far as 3; then down or up the column from the ring's tile, which is in the source's row or,
// past a row's end, the row below or above it, to a row at most 4 below the source's or 3 above it.
unsigned broadcast_hops(unsigned s, unsigned k) {
    const auto reach = [](unsigned d) { return d <= 4 ? int(d) : int(d) - 8; };   // a distance around: 4 one way, 3 the other
    const int ring = reach((k % 8 - s % 8) & 7);
    const unsigned ring_tile = unsigned(int(s) + ring) & 63;
    const int ring_row = reach((ring_tile / 8 - s / 8) & 7), row = reach((k / 8 - s / 8) & 7);
    return unsigned(std::abs(ring) + std::abs(row - ring_row));
}

// 16.3, one output under load: its queues in their inputs' order, each of 16 packets; the arbiter takes the first
// non-empty queue at or after its pointer and moves the pointer one past it; a queue is full by its count at the start
// of the cycle, even if its head leaves in it (router_output.veryl), and then a link's packet is lost and a DMA's
// waits at its input (16.4). `feed[i]` are queue i's arrivals; the packet taken in cycle c is in the register in c + 1.
struct Feed { bool dma; std::vector<Ev> in; };
struct Load { std::vector<std::pair<uint64_t, Ev>> sent; std::vector<Ev> lost; };
Load one_output(const std::vector<Feed>& feed, uint64_t from, uint64_t to) {
    const size_t n = feed.size();
    std::vector<std::vector<Ev>> queue(n);
    std::vector<size_t> head(n, 0), next(n, 0);
    size_t pointer = 0;
    Load l;
    for (uint64_t c = from; c <= to; c++) {
        std::vector<size_t> count(n);
        for (size_t i = 0; i < n; i++) count[i] = queue[i].size() - head[i];
        for (size_t i = 0; i < n; i++) {
            const size_t q = (pointer + i) % n;
            if (!count[q]) continue;
            l.sent.push_back({c + 1, queue[q][head[q]++]});
            pointer = (q + 1) % n;
            break;
        }
        for (size_t i = 0; i < n; i++) {
            if (next[i] == feed[i].in.size()) continue;
            const Ev& e = feed[i].in[next[i]];
            if (feed[i].dma ? e.at > c : e.at != c) continue;
            if (count[i] < ROUTER_QUEUE_DEPTH) { queue[i].push_back(e); next[i]++; }
            else if (!feed[i].dma) { l.lost.push_back(e); next[i]++; }
        }
    }
    return l;
}

uint64_t g_id = 0;                                           // every packet's payload: its number
Ev ev(uint64_t at, unsigned tile, unsigned port, unsigned src, unsigned dst, uint64_t tags = 0) { return {at, tile, port, ++g_id, hdr(src, dst, tags)}; }
constexpr unsigned ANY = 99;                                 // a src the router is to replace: a reserved id

}  // namespace

// 2.1, 16.1: the attach tiles are the master tile's tile 0, the memory tile's tile MESH_SIZE - 1 and the tracer
// tile's tile 2 MESH_SIZE - 1: "one each on tiles 0, 7 and 15" at 8 x 8. The network's configuration, which every
// router here is built from (the rigs below stop on a router whose attach port is not where the test wires it).
TEST(attach_tiles_are_0_7_and_15) {
    const NetConfig nc;
    for (unsigned k = 0; k < 64; k++) CHECK_EQ(nc.attached_special(k), k == 0 ? 0 : k == 7 ? 1 : k == 15 ? 2 : -1);
    for (unsigned n = 0; n < 3; n++) CHECK_EQ(lw::tile_of(MASTER + n), kAttachTile[n]);
}

// 16.2, "Inside a special tile's router": a packet for the special tile ejects, and every other packet, from its local
// input or a DMA, leaves by its attach port, a broadcast included (it spreads from the attach tile). 14.3, 16.4: the
// router stamps src on the local input's packets and on a DMA's, with the special tile's id. An arrival on the attach
// port keeps its src.
TEST(special_router_ejects_its_own_and_sends_every_other_by_its_attach_port) {
    for (unsigned me = MASTER; me <= TRACER; me++) {
        const unsigned other = me == TRACER ? MASTER : me + 1;
        std::vector<Ev> s;
        std::vector<Seen> want;
        uint64_t t = 10;
        for (unsigned port : {unsigned(LOCAL), unsigned(DMA)})
            for (unsigned dst : {me, 0u, 5u, 63u, other, unsigned(lw::BROADCAST)}) {
                s.push_back(ev(t, me, port, ANY, dst));
                want.push_back(seen(t + 2, me, dst == me ? LOCAL : ATTACH, s.back(), me));
                t += 10;
            }
        s.push_back(ev(t, me, ATTACH, 3, me));               // from compute tile 3, through the attach tile
        want.push_back(seen(t + 2, me, LOCAL, s.back(), 3));
        const Result r = run_router(me, s, t + 20);
        CHECK(same(r.out, want));
        CHECK_EQ(r.overflows, 0u);
    }
}

// The specification names the packets a special tile's router sends on: those from its local input or a DMA. One
// arriving on its attach port that is not for the tile goes nowhere (router.veryl, `route`: on a special tile, dst !=
// me leaves by the attach port only from a port below the attach ports), a broadcast included: it reaches no special
// tile (16.2).
TEST(special_router_drops_an_attach_arrival_for_another_tile) {
    for (unsigned me = MASTER; me <= TRACER; me++) {
        const unsigned other = me == MASTER ? TRACER : me - 1;
        const std::vector<Ev> s = {ev(10, me, ATTACH, 3, 5), ev(11, me, ATTACH, 3, lw::BROADCAST), ev(12, me, ATTACH, 3, other),
                                   ev(13, me, ATTACH, 3, me)};
        const Result r = run_router(me, s, 40);
        CHECK(same(r.out, {seen(15, me, LOCAL, s[3], 3)}));
        CHECK_EQ(r.overflows, 0u);
    }
}

// 16.2 with the line (v1.37): an attach port's arrival for another special tile still goes nowhere: only packets from
// local or the DMA take the line, so a compute tile's packet never does (16.2).
TEST(line_special_router_drops_an_attach_arrival_for_another_tile) {
    for (unsigned me = MASTER; me <= TRACER; me++) {
        const unsigned other = me == MASTER ? MEMORY : me - 1, far = me == TRACER ? MASTER : TRACER;
        const std::vector<Ev> s = {ev(10, me, ATTACH, 3, 5), ev(11, me, ATTACH, 3, lw::BROADCAST), ev(12, me, ATTACH, 3, other),
                                   ev(13, me, ATTACH, 3, far), ev(14, me, ATTACH, 3, me)};
        const Result r = run_router(me, s, 40, true);
        CHECK(same(r.out, {seen(16, me, LOCAL, s[4], 3)}));
        CHECK_EQ(r.overflows, 0u);
        CHECK_EQ(r.route_errors, 0u);                        // routed nowhere, not to an output no queue from the attach port feeds
    }
}

// 16.4: a DMA's packets to its own tile "reach the eject like any arrival, and take no read token". Like any arrival:
// the DMA's queue at the eject is one of the arbiter's, before the attach port's in the inputs' order (the DMA is on a
// link input: router.veryl's INPUTS, router_output.veryl's order), and the loopback queue waits for it (16.3). No
// token: only a packet from the loopback queue is marked.
TEST(special_dma_packets_for_its_own_tile_are_arrivals_at_the_eject) {
    for (unsigned me = MASTER; me <= TRACER; me++) {
        std::vector<Ev> s;
        std::vector<Seen> want;
        // the three at once, twice: the DMA's, the attach port's, then the tile's own with its token. After the attach
        // port's the pointer is past every queue, so the second time is the first's order.
        for (uint64_t t : {10u, 30u}) {
            s.push_back(ev(t, me, LOCAL, ANY, me, lw::TAG_TOKEN));
            s.push_back(ev(t, me, DMA, ANY, me, lw::TAG_TOKEN));    // the mark is the queue's, whatever the packet holds
            s.push_back(ev(t, me, ATTACH, 7, me, lw::TAG_TOKEN));
            const size_t i = s.size() - 3;
            want.push_back(seen(t + 2, me, LOCAL, s[i + 1], me));
            want.push_back(seen(t + 3, me, LOCAL, s[i + 2], 7));
            want.push_back(seen(t + 4, me, LOCAL, s[i], me));
            want.push_back(token(t + 4, me));
        }
        // the DMA's alone moves the pointer past it: then the attach port's is first
        s.push_back(ev(50, me, DMA, ANY, me));
        want.push_back(seen(52, me, LOCAL, s.back(), me));
        s.push_back(ev(60, me, DMA, ANY, me));
        want.push_back(seen(63, me, LOCAL, s.back(), me));
        s.push_back(ev(60, me, ATTACH, 7, me));
        want.push_back(seen(62, me, LOCAL, s.back(), 7));
        // the tile's own packet waits while the DMA's keep coming
        s.push_back(ev(80, me, LOCAL, ANY, me, lw::TAG_TOKEN));
        want.push_back(seen(87, me, LOCAL, s.back(), me));
        want.push_back(token(87, me));
        for (uint64_t t = 80; t < 85; t++) {
            s.push_back(ev(t, me, DMA, ANY, me));
            want.push_back(seen(t + 2, me, LOCAL, s.back(), me));
        }
        const Result r = run_router(me, s, 120);
        CHECK(same(r.out, want));
        CHECK_EQ(r.overflows, 0u);
    }
}

// 16.4, local flow control: "a DMA sends a packet only while the queue that packet enters has room". The tile's own
// packets and its DMA's, one of each a cycle for 48 cycles, all for the mesh: the attach output takes one a cycle from
// the two queues in turn (16.3), so each fills at half a packet a cycle. The local input has no flow control and loses
// packets from its 32nd on, each an overflow (16.3); the DMA loses none: its packet waits at its input.
TEST(special_dma_sends_only_while_its_queue_has_room) {
    const unsigned me = MEMORY;
    std::vector<Ev> s;
    Feed local{false, {}}, dma{true, {}};
    for (uint64_t t = 10; t < 58; t++) {
        local.in.push_back(ev(t, me, LOCAL, ANY, 5));
        dma.in.push_back(ev(t, me, DMA, ANY, 5));
    }
    s = local.in;
    s.insert(s.end(), dma.in.begin(), dma.in.end());
    const Load l = one_output({local, dma}, 10, 200);
    // by hand: served first, the local queue holds ceil(k / 2) packets k cycles after the first, so 16 at k = 31
    CHECK_EQ(l.lost.size(), 9u);
    CHECK_EQ(l.lost[0].id, local.in[31].id);
    CHECK_EQ(l.sent.size(), 48u + 39u);
    std::vector<Seen> want;
    for (const auto& [at, e] : l.sent) want.push_back(seen(at, me, ATTACH, e, me));
    const Result r = run_router(me, s, 200);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 9u);
    unsigned dmas = 0;
    for (const Seen& o : r.out) if (o.id == dma.in[dmas].id) dmas++;     // every one of the DMA's, in order
    CHECK_EQ(dmas, 48u);
}

// 16.2: "a packet for a reserved tile id is dropped where it enters the NoC, with no error". The reserved ids are those
// above the tracer tile's and below broadcast (4.1): 67 to 125 at 8 x 8. The router drops one at any input, and one for
// none likewise (the RTL's README: an encoding the specification does not define). The ids beside them are routed.
TEST(packet_for_a_reserved_tile_id_is_dropped_where_it_enters) {
    {
        std::vector<Ev> s;
        uint64_t t = 10;
        for (unsigned dst : {67u, 68u, 100u, 125u, unsigned(lw::NONE)})
            for (unsigned port : {unsigned(LOCAL), unsigned(WEST), unsigned(EAST), unsigned(NORTH), unsigned(SOUTH)}) s.push_back(ev(t++, 5, port, 4, dst));
        // 126 is broadcast: tile 5's own is ejected and leaves by every link. 66 is the tracer tile, at tile 15:
        // offset (1, 2) from tile 5, east first.
        s.push_back(ev(60, 5, LOCAL, ANY, lw::BROADCAST));
        s.push_back(ev(70, 5, LOCAL, ANY, TRACER));
        const size_t b = s.size() - 2;
        std::vector<Seen> want = {seen(72, 5, EAST, s[b + 1], 5)};
        for (unsigned port : {unsigned(LOCAL), unsigned(WEST), unsigned(EAST), unsigned(NORTH), unsigned(SOUTH)}) want.push_back(seen(62, 5, port, s[b], 5));
        const Result r = run_router(5, s, 100);
        CHECK(same(r.out, want));
        CHECK_EQ(r.overflows, 0u);
    }
    for (unsigned me = MASTER; me <= TRACER; me++) {         // a special tile's: its local input, its DMA, its attach port
        std::vector<Ev> s;
        uint64_t t = 10;
        for (unsigned dst : {67u, 100u, 125u, unsigned(lw::NONE)})
            for (unsigned port : {unsigned(LOCAL), unsigned(DMA), unsigned(ATTACH)}) s.push_back(ev(t++, me, port, 4, dst));
        s.push_back(ev(t, me, DMA, ANY, me));                // the DMA's next packet is not held up
        const Result r = run_router(me, s, 60);
        CHECK(same(r.out, {seen(t + 2, me, LOCAL, s.back(), me)}));
        CHECK_EQ(r.overflows, 0u);
    }
}

// 16.1, 16.2: an attach port for each special tile on its attach tile, one each on tiles 0, 7 and 15. A packet for a
// special tile leaves its attach tile by that attach port, from the local input and from a link; at another tile it
// goes on through the mesh towards the attach tile (the routes by the offset, 16.2: from tile 0, tile 7 is (1, -1) and
// tile 15 (2, -1), west first; from tile 7, tile 0 is (-1, 1), east first, and tile 15 (1, 0), south; from tile 15,
// tile 0 is (-2, 1), east first, and tile 7 (-1, 0), north). The special tile's own packets arrive on the attach port
// and are routed as the attach tile's: one for the attach tile ejects, one for the tile to its east leaves east.
TEST(packet_for_a_special_tile_leaves_its_attach_tile_by_the_attach_port) {
    const unsigned elsewhere[3][2] = {{WEST, WEST}, {EAST, SOUTH}, {EAST, NORTH}};   // the two other special tiles, in id order
    for (unsigned n = 0; n < 3; n++) {
        const unsigned at = kAttachTile[n], special = MASTER + n, west = (at + 63) % 64;
        std::vector<Ev> s = {ev(10, at, LOCAL, ANY, special), ev(20, at, WEST, west, special), ev(30, at, ATTACH, special, at),
                             ev(40, at, ATTACH, special, at + 1)};
        std::vector<Seen> want = {seen(12, at, ATTACH, s[0], at), seen(22, at, ATTACH, s[1], west), seen(32, at, LOCAL, s[2], special),
                                  seen(42, at, EAST, s[3], special)};
        uint64_t t = 50;
        unsigned k = 0;
        for (unsigned o = MASTER; o <= TRACER; o++) {
            if (o == special) continue;
            s.push_back(ev(t, at, LOCAL, ANY, o));
            want.push_back(seen(t + 2, at, elsewhere[n][k++], s.back(), at));
            t += 10;
        }
        const Result r = run_router(at, s, 100);
        CHECK(same(r.out, want));
        CHECK_EQ(r.overflows, 0u);
    }
}

// 16.3: "an attach output takes one packet a cycle, with no back-pressure". Tile 7's own packets and its west
// neighbour's, one of each a cycle for 20 cycles, all for the memory tile: the attach output's register holds a packet
// in every one of the next 40 cycles, the two queues' in turn, local's first (the pointer starts at 0), and none is lost.
TEST(attach_output_takes_one_packet_a_cycle) {
    std::vector<Ev> s;
    std::vector<Seen> want;
    for (uint64_t i = 0; i < 20; i++) {
        s.push_back(ev(30 + i, 7, LOCAL, ANY, MEMORY));
        want.push_back(seen(32 + 2 * i, 7, ATTACH, s.back(), 7));
        s.push_back(ev(30 + i, 7, WEST, 6, MEMORY));
        want.push_back(seen(33 + 2 * i, 7, ATTACH, s.back(), 6));
    }
    const Result r = run_router(7, s, 100);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 0u);
}

// 16.3, the arbiters, on tile 0's router, which has every input (the master tile's attach port). The eject: its
// queues in their inputs' order, west, east, north, south, attach, from a pointer that starts at 0; the loopback queue
// (local's) only when every other is empty, without moving the pointer; the pointer one past the queue served. The
// arrivals for tile 0: from the west tile 63's, from the east tile 1's, from the north tile 56's, from the south tile
// 8's, from the attach port the master tile's.
TEST(eject_arbiter_is_round_robin_in_the_inputs_order_with_the_loopback_queue_last) {
    const unsigned src_of[6] = {ANY, 63, 1, 56, 8, MASTER}, sent_src[6] = {0, 63, 1, 56, 8, MASTER};
    std::vector<Ev> s;
    std::vector<Seen> want;
    const auto arrive = [&](uint64_t at, unsigned port, uint64_t ejected) {
        s.push_back(ev(at, 0, port, src_of[port], 0, port == LOCAL ? lw::TAG_TOKEN : 0));
        want.push_back(seen(ejected, 0, LOCAL, s.back(), sent_src[port]));
        if (port == LOCAL) want.push_back(token(ejected, 0));
    };
    // all six at once, the reset's pointer: west, east, north, south, attach, and the tile's own last
    arrive(10, LOCAL, 17); arrive(10, WEST, 12); arrive(10, EAST, 13); arrive(10, NORTH, 14); arrive(10, SOUTH, 15); arrive(10, ATTACH, 16);
    // east's alone leaves the pointer at north: then north, south, and around to west
    arrive(30, EAST, 32);
    arrive(40, WEST, 44); arrive(40, NORTH, 42); arrive(40, SOUTH, 43);
    // east's again, then the tile's own, which does not move the pointer from north: north before west
    arrive(50, EAST, 52);
    arrive(54, LOCAL, 56);
    arrive(60, WEST, 63); arrive(60, NORTH, 62);
    // the tile's own waits while the west's keep coming
    arrive(70, LOCAL, 77);
    for (uint64_t t = 70; t < 75; t++) arrive(t, WEST, t + 2);
    const Result r = run_router(0, s, 100);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 0u);
}

// 16.3 on a link's output, tile 0's east: its queues are local's, the west input's and the attach port's (the RTL's
// README, "Queues by kind"), in that order, the pointer 0 at the reset and one past the queue served. All for tile 1:
// tile 0's own, tile 63's passing east (offset (0, 2)), the master tile's (routed as tile 0's).
TEST(link_output_arbiter_is_round_robin_from_local) {
    std::vector<Ev> s = {ev(10, 0, LOCAL, ANY, 1), ev(10, 0, WEST, 63, 1), ev(10, 0, ATTACH, MASTER, 1),
                         ev(20, 0, WEST, 63, 1),                                    // the pointer past west: at the attach port
                         ev(30, 0, LOCAL, ANY, 1), ev(30, 0, ATTACH, MASTER, 1)};
    const Result r = run_router(0, s, 60);
    CHECK(same(r.out, {seen(12, 0, EAST, s[0], 0), seen(13, 0, EAST, s[1], 63), seen(14, 0, EAST, s[2], MASTER), seen(22, 0, EAST, s[3], 63),
                       seen(32, 0, EAST, s[5], MASTER), seen(33, 0, EAST, s[4], 0)}));
    CHECK_EQ(r.overflows, 0u);
}

// 16.3, overflow: "a packet pushed into a full queue sets ERRORS.router_overflow"; a queue holds 16 packets and is
// full by its count at the start of the cycle, even if its head leaves in that cycle (router_output.veryl). Tile 5's
// west and east neighbours each send it a packet a cycle for 40 cycles: the eject takes one a cycle from the two queues
// in turn, west's first. By hand: k cycles after the first arrival west's queue holds ceil(k / 2) and east's
// floor(k / 2) + 1, so east loses its packet 30 first and west its packet 31, and then every second one of each.
TEST(push_into_a_full_queue_is_lost_and_counted) {
    Feed west{false, {}}, east{false, {}};
    for (uint64_t t = 10; t < 50; t++) {
        west.in.push_back(ev(t, 5, WEST, 4, 5));
        east.in.push_back(ev(t, 5, EAST, 6, 5));
    }
    std::vector<Ev> s = west.in;
    s.insert(s.end(), east.in.begin(), east.in.end());
    const Load l = one_output({west, east}, 10, 200);
    CHECK_EQ(l.lost.size(), 10u);
    CHECK_EQ(l.lost[0].id, east.in[30].id);
    CHECK_EQ(l.lost[1].id, west.in[31].id);
    std::vector<Seen> want;
    for (const auto& [at, e] : l.sent) want.push_back(seen(at, 5, LOCAL, e, lw::src(e.hdr)));
    const Result r = run_router(5, s, 200);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 10u);
    // `overflow` is 1 in the cycle of the lost push (router_output.veryl): the first packet lost is at the input in
    // cycle 40, and the stand, which reads the count as at the end of the cycle before, first sees it in 41
    CHECK_EQ(r.first_overflow, 41u);
}

// 16.1 and the RTL's README ("Links and timing"): a packet at the local input in cycle t that crosses h links, attach
// links included, is ejected in cycle t + 4h + 2 when no queue holds it back. 16.2: h is the offset's hops between the
// two tiles, a special tile routed as its attach tile, as a source and as a destination, and reached over its attach
// link. Every pair of the 67 tiles, one packet at a time, from the local input; and from each special tile's DMA.
TEST(unicast_is_ejected_at_its_destination_four_cycles_a_link) {
    std::vector<Ev> s;
    std::vector<Seen> want;
    uint64_t t = 10;
    for (unsigned src = 0; src < 67; src++)
        for (unsigned port : {unsigned(LOCAL), unsigned(DMA)}) {
            if (port == DMA && src < MASTER) continue;
            for (unsigned dst = 0; dst < 67; dst++) {
                s.push_back(ev(t, src, port, ANY, dst));
                want.push_back(seen(t + 4 * unicast_links(src, dst) + 2, dst, LOCAL, s.back(), src));
                t += 48;                                     // the longest: 7 hops and two attach links, 38 cycles
            }
        }
    CHECK_EQ(unicast_links(0, 9), 2u);                       // (1, 1)
    CHECK_EQ(unicast_links(0, 36), 7u);                      // (-3, -4): the shorter way round the column
    CHECK_EQ(unicast_links(MEMORY, TRACER), 3u);             // tiles 7 and 15, a row apart
    const Result r = run_noc(s, t + 48);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 0u);
    CHECK_EQ(r.attached, 3u);                                // 16.1: an attach port on tiles 0, 7 and 15, one each
}

// 16.2, broadcast: every compute tile ejects exactly one copy and no special tile any; a special tile's "spreads from
// its attach tile as that tile's own", one attach link later. Each copy's cycle is its hops' (broadcast_hops above,
// from the specification's reach), four cycles a link (16.1). From every tile's local input and each special tile's DMA.
TEST(broadcast_reaches_every_compute_tile_once_and_a_special_tiles_spreads_from_its_attach_tile) {
    std::vector<Ev> s;
    std::vector<Seen> want;
    uint64_t t = 10;
    for (unsigned src = 0; src < 67; src++)
        for (unsigned port : {unsigned(LOCAL), unsigned(DMA)}) {
            if (port == DMA && src < MASTER) continue;
            s.push_back(ev(t, src, port, ANY, lw::BROADCAST));
            for (unsigned k = 0; k < 64; k++)
                want.push_back(seen(t + 4 * (broadcast_hops(mesh_tile(src), k) + (src >= MASTER)) + 2, k, LOCAL, s.back(), src));
            t += 48;
        }
    CHECK_EQ(broadcast_hops(0, 36), 8u);                     // 4 east, 4 south
    CHECK_EQ(broadcast_hops(7, 8), 1u);                      // the ring passes the row's end
    CHECK_EQ(broadcast_hops(7, 0), 2u);                      // and tile 8's copy goes north
    const Result r = run_noc(s, t + 48);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 0u);
}

// 16.3 on the whole NoC: the fan-in figures for receiver 27 that the RTL's router_test requires of the RTL's NoC
// (rtl/blocks/router/test/src/router_test.rs, "Fan-in, at 8x8: the figures of meshsim's NetTest for receiver 27"): the
// two nearest senders, tiles 28 and 35, each sending a burst of 30 packets lose none; of 31, one, to a full queue at
// tile 27, 34 cycles after the bursts start; and all 63 other tiles sending one packet each lose two, 34 and 35 cycles
// after. A burst starts in the cycle its first packets are at their routers' local inputs (router_test.rs's step presents
// a local's packet in the cycle it counts), an event's `at` here: the losses are in `at` + 34 (and + 35).
TEST(fan_in_at_tile_27_loses_what_the_rtls_noc_loses) {
    const uint64_t at = 10;
    for (unsigned burst : {30u, 31u}) {
        std::vector<Ev> s;
        for (unsigned i = 0; i < burst; i++) for (unsigned source : {28u, 35u}) s.push_back(ev(at + i, source, LOCAL, ANY, 27));
        const Result r = run_noc(s, 400);
        const std::vector<std::pair<uint64_t, unsigned>> want = burst == 30 ? std::vector<std::pair<uint64_t, unsigned>>{}
                                                                            : std::vector<std::pair<uint64_t, unsigned>>{{at + 34, 27}};
        CHECK(r.lost == want);
        CHECK_EQ(r.overflows, uint64_t(want.size()));
        unsigned ejected = 0;
        for (const Seen& x : r.out) if (x.tile == 27 && x.port == LOCAL) ejected++;
        CHECK_EQ(ejected, 2 * burst - unsigned(want.size()));               // every other packet reaches the eject
    }
    std::vector<Ev> s;
    for (unsigned source = 0; source < 64; source++) if (source != 27) s.push_back(ev(at, source, LOCAL, ANY, 27));
    const Result r = run_noc(s, 400);
    const std::vector<std::pair<uint64_t, unsigned>> want = {{at + 34, 27}, {at + 35, 27}};
    CHECK(r.lost == want);
    CHECK_EQ(r.overflows, 2u);
    unsigned ejected = 0;
    for (const Seen& x : r.out) if (x.tile == 27 && x.port == LOCAL) ejected++;
    CHECK_EQ(ejected, 61u);
}

// ---- v1.37: the line between the special tiles (16.1 to 16.4; the INTEGRATOR's stage 2) ------------------------------

// The links a unicast crosses on v1.37's chip: between two special tiles, the line's, one a special tile (16.2: "one hop
// a special tile"); else as before, a special tile routed as its attach tile.
unsigned unicast_links_v137(unsigned src, unsigned dst) {
    if (src >= MASTER && dst >= MASTER) return unsigned(std::abs(int(dst) - int(src)));
    return unicast_links(src, dst);
}

// 16.1, 16.2: every pair of the 67 tiles, one packet at a time, from the local input and from each special tile's DMA (on
// south): ejected at its destination four cycles a link after it was at its input, the special tiles' packets for one
// another along the line (the master tile's for the tracer tile through the memory tile's router), every other route as
// v1.36's; a compute tile's packet for a special tile never takes the line.
TEST(line_unicast_between_special_tiles_takes_the_line) {
    std::vector<Ev> s;
    std::vector<Seen> want;
    uint64_t t = 10;
    for (unsigned src = 0; src < 67; src++)
        for (unsigned port : {unsigned(LOCAL), unsigned(DMA)}) {
            if (port == DMA && src < MASTER) continue;
            for (unsigned dst = 0; dst < 67; dst++) {
                s.push_back(ev(t, src, port, ANY, dst));
                want.push_back(seen(t + 4 * unicast_links_v137(src, dst) + 2, dst, LOCAL, s.back(), src));
                t += 48;
            }
        }
    CHECK_EQ(unicast_links_v137(MASTER, TRACER), 2u);        // the line's two links, through the memory tile's router
    CHECK_EQ(unicast_links_v137(MEMORY, TRACER), 1u);        // v1.36: 3 (tiles 7 and 15, a row apart)
    CHECK_EQ(unicast_links_v137(20, TRACER), unicast_links(20, TRACER));
    const Result r = run_noc(s, t + 48, true);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 0u);
}
// 16.2: a special tile's broadcast still spreads from its attach tile as that tile's own, by its attach port, with the line.
TEST(line_broadcast_from_a_special_tile_spreads_from_its_attach_tile) {
    std::vector<Ev> s;
    std::vector<Seen> want;
    uint64_t t = 10;
    for (unsigned src = MASTER; src < 67; src++)
        for (unsigned port : {unsigned(LOCAL), unsigned(DMA)}) {
            s.push_back(ev(t, src, port, ANY, lw::BROADCAST));
            for (unsigned k = 0; k < 64; k++) want.push_back(seen(t + 4 * (broadcast_hops(mesh_tile(src), k) + 1) + 2, k, LOCAL, s.back(), src));
            t += 48;
        }
    const Result r = run_noc(s, t + 48, true);
    CHECK(same(r.out, want));
    CHECK_EQ(r.overflows, 0u);
}
// 16.3, invariant 40: the line has no flow control, so traffic that outruns it overflows a queue, which is counted on the
// router it is in. The tracer tile and the memory tile each send the master tile a packet a cycle, from the tracer's
// local input and the memory tile's local input and DMA: the memory tile's west output takes one a cycle, and the queue
// from the line (east), full first, loses packets at the memory tile's router.
TEST(line_overflows_where_its_traffic_is_not_paced) {
    std::vector<Ev> s;
    for (uint64_t i = 0; i < 40; i++) {
        s.push_back(ev(10 + i, TRACER, LOCAL, ANY, MASTER));
        s.push_back(ev(10 + i, MEMORY, LOCAL, ANY, MASTER));
        s.push_back(ev(10 + i, MEMORY, DMA, ANY, MASTER));
    }
    const Result r = run_noc(s, 600, true);
    CHECK(r.overflows > 0);
    bool only_memory = !r.lost.empty();
    for (const auto& l : r.lost) if (l.second != MEMORY) only_memory = false;
    CHECK(only_memory);                                      // counted on the router the full queue is in (16.3)
    unsigned ejected = 0;
    for (const Seen& x : r.out) if (x.tile == MASTER && x.port == LOCAL) ejected++;
    CHECK_EQ(uint64_t(ejected) + r.overflows, uint64_t(120));    // every packet ejected or lost; the DMA's wait for room (16.4)
}
// 16.3: the line's output's arbiter, round-robin over its queues in the inputs' order: local, then the line, then the
// DMA. The memory tile's west output, fed by its local input, the line from the tracer tile and its DMA in one cycle, sends
// them in that order, a cycle apart.
TEST(line_output_arbiter_is_round_robin_in_the_inputs_order) {
    std::vector<Ev> s;
    s.push_back(ev(30, MEMORY, DMA, ANY, MASTER));
    s.push_back(ev(30, MEMORY, LOCAL, ANY, MASTER));
    s.push_back(ev(26, TRACER, LOCAL, ANY, MASTER));          // on the line: at the memory tile's east input in 30
    const Ev dma = s[0], local = s[1], far = s[2];
    const Result r = run_noc(s, 200, true);
    std::vector<Seen> want = {seen(30 + 4 + 2, MASTER, LOCAL, local, MEMORY), seen(30 + 4 + 3, MASTER, LOCAL, far, TRACER),
                              seen(30 + 4 + 4, MASTER, LOCAL, dma, MEMORY)};
    CHECK(same(r.out, want));
}
