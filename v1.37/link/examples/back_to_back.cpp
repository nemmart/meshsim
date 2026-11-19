// Two ends back to back: a wire each way, one cycle long. End a sends three words on the foreground; end b takes
// them out.
#include <cstdio>
#include <cstring>

#include "hostlink.h"

int main() {
    hostlink::End a, b;
    uint8_t a_to_b = 0, b_to_a = 0;        // the two wires: the byte each end sent last cycle

    hostlink::Word message[3] = {};
    strcpy(reinterpret_cast<char*>(message[0].byte), "one");
    strcpy(reinterpret_cast<char*>(message[1].byte), "two");
    strcpy(reinterpret_cast<char*>(message[2].byte), "three");
    if (!a.push(hostlink::FOREGROUND, message, 3)) return 1;

    unsigned got = 0;
    for (unsigned cycle = 0; cycle < 20000 && got < 3; cycle++) {
        const uint8_t from_a = a.clock(b_to_a);        // every end, every cycle
        const uint8_t from_b = b.clock(a_to_b);
        a_to_b = from_a; b_to_a = from_b;

        hostlink::Word word;
        bool last = false;
        while (b.take(hostlink::FOREGROUND, word, nullptr, &last)) {
            printf("cycle %u: b took \"%s\"%s\n", cycle, reinterpret_cast<const char*>(word.byte), last ? " (the packet's last word)" : "");
            got++;
        }
    }
    const hostlink::LinkStatus s = a.status();
    printf("a: in step %d, error %d\n", s.in_step, s.error);
    return got == 3 && s.in_step && !s.error ? 0 : 1;
}
