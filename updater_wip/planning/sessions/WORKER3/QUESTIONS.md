# WORKER3 questions

Each with the evidence and what I take meanwhile. None stops the work.

**All three are answered** (the owner, 2026-11-16): Q1, `HOST-LINK.md` on main now defines the CRC-32C as taken
here; Q2, its own bit, `ERRORS.format_error` (not `crc_error`: built so); Q3, as taken here, listed in the report.

## Q1. HOST-LINK does not define the checks

- **Evidence.** `HOST-LINK.md` says "† CRC-32C for both" and "the header CRC over bits [95:0]", and leaves open
  "what a data checksum over no data is". It gives no polynomial, no initial value, no final XOR, no bit order,
  and does not say in which order a word's bytes go into the check. A C++ reader of the document alone cannot get
  the values.
- **Where the definition is.** In WORKER2's prompt: polynomial 0x1EDC6F41, reflected, initial value and final
  XOR 0xFFFFFFFF, "123456789" gives 0xE3069283; over the bytes in the order they go on the link.
- **What I take.** That one. The header CRC runs over the header word's first 12 bytes; the data checksum over
  the data's bytes; over no data it is 0x00000000, what that CRC gives for no bytes.
- **Asked.** That `HOST-LINK.md` state it, so that the two implementations are checked against the document and
  not against a prompt.

## Q2. A packet whose header CRC is good and whose fields cannot be

- **Evidence.** `INGRESS-INTERFACE.md` has two error bits: `crc_error` for a failed check, `sequence_error` for a
  packet number out of order. A header can pass its CRC with a stream that is not 1 or 2, a length above 32, or a
  reserved bit set. WORKER2's reader is told to refuse each; the interface has no bit for them.
- **What I take.** `crc_error`, and nothing more is presented. The flags and the acknowledge field are ignored:
  they need the way back to the host.
- **The alternative.** A third bit, `ERRORS` [3], for a packet that is not one.

## Q3. No frame says how many rows a segment has

- **Evidence.** The tracer tile runs a segment as a number of windows of `RUN_CONFIG.rows` rows
  (`tracer_layout.h`, `TL_H_WINDOWS` and `TL_H_RUN_CONFIG`), which the harness places today. `HOST-FRAMES.md`
  carries each segment's start state, advice and update words, and not its length.
- **It does not matter for the real configuration:** a segment is 2²² rows, 64 windows of 65,536, the chip's own
  shape.
- **It would matter** for segments cut elsewhere (gold's hard cuts on the small inputs), whose last window is
  short.
- **What I take.** The windows stay the harness's header word: 64, and 8 in the test matrix, with gold's
  `--segment-rows` set to match. Listed in the report under what the frames do not carry.
