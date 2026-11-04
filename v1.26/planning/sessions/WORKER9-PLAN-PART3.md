# WORKER9 part 3 plan (proposed, awaiting the owner's go-ahead): the delivery writer

A functional model of the Tracer's output (v1.3 s1-8, spec 4.19.2), in PR #2 next to the tracer and the table builder
(`tracer/delivery/`), from jolt-cpp 821e1bd, writing exactly `REPLAY-INPUT-REQUIREMENTS.md`'s files (with the
universal tables and the normalized template-index key).

1. **`delivery_writer`** (C++, linked with the tracer like `capture_writes`): generate the compressed trace, expand it
   slice by slice, and for each selected window of 2^16 rows (or a debug `window_rows`) write:
   - **reference rows** `rows.bin`: 80-byte 4.19.12 records from jolt-cpp's rows (`expand_compressed_chunk`), with
     `instr_idx` and the slot kinds from the bytecode's `VirtualInstruction`; padding rows zero; `mem_value` per 4.19.12
     (now for `VirtualLoad*`/`VirtualStore*` too);
   - **state record** (1,072 B): template bitmap in the replayer's numbering (`template_map.txt`), state word (pc,
     `instr_idx`, next offset = rows of the current expansion before the window, flags), stream word, the 128-register
     image at the window start (tracked by applying every row's rd write from the program's initial registers);
   - **three streams**, raw and LRU-compressed per 4.19.15 (tables reset per window, move to front, the write log's
     E[15] last-load entry, encoded in row order with the read log), and the Tracer's pick (two passes: per stream the
     smaller form), each as its own state-record variant;
   - code stream: decompressed words with bit 0 the compressed flag, padding words, the mid-expansion first entry;
     read log: `LD`/`VirtualLoad*` values, every advice value (DIV/REM advice included), the next `instr_idx` of every
     row that transfers control, 0 for the program-end row; write log: `SD`/`VirtualStore*` pre-values (store log on);
   - **diagnostics** (3.4): per instruction its code-stream index, row count and decode class; the end state; and
   - **manifest.json**: commits (jolt-cpp, PR #2, the tables' SHA-256), program and inputs, per-window files with sizes
     and SHA-256, flags and bit lengths, counts, template numbers, the rows' SHA-256.
2. **Self-check in the writer**: decode every compressed stream back and compare with the raw one; consecutive windows'
   records agree (window k's end state = window k+1's record); stream lengths consumed exactly by a log-driven walk of
   the rows (each row's log words in order). A row-by-row replay belongs to v1.25's replayer (W1), whose opcode
   numbering changes with these tables; I can add a C++ functional replay from the tables (resolver + jolt-cpp's
   `execute_instruction` on the log path) if you want the deliveries proven before W1 runs them.
3. **Small inputs first**: PR #1's EVM Zone trace-23 and Cyclone's smallest input (input/22), every window; then the
   2^29 runs' **selected windows** (section 4's set: consecutive ordinary windows, mid-expansion starts in a ROM and a
   controller-fed template, long inline expansions, atomics and rd = x0, program end, all padding, densest logs, both
   stream forms).
4. **Full-program deliveries** on the Quest runner: about 8,192 windows per 2^29 program, ~5 MiB of rows each, so ~40 GB
   plus streams per program; a held task with the writer in a hand-copied tarball (as 069), outputs left on the box;
   I report the sizes, wall time and manifest checksums. Nothing is committed.
