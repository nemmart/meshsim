# The change guide from v1.36 to v1.37

*For whoever reads v1.37's specification against v1.36's, for the RTL engineer, and for the session that brings
v1.37's simulator and firmware to the specification. Section numbers are v1.37's unless they say "v1.36's".*

## 1. In brief

**v1.37 is v1.36, the RTL's hardware, with the full system around it.** v1.36 took the RTL's specification as
meshsim's; v1.37 specifies what the RTL pencilled in or left out, designed in `v1.4_wip/planning/` with the owner
over 2026-11-14 to 2026-11-18, and the code of three machines built from that design:

- **the host link** (28): link packets with two checks and a mask, one run of numbers a link, sending again,
  recovery, starting a link again, "stop" for each stream, a chain of ASICs, and the link end as hardware;
- **the frames** (29) the host sends, and the reply;
- **Ingress/Egress's host side** (17.6): two incoming queues, the DMA, the forward count to the Updater, Egress;
  v1.36's host interface removed, and the UART redesigned;
- **the Updater** (17.10), the master tile's peripheral 2: it builds the copy-on-write files from the host's update
  words;
- **the memory system** (17.11, 23), the memory tile's peripheral 1: the cache, the AXI arbiter and `lpddr` with IDs;
  the RW engine becomes peripheral 2;
- **the line between the special tiles** (16.1), with a peripheral DMA's input on south (16.4);
- **the master tile's firmware** as a contract (30).

**What the RTL is asked for** is section 3. **Nothing else of v1.36's hardware changes.** Where v1.37's simulator and
firmware are behind, `v1.37/planning/updater/sessions/SPEC/FOR-THE-SIMULATOR.md` lists it.

**Whose word wins** (owner, 2026-11-18): for the cache's controller, the Updater's controller and the link end, the
code (`cache_ctl.*`, `updater_ctl.h`, `v1.37/link/`); for Ingress/Egress, the UART, Egress and the frames, the
design documents; for hardware the RTL has built, the RTL.

## 2. Unit by unit

Each item: what changed, and whether it touches the RTL: **RTL** a change to built hardware, asked of the RTL;
**new** hardware designed here, for the RTL to build; **none** specification or software only.

| Unit | Change | RTL? |
| --- | --- | --- |
| the router (16) | the line: the special tiles' east and west ports join master ↔ memory ↔ tracer, mesh links with no flow control, routed by the ring's rule on the difference of the ids (16.1, 16.2) | RTL (RCR 1) |
| the router (16.4) | a peripheral DMA enters on the south input, on every special tile | RTL (RCR 3) |
| Ingress/Egress (17.6) | version 1; v1.36's host interface gone (words 64–72, `STATUS.stream_last`, `EVENTS.stream_in`, `stream_out`); the standard DMA; two incoming queues of 16 KiB; `FOREGROUND_WORDS`, `BACKGROUND_WORDS`, `DMA_FORWARD_COUNT`, `EGRESS_COUNT` at words 64–67; `STATUS` [2], [3]; `EVENTS` [6]–[8]; `ERRORS` [1]–[3]; the word port; the outgoing queue in region 0x04; the host link's two link ends and four forwarding buffers; the pin `chain_next` | new; the host interface's removal agreed (RCR C) |
| the UART (17.6.9) | `UART_TX` at word 30, `UART_RX` at 31, `UART_CONTROL` at 42 (0 its reset and its value at the reset), its levels and line errors in `STATUS`; `UART_DIVISOR` and `UART_STATUS` gone | RTL (RCR 2) |
| the RW engine (17.7) | the memory tile's peripheral 2: window 0xFFFF_FC00, region 0x05; its `lpddr` port is client 2 of the AXI arbiter. Its hardware unchanged | RTL (RCR 4) |
| the Updater (17.10) | new: the master tile's peripheral 2, type 5 | new |
| the memory system (17.11, 23) | new: the memory tile's peripheral 1, type 6; the cache (23.6), the AXI arbiter (23.11) | new |
| `lpddr` (23.12) | IDs added, one a client | RTL (RCR A) |
| the Tracer (17.9) | none: its `fill` is served by the cache; its segment comes in its load (17.9.10) | none |
| the reset (18) | still one reset; it reaches the host link and Ingress/Egress's queues too | none |
| the master tile's firmware (30) | the frame loop, the Updater's runs, the hand-over, the reply | none |
| the tracer tile's firmware (17.9.10, 30.6) | at its trace's end it prepares a block of 69 words, its figures and its last record; the master tile pulls it by one request, `SEND_END`, and it sends it by one DMA transfer into the reply's buffer | none |

## 3. What is asked of the RTL

`v1.4_wip/planning/RTL-CHANGE-REQUESTS.md` (RCR) has each in full.

