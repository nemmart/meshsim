// hostlink.h: the host link, for whoever builds on it. This is the one header to include; README.md, "Using the link
// end", has two examples that build and run, what a user must promise, and what it may rely on.
//
// The protocol is v1.4_wip/planning/HOST-LINK.md. Two things are here:
//
//   End    one end of one link, with its queues: the host's side of the first link, or either side of a link on its
//          own.
//   Node   an ASIC on the chain: the link towards the host, the link away from it, and the forwarding between them.
//
// A CYCLE is one byte on the link. Each object has one function, clock(), called once a cycle: the bytes that arrive
// go in, the bytes that leave come out. The bytes are all there is between two objects, so they may sit in one
// program, in two processes, or on two machines. An idle line is 0x00.
//
// DATA is words of 128 bits on two streams, the foreground and the background. Words are pushed in as packets of 1
// to 32 words and taken out a word at a time, in the order they were pushed, each once. A packet's last word is
// marked, so a packet pushed is a packet taken.
//
// This header will keep its shape. What is behind it (link_end.h, node.h) is the model of the hardware and may
// change with the protocol.
#pragma once

#include <cstdint>
#include <memory>

namespace hostlink {

constexpr unsigned WORD_BYTES = 16;        // a word is 128 bits
constexpr unsigned MAX_WORDS = 32;         // a packet's data, at most
constexpr unsigned MAX_CHAIN = 16;         // the ASICs on a chain, at most: hops is four bits

enum Stream : unsigned { FOREGROUND = 0, BACKGROUND = 1 };

struct Word { uint8_t byte[WORD_BYTES]; };

// The sizes, as HOST-LINK.md has them. Change one only to model something else.
struct Sizes {
    unsigned window = 8;                   // packets kept until they are acknowledged, for each direction of a link.
                                           // The chip's is 8. A host's may be 16 at most: half a queue of 1,024 words
    unsigned queue_words = 1024;           // a stream's queue of words received: 16 KB. "Stop" is said at half full
    unsigned send_words = 1024;            // a stream's queue of words waiting to be sent (the document gives no size)
    unsigned forward_words = 1024;         // a node's forwarding buffer, for each stream and direction: 16 KB
};

// What one end of a link shows its owner.
struct LinkStatus {
    bool in_step = false;                  // false while the link recovers or starts: no data moves on it meanwhile
    bool error = false;                    // the link started again under this end, and packets it had sent and not
                                           // yet had acknowledged are gone. Stays up until clear_error()
    bool stop_said[2] = {};                // this end's stop bit for each stream, as last sent: its queue is half full
    bool stop_heard[2] = {};               // the other end's: this end sends no new packet of that stream
};

// One end of one link.
class End {
public:
    explicit End(const Sizes& sizes = Sizes());
    ~End();
    End(End&&) noexcept;
    End& operator=(End&&) noexcept;

    // One cycle: the byte that arrived, and the byte to send.
    uint8_t clock(uint8_t byte_in);

    // Sending. One packet of 1 to 32 words; they are copied. False, and nothing is taken, if the stream's queue
    // has no room for them or the count is not 1 to 32.
    bool push(Stream stream, const Word* words, unsigned count);
    unsigned room(Stream stream) const;    // the words push() would still take

    // Receiving, a word at a time, in order. False if the stream has none. `hops` is how many ASICs the word's
    // packet passed through: at the host, which ASIC sent it (0 is the nearest). `last` is true on a packet's
    // last word.
    bool take(Stream stream, Word& word, unsigned* hops = nullptr, bool* last = nullptr);
    unsigned waiting(Stream stream) const; // the words take() can still give

    // The host's sorting by hops: the next foreground word of the ASIC that many hops away, in that ASIC's order.
    bool take_from(unsigned hops, Word& word, bool* last = nullptr);
    unsigned waiting_from(unsigned hops) const;

    LinkStatus status() const;
    void clear_error();
    void power_on();                       // as constructed: every register, and the queues empty

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// An ASIC on the chain.
class Node {
public:
    // `neighbour_below` is the pin: true if another ASIC follows this one on the chain. It is a fact about the
    // board and does not change. The last ASIC has none, and its lower link does not exist.
    explicit Node(bool neighbour_below, const Sizes& sizes = Sizes());
    ~Node();
    Node(Node&&) noexcept;
    Node& operator=(Node&&) noexcept;

    // One cycle. In: the byte from the link towards the host, and the byte from the link away from it (not looked
    // at with no neighbour below). Out: the byte for each.
    struct Bytes { uint8_t up = 0, down = 0; };
    Bytes clock(uint8_t from_above, uint8_t from_below);

    // The host's words, for this ASIC: every ASIC gets them all.
    bool take(Stream stream, Word& word, bool* last = nullptr);
    unsigned waiting(Stream stream) const;

    // This ASIC's own words for the host, on the foreground: one packet of 1 to 32 words. False, and nothing is
    // taken, if there is no room or the count is not 1 to 32.
    bool push(const Word* words, unsigned count);
    unsigned room() const;

    LinkStatus above() const;              // the link towards the host
    LinkStatus below() const;              // the link away from it; all false with no neighbour below
    void clear_error();                    // both links'
    void power_on();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace hostlink
