# updater_wip/link: the host link's protocol, on its own

WORKER8's work (`../planning/sessions/WORKER8/`): the link of `v1.4_wip/planning/HOST-LINK.md` ("HL") built as it is
written, put on wires that misbehave, and searched. No chip, no mesh, nothing of `v1.37_wip/`.

**Where it stands (2026-11-17):** one link is built and searched. The chain node, the host end, the random campaign
and the runner's tasks are still to come. What the search found is `../planning/sessions/WORKER8/QUESTIONS.md`.

## What is here

| file | what |
|---|---|
| `crc32c.h`, `header.h` | CRC-32C a byte a cycle; the header word packed and unpacked; the packet numbers' arithmetic |
| `link_params.h` | the sizes; **the rules taken where HL is silent, each a switch** (`Rules`); the planted bugs (`Plant`) |
| `link_end.h`, `.cpp` | **the link end:** its registers in one struct, one `step()` a cycle, a byte in and a byte out. Kept for the chip's Ingress/Egress model |
| `archive.h` | registers to bytes and back; the 128-bit hash the search knows a state by |
| `bench.h`, `.cpp` | one link on the bench: two ends, the two wires with their damage, queues, sources, and the monitor that knows what every delivered word must be; the kept cases' format and player |
| `linksearch.cpp` | the search of the shrunken model: every order of loss, damage, stall and delay within a budget |
| `linkcase.cpp` | plays kept cases |
| `test_header.cpp`, `test_link.cpp` | HL's example headers and CRC values, gold's vectors; the link on a whole wire at the real sizes, and its figures |
| `cases/` | a case for each flaw found: it fails under the rules it names and holds under the fix |
| `searches.sh`, `plants.sh` | the searches as a table; each planted bug against the search |

## Building and running

```
make                      # build/: the four programs, optimized (g++)
make check                # the tests and the kept cases: g++ with the address and undefined-behaviour sanitizers, and clang++
make check VECTORS=<path to gold's VECTORS.txt>     # also every packet of gold's vectors (on updater-w7 until merged)
```

| to | run |
|---|---|
| see a flaw happen, line by line | `build/linkcase -t cases/g9-both-lost.case` |
| search the link as HL has it | `build/linksearch --rules as_written --damage 1` |
| search it with the proposed rules | `build/linksearch --rules proposed --damage 2 --packets 1 0 1 0` |
| the table of searches | `./searches.sh main`, `rules`, `window`, `forged`; `big` is larger than this machine |
| the planted bugs | `./plants.sh` |

**`--rules`** is `as_written` (the least that makes HL run) or `proposed`, then `+name` or `-name` for one rule
(`link_params.h` has the names) and `,size=value` for a size: `proposed-flush`, `as_written+resend_ahead,window=4`.

**`linksearch`'s budgets:** `--packets a b c d` (each source's packets: end 0 foreground and background, end 1 the
same), `--damage`, `--noise`, `--forge`, `--power`, `--flip` (how many of each may happen), `--queue`, `--delay`,
`--tick`, `--images`, `--fates`, `--whole`, `--all`, `--out <case file>`. Its heading comment says what each is.

## A case file

```
sizes number_bits=3 max_words=1 window=2 zeros=40 flush_zeros=32 status_timer=24 queue=4 delay=1 words=1 images=0
packets 2 0 2 0                 # each source's packets
offer 0 1 0                     # end 0 lets its next foreground packet out at cycle 0
fate 0 1 gone                   # the first packet end 0 sends is turned to zeros
drain 1 1 96 0                  # end 1's foreground queue is emptied at cycle 96 (0: all of it)
quiet 32 20000                  # from cycle 32 nothing more goes wrong: it must finish within 20,000 cycles
run as_written fail nothing more goes wrong, and it does not finish
run as_written+resend_ahead ok
```

The fates: `gone`, `first0`, `header`, `data`, and the forged ones (`forge-ack`, `forge-stop`, `forge-lost`,
`forge-recovery`, `forge-resend`, `forge-number`, `forge-ack-data`), which change a field and seal the header again.
Other events: `noise <wire> <cycle>`, `power <end> <cycle>`, `flip <end> <stream> <cycle>`.
