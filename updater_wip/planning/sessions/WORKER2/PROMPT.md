# Worker 2 (updater_wip): the packetizer

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. The host sends each ASIC the program, a proof's inputs and its memory changes as **frames**
(`v1.4_wip/planning/HOST-FRAMES.md`). Gold, Jolt's tracer in Java, already writes them (WORKER1;
`updater_wip/pipeline/gold/`). On the wire the frames ride in **packets** of a link with two streams, a 128-bit
header word and two checks: **`v1.4_wip/planning/HOST-LINK.md`**.

**Your part: the packetizer.** For v1.37 the simulator is started with one file, the data that comes over the link,
and its Ingress is a black box that reads that file. You make the file: gold's frame file wrapped in the link's
packets, with the right check values. A second worker (WORKER3) writes the Ingress black box in C++ **from the
documents, not from your code**, so the two implementations check each other. Your files and test vectors are its
test input.

READ FIRST: the top-level `README.md`; `updater_wip/README.md`; **`HOST-LINK.md` in full**;
`v1.4_wip/planning/INGRESS-INTERFACE.md` ("Terms", "Frames", "In v1.37: a black box and a file"); `HOST-FRAMES.md`
("Every frame"); `updater_wip/pipeline/gold/README.md` and its `FrameWriter.java` and `FrameReader.java`;
`updater_wip/planning/sessions/WORKER1-REPORT.md`.

THE WORK, all in `updater_wip/pipeline/gold/`
1. **The packetizer:** a command that takes a frame file and writes a packet file.
   - Every packet is on stream 2, the background.
   - Each frame starts a new packet. A frame's packets hold 32 words of data but its last, which holds what
     remains. A frame ends at the end of a packet.
   - Packet numbers run from 1 on the stream, and after 0xFFFFE comes 1.
   - The acknowledge field, the flags and the reserved bits are 0.
   - The data checksum is over the packet's data; the header CRC is over the header word's bits [95:0], the data
     checksum included. Every multi-byte value is least significant byte first, and a checksum runs over the bytes
     in the order they go on the link.
2. **The reader:** a command that reads a packet file, checks every packet, and writes the frames back out.
   - It checks: the first byte is not zero; the stream is 1 or 2; the length is 1 to 32; the reserved bits are 0;
     both checks; the packet numbers in order on each stream; that each frame ends at the end of a packet.
   - The frames it writes are byte-identical to the frame file the packetizer was given.
   - It prints what it found: packets, words, frames, and the overhead against the frame file.
3. **Test vectors:** a small text file in the repository, a few packets in hex with every field and both check
   values spelled out, and one or two small whole packet files. WORKER3 tests its Ingress against these.

WHAT THE DOCUMENTS LEAVE OPEN, AND WHAT TO TAKE
- **The checks are CRC-32C** (†): the standard one, polynomial 0x1EDC6F41, reflected, initial value and final XOR
  0xFFFFFFFF, whose check value for the ASCII string "123456789" is 0xE3069283. Java has it (`java.util.zip.CRC32C`).
  Write the exact definition and the byte order in your `NOTES.md` and in the vectors file, so that a C++ reader of
  the document alone gets the same values.
- **Zeros between packets are idle** (†): a zero byte where a header's first byte is expected is skipped, in any
  number. The packetizer writes none by default; give it an option that puts a chosen number of zero bytes between
  packets, for the tests. The reader accepts them.
- **A packet of length 0** is legal on the link and carries no data. The packetizer writes none; the reader accepts
  one and skips it.
- Anything else the documents do not say and you must settle: take the simplest reading, and record it in `NOTES.md`
  under "Choices", with the reason and the alternative.
- **Do not edit anything in `v1.4_wip/`.** If you believe a document is wrong, say so in `QUESTIONS.md` with the
  evidence, and carry on.

HOW IT IS CHECKED
- **Round trip:** packetize, read back, and the frames are byte-identical, for every frame file gold's tests make:
  the guests, the small inputs, and both workloads at 2²⁹ on the runner.
- **Damage is caught:** change one bit or one field of a good packet file at a time (the first byte, the stream, the
  length, a packet number, each check, a data byte, a reserved bit) and the reader refuses it, saying which check
  failed.
- **The wrap of the packet number,** with a made-up file long enough to pass 0xFFFFE, or a packetizer option that
  starts the numbering near it.
- **Idle zeros,** in several counts including ones that are not a multiple of 16.
- **Nothing else changed:** gold's existing tests pass, and the frame files are byte-identical to before.

NOT YOUR WORK: the simulator, the firmware, Ingress; sending again, "lost", recovery and "stop", which need a way
back to the host; the foreground stream's contents.

RULES
- **Propose your plan first** in your `NOTES.md` and wait for the owner's go-ahead before writing code: the command
  lines, the vectors file's format, the choices you already see.
- Gold stays as it is: plain Java 21, no dependencies, its own build script. The frames' commands and their output
  are unchanged.
- **Branch `updater-w2`**; merge `main` in often (never rebase); the integrator merges into `main`. Your notes,
  questions and report are in `updater_wip/planning/sessions/WORKER2/`. Frozen folders are never edited.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there). **Quest numbers:** pull Quest and re-read
  `NUMBERS.md` immediately before claiming, and push the claim before writing the task; on a rejected push, pull and
  take the next free number. WORKER1's task and its generator are a pattern (`WORKER1-tasks/`).
- Clone with the token from your user preferences, or attach the repository to the session. **Never write a token
  into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

DELIVERABLES
- The two commands and their tests in gold's build; gold's README updated with how to run them.
- The vectors file and the small packet files, where WORKER3 can find them; say where in your report.
- `WORKER2/NOTES.md`, with the plan, the exact definition of the checks and the "Choices" list.
- `WORKER2/REPORT.md`: what was built, each check's result, the packet files' sizes and overhead for both
  workloads at 2²⁹, and what you would change in `HOST-LINK.md`, as a list for the owner.
