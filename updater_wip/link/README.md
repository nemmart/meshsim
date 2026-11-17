# updater_wip/link: the host link's protocol, on its own

WORKER8's work (`../planning/sessions/WORKER8/`): the link of `v1.4_wip/planning/HOST-LINK.md` ("HL") built as it is
written, put on wires that misbehave, and searched. No chip, no mesh, nothing of `v1.37_wip/`.

**Where it stands (2026-11-17, after NEXT.md):** on HL's header of 16 bytes with one run of packet numbers for a
link. **Stopped: the foreground can stand still behind a stopped background**
(`../planning/sessions/WORKER8/REPORT.md`, `cases/stopped-blocks.case`). The link end, the benches, the search and
the random runs are brought up to HL as it stands. Not yet: the cases in `cases/not-yet`, `plants.sh`,
`searches.sh`, `figures.sh`, `make_cases.sh` and `rates.py`, which are of the header before.

## What is here

| file | what |
|---|---|
| `crc32c.h`, `header.h` | CRC-32C a byte a cycle; the header packed and unpacked; the restart packet's data; the packet numbers' arithmetic |
| `link_params.h` | the sizes; **the rules taken where HL is silent, each a switch** (`Rules`); the planted bugs (`Plant`) |
| `link_end.h`, `.cpp` | **the link end:** its registers in one struct, one `step()` a cycle, a byte in and a byte out. Kept for the chip's Ingress/Egress model |
| `node.h`, `.cpp` | **the chain node:** two link ends and the forwarding buffers between them |
| `archive.h` | registers to bytes and back; the 128-bit hash the search knows a state by |
| `bench.h`, `.cpp` | one link on the bench: two ends, the wires with their damage, queues, sources, the monitor; the kept cases' format and player |
| `chain.h`, `.cpp` | a chain on the bench: the host, the nodes, a wire each way on every link, the monitor |
| `linksearch.cpp` | the search of the shrunken model, on one link or a chain (`--chain N`) |
| `linkrun.cpp` | the random runs: every wire, consumer and source a process drawn from the seed |
| `linkcase.cpp` | plays kept cases |
| `test_header.cpp`, `test_link.cpp` | HL's example headers and CRC values; the link on a whole wire at the real sizes, and its figures |
| `cases/`, `make_cases.sh` | a case for each flaw: it fails under the rules it names and holds under the fix; the script finds them again |
| `searches.sh`, `plants.sh`, `plants_run.sh`, `figures.sh` | the searches as a table; each planted bug against the search and against the runs; the runs that answer the owner's questions |
| `rates.py`, `sum_runs.py` | how often damage passes a CRC unseen, counted; the totals of several runs added up |

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
| search a chain | `build/linksearch --chain 2 --cpackets 1 0 1 --damage 1` |
| run at random | `build/linkrun --seeds 1 100 --cycles 300000`; one seed again: `--seed 47 -v` |
| the table of searches | `./searches.sh main`, `starve`, `rules`, `window`, `forged`, `chain`; `big` to `big10` are larger: the last six are the runner's |
| the planted bugs | `./plants.sh`, `./plants_run.sh` |
| the figures | `./figures.sh a`, `./figures.sh b`; `python3 rates.py` |

**`--rules`** is `as_written` (the least that makes HL run) or `proposed`, then `+name` or `-name` for one rule
(`link_params.h` has the names) and `,size=value` for a size: `proposed-flush`, `as_written+resend_ahead,window=4`.

**`linksearch`'s budgets:** `--packets a b c d` (each source's packets: end 0 foreground and background, end 1 the
same), `--damage`, `--noise`, `--forge`, `--power`, `--flip` (how many of each may happen), `--queue`, `--delay`,
`--tick`, `--images`, `--fates`, `--whole`, `--all`, `--out <case file>`. Its heading comment says what each is.

## A case file

```
sizes number_bits=3 max_words=1 window=2 zeros=44 flush_zeros=36 status_timer=24 queue=8 reask=96 delay=1 words=1 images=0 window0=0 wrong=0 still=0
packets 2 0 2 0                 # each source's packets
offer 0 1 0                     # end 0 lets its next foreground packet out at cycle 0
fate 0 1 gone                   # the first packet end 0 sends is turned to zeros
drain 1 1 96 0                  # end 1's foreground queue is emptied at cycle 96 (0: all of it)
quiet 32 20000                  # from cycle 32 nothing more goes wrong: it must finish within 20,000 cycles
run as_written fail nothing more goes wrong, and it does not finish
run as_written+resend_ahead ok
```

The fates: `gone`, `first0`, `header`, `data`, and the forged ones (`forge-ack`, `forge-stop`, `forge-lost`,
`forge-recovery`, `forge-resend`, `forge-number`, `forge-number-back`, `forge-ack-data`), which change a field and
seal the header again. A case with a line `chain N` is of a chain, and names sources, queues and wires by `chain.h`'s numbers.
Other events: `noise <wire> <cycle>`, `power <end> <cycle>`, `flip <end> <stream> <cycle>`.
