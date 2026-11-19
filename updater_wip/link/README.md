# updater_wip/link: the host link's protocol, on its own

WORKER8's work (`../planning/sessions/WORKER8/`): the link of `v1.4_wip/planning/HOST-LINK.md` ("HL") built as it is
written, put on wires that misbehave, and searched. No chip, no mesh, nothing of `v1.37/`.

**Where it stands (2026-11-18):** on HL as it now stands: the header of 16 bytes, one run of packet numbers for a
link, "stop" holding only packets not yet numbered, the data masked on the wire, and the sender's fast unit.
**Everything run so far holds:** the searches, on one link and on chains of 1, 2 and 3; 18 planted bugs; a case for
every rule; the random runs made here and the runner's campaign. `REPORT.md` has the results and the figures, and
`HARDWARE.md` has the link end as hardware. Both are in `../planning/sessions/WORKER8/`; a copy of this folder that
stands alone (v1.37's `link/`) has them beside this page, with `make_task.sh`, which writes the runner's tasks.
`rates.py` is still of the header before.

**To build on the link, read the next section and `hostlink.h`, and nothing else.**

## Using the link end

`hostlink.h` is the interface: one header, two classes, and it will keep its shape. `make lib` builds
`build/libhostlink.a`, which holds it and the model behind it and nothing of the benches.

```
make lib
g++ -std=c++17 -I updater_wip/link yours.cpp updater_wip/link/build/libhostlink.a
```

| class | what it is | for |
|---|---|---|
| `hostlink::End` | one end of one link, with its queues | the host's side of the first link; or either side of a link on its own |
| `hostlink::Node` | an ASIC on the chain: the link towards the host, the link away from it, the forwarding between | the chip's Ingress and Egress; a chain |

- **A cycle is one byte on the link.** Each object has one function, `clock()`, called once a cycle: the byte that
  arrived goes in, the byte to send comes out. An idle line is `0x00`.
- **The bytes are all there is between two objects,** so they can sit in one program, in two processes or on two
  machines. `test_hostlink` runs two ends in two processes with a socket between.
- **Data is words of 128 bits on two streams,** the foreground and the background. Words are pushed in as packets
  of 1 to 32 words and taken out a word at a time, with a packet's last word marked.

### Two ends back to back

`examples/back_to_back.cpp`:

```cpp
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
```

```
cycle 4736: b took "one"
cycle 4736: b took "two"
cycle 4736: b took "three" (the packet's last word)
a: in step 1, error 0
```

- The first 4,657 cycles are the two ends coming up from power-on (`build/linkcost start`). Nothing has to be done
  for it but to keep calling `clock()`.

### A host, two ASICs, and a reply sorted by hops

`examples/chain_reply.cpp`:

```cpp
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
```

```
cycle 4704: asic 0 took "who is there?"
cycle 4737: asic 1 took "who is there?"
cycle 4768: from the ASIC 0 hops away: "the near one"
cycle 4768: from the ASIC 0 hops away: "and I am well"
cycle 4852: from the ASIC 1 hops away: "the far one"
cycle 4852: from the ASIC 1 hops away: "and I am well"
```

- **No ASIC knows its place.** Each sends its reply with hops 0, every ASIC that forwards it adds one, and the
  host takes each ASIC's words with `take_from(hops)`: 0 is the nearest.

### What a user must promise

| | the promise | if it is broken |
|---|---|---|
| 1 | **`clock()` is called once every cycle for every object,** from power-on on, with or without anything to send | the timers count cycles: a status every 512, the wait before asking again. An object that is not clocked is a dead ASIC to its neighbours |
| 2 | **every byte that `clock()` returns reaches the other end's `clock()`,** in order, after a delay that does not change; where nothing is connected, `0x00` goes in | a byte lost, doubled or changed is damage. The link repairs it, as it repairs noise, but it costs what damage costs |
| 3 | **the pin is the board's:** `neighbour_below` is true exactly when another ASIC follows | true with nothing below: the ASIC holds what it must forward for a link that never comes up, and in the end stops the host. False with an ASIC below: that ASIC is never heard |
| 4 | **words are taken out.** A stream with 512 words waiting says "stop" to its neighbour | the background backs up link by link to the host: it moves at the pace of its slowest ASIC. The foreground has its own stop bit and still gets through, so take the foreground promptly |
| 5 | **a `push()` that returns false is tried again later** | the words were not taken: nothing of them is sent |
| 6 | **the error is looked at:** `status().error`, with its cause beside it. `clear_error()` takes it down; the chip has no such thing, and there only the reset does | it stays up. The link carries on either way; what was lost stays lost (below) |
| 7 | **an ASIC's own words go on the foreground only** (`Node::push` has no stream) | |
| 8 | **no more than 16 ASICs on a chain** | hops is four bits: a packet with hops 15 that must be forwarded is a packet made wrong, and the link starts again |
| 9 | **the sizes are left alone,** but for a host's window, 16 at most | the window must be no more than half the queue it feeds, or a packet can find no room |
| 10 | **one thread at a time** uses an object | nothing is locked. Different objects share nothing |

### What a user may rely on

| | what | on what it rests |
|---|---|---|
| 1 | **Every word pushed comes out at the other side once, in the order pushed, on its stream, its packet's end where it was,** while no error is raised | the searches of the small model, every state; the runner's campaign of 6,071 runs; `test_hostlink` through this header |
| 2 | **Every ASIC gets all the host's words;** the host gets each ASIC's words in that ASIC's order under its hops. Different ASICs' packets interleave | the same, on chains of 1 to 16 |
| 3 | **It cannot stop for good.** Once the damage stops, everything pushed arrives, with no call but `clock()` and `take()` | the searches: from every state reached, with nothing more going wrong, it finishes |
| 4 | **A stream that is stopped does not hold the other up** | searched with each stream never taken again in turn (`searches.sh held`); `test_hostlink`, with one ASIC that never takes its background |
| 5 | **Damage on the wire costs time, not words.** A damaged packet is thrown away whole and sent again | the same searches and runs |
| 6 | **An error means the link started again under this end.** What this end had sent and not yet had acknowledged, 8 packets at most, may be missing at the other side. Nothing is repeated and nothing arrives out of order. The link comes up again by itself, and what is still in the queues goes | `test_hostlink`: an end powered on under a running link; `cases/power-on.case` |
| 7 | **An end that powers on raises its neighbour's error,** 1,554 cycles later; the link is up again 3,648 cycles after the power-on | `build/linkcost start` |
| 8 | **An ASIC that is reset in a running chain raises the error of both its neighbours,** and of nobody else, itself included; the links come up again; the ASICs above it miss nothing, and below it words go missing but none is repeated or out of order | `build/test_reset`: 1,380 runs, an ASIC of 3, 4 or 16 reset at a random cycle, held up to 6,000 cycles, with and without noise; every ASIC at once; the same one twice; two neighbours one after the other |
| 9 | **The wire's delay may be anything that does not change.** Up to 512 cycles each way the window of 8 is never full | `build/linkcost window` |

**What an end tells its owner** (`status()`, `counts()`; a `Node` has them for each of its links):

| | | |
|---|---|---|
| `in_step` | the link is up | false while it recovers or starts: no data moves on it meanwhile |
| `error` | the link started again under this end | sticky. Packets this end had sent and not had acknowledged are gone |
| `made_wrong` | why: this end was sent a packet made wrong twice running | the fault is the **other** end's. The chip's `format_error` |
| `not_kept` | why: this end was asked for a packet it no longer keeps | an acknowledgement it had believed was false |
| `other_end` | why: the other end said "I have started again" | it was reset or powered on, or it found one of the two causes above |
| `stop_said`, `stop_heard` | the stop bits, each stream | this end's as last sent; the other end's as last heard |
| `counts()` | what the end has counted since power-on | packets good, taken, damaged (header, data, fields), ahead, behind; sent, sent again, of length 0; times it went back; rounds; restarts and their causes |
| `counts().refused` | **a size is wrong** | a good packet in order that found no room in its queue. With the window no more than half the queue it feeds it stays 0 |

- **A packet made wrong is reported at the end that received it,** not at the one that made it: that one only hears
  "I have started again". `test_hostlink` shows it on a chain of 17, one too long for hops.

**What it does not promise:**

- **A word that goes bad in a queue or a buffer** goes out with a checksum that matches it, and nobody is told. The
  checks cover the wire, not the memory (HL, "The chain").
- **A damaged packet that passes both CRCs** by chance, 1 in 2³² of the damaged ones for each check, is taken as
  good. HL has what each such case does.
- **A line that reads as zeros for good** looks idle: the end stays in step and waits. `status().in_step` does not
  show it.

**How long things take** (cycles, an idle link, no damage; `build/linkcost pass`, `start`):

| | cycles |
|---|---|
| both ends from power-on to a link in step | 4,657 |
| a packet of one word, pushed to takeable at the other end | 34 on average, 48 at most |
| a packet of 32 words | 561 on average, 574 at most |
| each ASIC a packet passes through | its length again: 16 for the header, 16 a word, and one cycle a word for the check before it leaves |

## What is here

| file | what |
|---|---|
| `crc32c.h`, `header.h` | CRC-32C a byte a cycle; the header packed and unpacked; the mask on the data; the restart packet's data; the packet numbers' arithmetic |
| `link_params.h` | the sizes; **HL's rules, each a switch** (`Rules`), so that a kept case can show what each is for; the planted bugs (`Plant`) |
| `hostlink.h`, `.cpp` | **the interface for whoever builds on the link:** `End` and `Node`, with their queues. `examples/` has the two programs above |
| `link_end.h`, `.cpp` | **the link end, the model of the hardware:** its registers in one struct, one `step()` a cycle, a byte in and a byte out. `HARDWARE.md` is this file as hardware |
| `node.h`, `.cpp` | **the chain node:** two link ends and the forwarding buffers between them |
| `archive.h` | registers to bytes and back; the 128-bit hash the search knows a state by |
| `bench.h`, `.cpp` | one link on the bench: two ends, the wires with their damage, queues, sources, the monitor; the kept cases' format and player |
| `chain.h`, `.cpp` | a chain on the bench: the host, the nodes, a wire each way on every link, the monitor |
| `linksearch.cpp` | the search of the shrunken model, on one link or a chain (`--chain N`) |
| `linkrun.cpp` | the random runs: every wire, consumer and source a process drawn from the seed |
| `linkcase.cpp` | plays kept cases |
| `linkcost.cpp` | what the choices cost, at the real sizes, nothing drawn at random: one run of numbers, the shared window, the pass over the data, a packet made wrong, coming up |
| `test_hostlink.cpp` | the interface used only as its header says: what a user may rely on, each as a test; the counts and an error's causes |
| `test_reset.cpp` | an ASIC reset in the middle of a running chain, on the interface alone: `build/test_reset` makes 1,380 runs in about 9 minutes and prints a table; `make check` runs a few |
| `linkhw.cpp` | the link end and the node as hardware: every register with its flip-flops, every memory with its bytes, the budget. Prints the tables of `HARDWARE.md` |
| `test_header.cpp`, `test_link.cpp` | HL's example headers and CRC values; the link on a whole wire at the real sizes, and its figures |
| `cases/`, `make_cases.sh` | a case for each flaw: it fails under the rules it names and holds under the fix; the script finds them again |
| `searches.sh`, `plants.sh`, `plants_run.sh`, `figures.sh` | the searches as a table; each planted bug against the search and against the runs; the runs that put figures to HL's proposed numbers |
| `rates.py`, `sum_runs.py` | how often damage passes a CRC unseen, counted; the totals of several runs added up |

## Building and running

```
make                      # build/: the programs, optimized (g++)
make check                # the tests and the kept cases: g++ with the address and undefined-behaviour sanitizers, and clang++
make lib                  # build/libhostlink.a
make check VECTORS=<path to gold's VECTORS.txt>     # also every packet of gold's vectors (on updater-w7 until merged)
```

| to | run |
|---|---|
| see a flaw happen, line by line | `build/linkcase -t cases/stopped-blocks.case` |
| search the link as HL has it | `build/linksearch --damage 2 --packets 1 0 1 0` |
| search it with one stream never taken again | `build/linksearch --packets 1 3 0 0 --queue 4 --damage 1 --hold 3` |
| search a chain | `build/linksearch --chain 2 --cpackets 1 0 1 --damage 1` |
| run at random | `build/linkrun --seeds 1 100 --cycles 300000`; one seed again: `--seed 47 -v` |
| the table of searches | `./searches.sh main`, `held`, `starve`, `rules`, `window`, `forged`, `nested`, `chain`; `big1` to `big16` are larger, the runner's |
| the planted bugs | `./plants.sh`, `./plants_run.sh` |
| the figures | `./figures.sh a` to `e`; `build/linkcost`; `python3 rates.py` |

**`--rules`** is `hl`, then `+name` or `-name` for one rule (`link_params.h` has the names) and `,size=value` for a
size: `hl-flush`, `hl+stop_holds_again`, `hl,window=4,number_bits=4`.

**`linksearch`'s budgets:** `--packets a b c d` (each source's packets: end 0 foreground and background, end 1 the
same), `--damage`, `--noise`, `--forge`, `--power`, `--flip` (how many of each may happen), `--queue`, `--delay`,
`--tick`, `--hold`, `--images`, `--images2`, `--images3`, `--images4`, `--wrong`, `--fates`, `--whole`, `--all`,
`--deep`, `--max-states`, `--seconds`, `--out <case file>`. Its heading comment says what each is.

