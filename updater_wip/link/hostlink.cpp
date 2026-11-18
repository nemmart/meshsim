// hostlink.cpp: see hostlink.h. Queues of whole packets around a link end (link_end.h) and around a chain node
// (node.h), wired as the chain bench wires them (chain.cpp): a stream says stop when its queue is half full, and a
// packet is taken only if its stream's queue has room for the whole of it.
#include "hostlink.h"

#include <cstring>
#include <deque>

#include "node.h"

namespace hostlink {

namespace {

constexpr unsigned PACKET_BYTES = MAX_WORDS * WORD_BYTES;

struct Received { uint8_t hops = 0, length = 0, taken = 0; uint8_t data[PACKET_BYTES]; };
struct Waiting { uint8_t length = 0; uint32_t id = 0; uint8_t data[PACKET_BYTES]; };

hl::Params params(const Sizes& z) {
    hl::Params p;
    p.window = z.window;
    return p;
}

// Why a link started again, kept beside the link end's one error bit: the link end shows the cause for one cycle.
struct Cause {
    bool made_wrong = false, not_kept = false, other_end = false;
    void watch(const hl::LinkEnd& le) {    // after each step
        if (!le.ev.restarted) return;
        if (le.ev.wrong_twice) made_wrong = true;
        else if (le.ev.not_kept) not_kept = true;
        else other_end = true;
    }
};

LinkStatus status_of(const hl::LinkEnd& le, const Cause& c) {
    LinkStatus s;
    s.in_step = le.running(); s.error = le.r.error;
    s.made_wrong = c.made_wrong; s.not_kept = c.not_kept; s.other_end = c.other_end;
    for (unsigned i = 0; i < 2; i++) { s.stop_said[i] = le.r.stop_sent[i]; s.stop_heard[i] = le.r.peer_stop[i]; }
    return s;
}

LinkCounts counts_of(const hl::LinkEnd& le) {
    const hl::Counts& c = le.count;
    LinkCounts n;
    n.good = c.rx_good; n.taken = c.delivered; n.bad_header = c.rx_bad_header; n.bad_data = c.rx_bad_data; n.bad_fields = c.rx_bad_fields;
    n.refused = c.refused; n.ahead = c.ahead; n.behind = c.behind;
    n.sent_data = c.sent[unsigned(hl::Kind::DATA)]; n.sent_again = c.sent[unsigned(hl::Kind::RESENT)]; n.sent_status = c.sent[unsigned(hl::Kind::STATUS)];
    n.went_back = c.went_back; n.rounds = c.rounds; n.restarts = c.restarts;
    n.wrong_once = c.wrong_once; n.wrong_twice = c.made_wrong; n.asked_not_kept = c.not_kept;
    n.ack_behind = c.ack_behind; n.ack_beyond = c.ack_beyond;
    return n;
}

// A stream's words received, as the packets they came in.
struct InQueue {
    std::deque<Received> packets;
    unsigned words = 0;                    // not yet taken

    void put(uint8_t hops, uint8_t length, const uint8_t* data) {
        packets.emplace_back();
        Received& r = packets.back();
        r.hops = hops; r.length = length; memcpy(r.data, data, size_t(length) * WORD_BYTES);
        words += length;
    }
    // The next word of the oldest packet, or of the oldest packet with these hops (hops < 0: any).
    bool take(int hops, Word& word, unsigned* its_hops, bool* last) {
        for (auto it = packets.begin(); it != packets.end(); ++it) {
            if (hops >= 0 && it->hops != hops) continue;
            memcpy(word.byte, it->data + size_t(it->taken) * WORD_BYTES, WORD_BYTES);
            if (its_hops) *its_hops = it->hops;
            const bool end = ++it->taken == it->length;
            if (last) *last = end;
            if (end) packets.erase(it);
            words--;
            return true;
        }
        return false;
    }
    bool peek(Word& word) const {          // the oldest packet's next word, left in place
        if (packets.empty()) return false;
        memcpy(word.byte, packets.front().data + size_t(packets.front().taken) * WORD_BYTES, WORD_BYTES);
        return true;
    }
    unsigned waiting(int hops) const {
        if (hops < 0) return words;
        unsigned n = 0;
        for (const Received& r : packets) if (r.hops == hops) n += unsigned(r.length - r.taken);
        return n;
    }
    void clear() { packets.clear(); words = 0; }
};

// A stream's packets waiting to be sent.
struct OutQueue {
    std::deque<Waiting> packets;
    unsigned words = 0, cap = 0;
    uint32_t made = 0;
    hl::Offer offer;

