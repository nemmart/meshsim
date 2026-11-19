// A host, two ASICs on a chain, and a reply from each. The host sends one query; every ASIC gets it; each answers
// with its own words; the host takes the answers out sorted by hops, which is each ASIC's place on the chain.
#include <cstdio>
#include <cstring>

#include "hostlink.h"

static hostlink::Word text(const char* s) {
    hostlink::Word w = {};
    strncpy(reinterpret_cast<char*>(w.byte), s, hostlink::WORD_BYTES - 1);
    return w;
}

int main() {
    hostlink::End host;
    hostlink::Node asic[2] = {hostlink::Node(true),      // the pin: an ASIC follows this one
                              hostlink::Node(false)};    // the last on the chain
    const char* name[2] = {"the near one", "the far one"};

    // The wires, a cycle long each: [0] between the host and asic[0], [1] between asic[0] and asic[1].
    uint8_t down[2] = {}, up[2] = {};

    const hostlink::Word query = text("who is there?");
    if (!host.push(hostlink::FOREGROUND, &query, 1)) return 1;

    unsigned answers = 0;
    for (unsigned cycle = 0; cycle < 40000 && answers < 2; cycle++) {
        const uint8_t from_host = host.clock(up[0]);
        const hostlink::Node::Bytes from0 = asic[0].clock(down[0], up[1]);
        const hostlink::Node::Bytes from1 = asic[1].clock(down[1], 0);
        down[0] = from_host; up[0] = from0.up; down[1] = from0.down; up[1] = from1.up;

        // Each ASIC: take the query, answer it. An answer is two words here: one packet.
        for (unsigned i = 0; i < 2; i++) {
            hostlink::Word word;
            while (asic[i].take(hostlink::FOREGROUND, word)) {
                printf("cycle %u: asic %u took \"%s\"\n", cycle, i, reinterpret_cast<const char*>(word.byte));
                const hostlink::Word answer[2] = {text(name[i]), text("and I am well")};
                if (!asic[i].push(answer, 2)) return 1;
            }
        }
        // The host: each ASIC's answer, by its hops.
        for (unsigned hops = 0; hops < 2; hops++) {
            hostlink::Word word;
            bool last = false;
            while (host.take_from(hops, word, &last)) {
                printf("cycle %u: from the ASIC %u hops away: \"%s\"\n", cycle, hops, reinterpret_cast<const char*>(word.byte));
                if (last) answers++;
            }
        }
    }
    return answers == 2 && !host.status().error ? 0 : 1;
}