## A case file

```
sizes number_bits=3 max_words=1 window=2 zeros=40 flush_zeros=32 status_timer=24 queue=4 reask=96 pass=16 restart_words=1 delay=1 words=1 images=0 window0=0 wrong=0 still=0
packets 1 3 0 0                 # each source's packets
offer 0 2 233                   # end 0 lets its next background packet out at cycle 233
fate 0 7 gone                   # the seventh packet end 0 sends is turned to zeros
hold 2                          # from "quiet" on, the background is never taken out of its queues again
quiet 329 20000                 # from cycle 329 nothing more goes wrong: it must finish within 20,000 cycles
run hl+stop_holds_again fail the background is never taken again
run hl ok
```

The fates: `gone`, `first0`, `header`, `data`, `head0` (the header reads as zeros, the data is whole), and the
forged ones (`forge-ack`, `forge-stop`, `forge-stop-bg`, `forge-lost`, `forge-recovery`, `forge-resend`,
`forge-number`, `forge-number-back`, `forge-ack-data`, `forge-wrong`, `forge-long`, `forge-restart`), which change
a field and seal the header again. A case with a line `chain N` is of a chain, and names sources, queues and wires by `chain.h`'s numbers.
Other events: `noise <wire> <cycle>`, `power <end> <cycle>`, `flip <end> <cycle>`, `drain <end> <stream> <cycle> <words>`.