    bool put(const Word* w, unsigned count) {
        if (count < 1 || count > MAX_WORDS || words + count > cap) return false;
        packets.emplace_back();
        Waiting& p = packets.back();
        p.length = uint8_t(count); p.id = (++made & 0x7FFFFFFFu) | 0x40000000u;     // never 0, and never a forwarded one's
        memcpy(p.data, w, size_t(count) * WORD_BYTES);
        words += count;
        return true;
    }
    const hl::Offer* front() {
        if (packets.empty()) return nullptr;
        const Waiting& p = packets.front();
        offer.length = p.length; offer.hops = 0; offer.id = p.id; offer.data = p.data;
        return &offer;
    }
    void pop() { words -= packets.front().length; packets.pop_front(); }
    void clear() { packets.clear(); words = 0; }
};

}  // namespace

static_assert(sizeof(Word) == WORD_BYTES);
static_assert(WORD_BYTES == hl::WORD_BYTES && MAX_WORDS == hl::MAX_WORDS);
static_assert(unsigned(FOREGROUND) == hl::FG && unsigned(BACKGROUND) == hl::BG);

// ---- End ----

struct End::Impl {
    Sizes z;
    hl::LinkEnd le;
    Cause cause;
    InQueue in[2];
    OutQueue out[2];
    explicit Impl(const Sizes& sizes) : z(sizes), le(params(sizes)) { for (auto& o : out) o.cap = z.send_words; }
};

End::End(const Sizes& sizes) : d(new Impl(sizes)) {}
End::~End() = default;
End::End(End&&) noexcept = default;
End& End::operator=(End&&) noexcept = default;

uint8_t End::clock(uint8_t byte_in) {
    hl::In in;
    in.rx_byte = byte_in;
    for (unsigned s = 0; s < 2; s++) {
        const unsigned fill = d->in[s].words;
        in.say_stop[s] = fill * 2 >= d->z.queue_words;
        in.rx_free[s] = d->z.queue_words > fill ? d->z.queue_words - fill : 0;
        in.offer[s] = d->out[s].front();
    }
    const hl::Taken t = d->le.step(in);
    d->cause.watch(d->le);
    for (unsigned s = 0; s < 2; s++) if (t.tx[s]) d->out[s].pop();
    const auto& r = d->le.r;
    if (r.rx_valid) d->in[r.rx_stream].put(r.rx_hops, r.rx_length, d->le.rx_data());
    return r.tx_byte;
}

bool End::push(Stream stream, const Word* words, unsigned count) { return stream <= BACKGROUND && d->out[stream].put(words, count); }
unsigned End::room(Stream stream) const { return stream <= BACKGROUND ? d->out[stream].cap - d->out[stream].words : 0; }
bool End::take(Stream stream, Word& word, unsigned* hops, bool* last) { return stream <= BACKGROUND && d->in[stream].take(-1, word, hops, last); }
unsigned End::waiting(Stream stream) const { return stream <= BACKGROUND ? d->in[stream].waiting(-1) : 0; }
bool End::take_from(unsigned hops, Word& word, bool* last) { return hops < MAX_CHAIN && d->in[FOREGROUND].take(int(hops), word, nullptr, last); }
unsigned End::waiting_from(unsigned hops) const { return hops < MAX_CHAIN ? d->in[FOREGROUND].waiting(int(hops)) : 0; }
LinkStatus End::status() const { return status_of(d->le, d->cause); }
LinkCounts End::counts() const { return counts_of(d->le); }
void End::clear_error() { d->le.r.error = false; d->cause = Cause(); }

void End::power_on() {
    d->le.power_on(); d->le.count = hl::Counts(); d->cause = Cause();
    for (unsigned s = 0; s < 2; s++) { d->in[s].clear(); d->out[s].clear(); }
}

// ---- Node ----

struct Node::Impl {
    Sizes z;
    hl::ChainNode node;
    Cause cause_up, cause_down;
    InQueue in[2];
    OutQueue out;
    Impl(bool below, const Sizes& sizes) : z(sizes), node(params(sizes), below, sizes.forward_words) { out.cap = z.send_words; }
};

Node::Node(bool neighbour_below, const Sizes& sizes) : d(new Impl(neighbour_below, sizes)) {}
Node::~Node() = default;
Node::Node(Node&&) noexcept = default;
Node& Node::operator=(Node&&) noexcept = default;

Node::Bytes Node::clock(uint8_t from_above, uint8_t from_below) {
    hl::NodeIn in;
    in.up_rx = from_above; in.down_rx = from_below;
    for (unsigned s = 0; s < 2; s++) {
        const unsigned fill = d->in[s].words;
        in.own_stop[s] = fill * 2 >= d->z.queue_words;
        in.own_free[s] = d->z.queue_words > fill ? d->z.queue_words - fill : 0;
    }
    in.own_offer = d->out.front();
    d->node.step(in);
    d->cause_up.watch(d->node.up);
    if (d->node.has_down) d->cause_down.watch(d->node.down);
    if (d->node.own_taken) d->out.pop();
    const auto& r = d->node.up.r;
    if (r.rx_valid) d->in[r.rx_stream].put(r.rx_hops, r.rx_length, d->node.up.rx_data());
    Bytes b;
    b.up = r.tx_byte;
    b.down = d->node.has_down ? d->node.down.r.tx_byte : 0;
    return b;
}

bool Node::take(Stream stream, Word& word, bool* last) { return stream <= BACKGROUND && d->in[stream].take(-1, word, nullptr, last); }
unsigned Node::waiting(Stream stream) const { return stream <= BACKGROUND ? d->in[stream].waiting(-1) : 0; }
bool Node::peek(Stream stream, Word& word) const { return stream <= BACKGROUND && d->in[stream].peek(word); }
bool Node::push(const Word* words, unsigned count) { return d->out.put(words, count); }
unsigned Node::room() const { return d->out.cap - d->out.words; }
LinkStatus Node::above() const { return status_of(d->node.up, d->cause_up); }
LinkStatus Node::below() const { return d->node.has_down ? status_of(d->node.down, d->cause_down) : LinkStatus(); }
LinkCounts Node::counts_above() const { return counts_of(d->node.up); }
LinkCounts Node::counts_below() const { return d->node.has_down ? counts_of(d->node.down) : LinkCounts(); }
void Node::clear_error() { d->node.up.r.error = false; d->node.down.r.error = false; d->cause_up = d->cause_down = Cause(); }

void Node::power_on() {
    d->node.up.power_on(); d->node.down.power_on();
    d->node.up.count = hl::Counts(); d->node.down.count = hl::Counts(); d->cause_up = d->cause_down = Cause();
    for (unsigned s = 0; s < 2; s++) {
        d->node.fwd_down[s].size(d->z.forward_words); d->node.fwd_up[s].size(d->z.forward_words);
        d->in[s].clear();
    }
    d->out.clear();
}

}  // namespace hostlink
