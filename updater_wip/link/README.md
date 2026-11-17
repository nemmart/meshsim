# updater_wip/link: the host link's protocol, on its own

WORKER8's work (`../planning/sessions/WORKER8/`): the link of `v1.4_wip/planning/HOST-LINK.md` ("HL") built as it is
written, put on wires that misbehave, and searched. No chip, no mesh, nothing of `v1.37_wip/`.

**Where it stands (2026-11-17, evening):** on HL as it now stands: the header of 16 bytes, one run of packet numbers
for a link, "stop" holding only packets not yet numbered. **Stopped a second time: a header that reads as zeros in
front of data that holds a packet** (`../planning/sessions/WORKER8/REPORT.md`, `cases/packet-in-data.case`).
One link is searched, the planted bugs are caught, and a case is kept for every rule. The chain's searches, the
random campaign and the figures (`figures.sh`, `linkcost`) are not yet run on this code; `rates.py` and
`sum_runs.py` are still of the header before.

## What is here

| file | what |
|---|---|
| `crc32c.h`, `header.h` | CRC-32C a byte a cycle; the header packed and unpacked; the restart packet's data; the packet numbers' arithmetic |
| `link_params.h` | the sizes; **HL's rules, each a switch** (`Rules`), so that a kept case can show what each is for; the planted bugs (`Plant`) |
| `link_end.h`, `.cpp` | **the link end:** its registers in one struct, one `step()` a cycle, a byte in and a byte out. Kept for the chip's Ingress/Egress model |
| `node.h`, `.cpp` | **the chain node:** two link ends and the forwarding buffers between them |
| `archive.h` | registers to bytes and back; the 128-bit hash the search knows a state by |
| `bench.h`, `.cpp` | one link on the bench: two ends, the wires with their damage, queues, sources, the monitor; the kept cases' format and player |
| `chain.h`, `.cpp` | a chain on the bench: the host, the nodes, a wire each way on every link, the monitor |
| `linksearch.cpp` | the search of the shrunken model, on one link or a chain (`--chain N`) |
| `linkrun.cpp` | the random runs: every wire, consumer and source a process drawn from the seed |
| `linkcase.cpp` | plays kept cases |
| `linkcost.cpp` | what the choices cost, at the real sizes, nothing drawn at random: one run of numbers, the shared window, the pass over the data, a packet made wrong, coming up |
| `test_header.cpp`, `test_link.cpp` | HL's example headers and CRC values; the link on a whole wire at the real sizes, and its figures |
| `cases/`, `make_cases.sh` | a case for each flaw: it fails under the rules it names and holds under the fix; the script finds them again |
| `searches.sh`, `plants.sh`, `plants_run.sh`, `figures.sh` | the searches as a table; each planted bug against the search and against the runs; the runs that put figures to HL's proposed numbers |
| `rates.py`, `sum_runs.py` | how often damage passes a CRC unseen, counted; the totals of several runs added up |

## Building and running

```
make                      # build/: the programs, optimized (g++)
make check                # the tests and the kept cases: g++ with the address and undefined-behaviour sanitizers, and clang++
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
`--tick`, `--hold`, `--images`, `--images2`, `--images3`, `--wrong`, `--fates`, `--whole`, `--all`, `--out <case file>`. Its heading comment says what each is.

## A case file

```
sizes number_bits=3 max_words=1 window=2 zeros=40 flush_zeros=32 status_timer=24 queue=4 reask=96 pass=1 restart_words=1 delay=1 words=1 images=0 window0=0 wrong=0 still=0
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