| RCR | Change | Status |
| --- | --- | --- |
| 1 | the line between the special tiles, on east and west | agreed in principle (2026-11-16) |
| 2 | the UART's registers | asked (2026-11-17) |
| 3 | a special tile's DMA input on south | to ask |
| 4 | the memory tile's peripherals: the memory system 1, the RW engine 2 | to ask |
| A | `lpddr` with IDs | a comment on a pencilled-in unit |
| B | the AXI arbiter between the clients and the controller | a comment on a pencilled-in unit |
| C | the host interface removed | agreed (2026-11-17) |
| D | where the Updater sits | a floorplan question |
| E | `fill` has no status for an LPDDR error on a read | nothing to change |

## 4. The name table: v1.36 and the planning documents → v1.37

| v1.36, or the planning documents | v1.37 | Where |
| --- | --- | --- |
| a word (the planning documents' 128 bits) | a 128-bit word; "words" in register names | 17.6, 28, 29 |
| a packet (the link's) | a link packet | 28 |
| a frame (the UART's) | a character | 17.6.9 |
| the host interface, `lvds_in`, `lvds_out` | the host link, `host_link`: `link_up`, `link_down` | 3, 28 |
| IEU; Ingress; Egress | Ingress/Egress; Ingress and Egress for its two ways | 17.6 |
| the bus driver, the splitter | the AXI arbiter | 23.11 |
| the cache box, the cache controller | the cache's controller, the cache memory | 23.6 |
| the shared cache and the translator behind `fill` | the cache | 23.6 |
| `End`, `Node` | a link end, a chain node | 28.12 |
| "the pin", "another ASIC follows" | `chain_next` | 18, 28.12 |
| the control row | the line | 16.1 |
| active, background | foreground, background | 28.2 |
| `UART_DIVISOR`, `UART_STATUS` | `UART_CONTROL`, `STATUS` [25:8] | 17.6.9 |
| `STREAM_BYTES`, `STREAM_DEPTH`, `UART_RESET_DIVISOR` | gone | 19 |
| C&C 1, 2, 3 | the master, memory and tracer tiles | 2.1 |
| KB | KiB | |

## 5. Where v1.36's sections went

| v1.36's | v1.37 |
| --- | --- |
| preamble, 1.1, 1.2 | edited: the new parts, the marker *Asked of the RTL*, the new terms |
| 2.1–2.3 | edited: the second peripherals, the line |
| 3 | edited: `lvds_in`, `lvds_out` replaced by `host_link`; `word_port`, `lookup`, `lookup_reply`, `lpddr_client` added; `lpddr` the arbiter's, with IDs |
| 11.5 | the write buffer in region 0x05 |
| 16.1, 16.2, 16.3, 16.4 | the line, its routes and its regime; DMAs on south |
| 17.1, 17.4 | types 5 and 6; Ingress/Egress has a DMA |
| 17.6 | replaced, 17.6.1–17.6.15 |
| 17.7 | edited: peripheral 2, its AXI port a client's |
| — | 17.10 the Updater, 17.11 the memory system: new |
| 18 | the pins `link_up`, `link_down`, `chain_next`, `lpddr`; the one reset reaching the link |
| 19 | the host interface's parameters gone; the new units' added |
| 20 | invariant 34 narrowed; 35–40 new |
| 21.5 | the host interface's row gone; the new units' added |
| 22.27, 22.28 | edited: chapter 30; the new errors; the one reset |
| 23 | 23.1 and the opening rewritten; 23.5 the files; 23.6 the cache, replacing v1.36's model; 23.10–23.13 new |
| 24 | the new units' rows |
| 26 | items 60, 124, 166, 172 changed; 178 new and closed; 179–182 and 191 new; 183–190 in chapters 28 and 29, 192 in chapter 28 |
| `jolt/tracer.md` 17.9.5, 17.9.10, 17.9.14, 17.9.15, 17.9.17, 17.9.18 | the fill port's rule of one handshake, one request; the cache behind `fill`; the segment in the load; the last block pulled by `SEND_END`; items 148 and 174 |
| — | chapters 28, 29, 30 in `host-link.md`, `host-frames.md`, `master-firmware.md`: new |

## 6. Rulings this version records

The owner's rulings of 2026-11-18 on the specification, each stated where it applies (the session's record is
`v1.37/planning/updater/sessions/SPEC/NOTES.md`; how each was reached, `v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md`
section 36):

- one reset, reaching the link; error recovery not designed (item 181);
- the memory system the memory tile's peripheral 1, the RW engine peripheral 2;
- the line on east and west, routed by the ring's rule, with no flow control (item 182); DMAs on south;
- 128 bits "a 128-bit word";
- the types 5 and 6; the Updater's `free_page_credit` packed to `EVENTS` [2]; a bad root or free page UB;
- the UART's fields in `STATUS`; Ingress/Egress's `ERRORS`; its version 1;
- the reply frame (29.14); *N* and *s* the master tile's image's arguments;
- every proposal of the planning documents the session recommended accepting, as a block.
