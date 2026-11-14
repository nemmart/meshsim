// guest.h: a synthetic guest for the block-level bench (tracer_block.h), so that the Tracer runs with no data from
// outside this folder: `tracer_block --synthetic [SEED]`.
//
// THE GUEST is a real RV64IMAC program, assembled here: an AUIPC for the data's address; two CSR instructions, core-fed
// (CSRRW and CSRRS of mtvec); stores and loads of every width (SD, SW, SH, SB; LD, LW, LWU, LH, LHU, LB, LBU); a
// multiply; a divide and an unsigned remainder, each with its advice; branches taken and not taken; a JAL and its
// return; an AUIPC-JALR pair; a 16-bit instruction in a line's last two bytes and a 32-bit one straddling the next
// line's end; 16-bit instructions (c.li, c.addi, c.mv, c.add); a loop of loads, stores, a multiply and a divide, some
// hundreds of iterations, so that more than 256 advice values pass the read queue; a loop of stores over 200 lines,
// more than the data cache holds, so that dirty lines are written back; the program's end, and padding after it. Its
// memory file (code pages another's, data pages its own), its jump-target map (every instruction's base, built as the
// host builds it: keys ascending, each in the first of its 3 probe lines with a free entry), its record and its advice
// are built with it. The seed sets the loops' counts, the data and the registers' start.
//
// IT RUNS through tracer_block::Bench, the RTL acceptance's bench, in runs of 3 to 512 rows (RUN_CONFIG stored before
// each), run boundaries inside expansions among them, to the program's end and a run of padding: at a fill read's
// latency 1 with the streams raw and with tracer_compression 7, and on the simulator's memory system (17.9.14).
//
// WHAT IT IS CHECKED AGAINST is not the simulator. An interpreter of the program written here (`Machine`: the RV64
// instructions' semantics, the advice by appendix G's formulas) gives each instruction's code value, its loads, stores,
// advice and transfer; the universal tables (`data/tables/replayer`, data) give each instruction's rows and which of
// them takes each. From them, every run:
//   - output queue 0: the state word (pc, instr_idx = the instruction's base + the row offset, the row offset); the
//     stream word's values and words; the advice field; x1-x31 where the run ends between instructions; word 66 as
//     written; word 67, 0;
//   - output queues 1-3, decoded (raw, or LRU-coded by 17.8.12's decoders written here): the code values in order,
//     the run's first again inside an expansion, 0 for a padding row; the read log (each load's doubleword, each
//     advice value, each control transfer's base from the map, 0 at the program's end); the write log (each store's
//     doubleword as read);
//   - each post's code value and row offset; ROWS_RUN, STATE_LENGTH, the length words, STATUS 0, ERRORS 0, no live event;
//   - STAT_MISS_I, STAT_MISS_D and STAT_PROBES against true-LRU models over the interpreter's fetches, memory rows and
//     transfers, and the fill port's reads against them;
//   - the cycles adding up (17.9.8): from START to the rows' end, the rows' schedules (4, or 6 for an M row) + the
//     three stall counters + the waits on the core;
//   - and a closed form, by hand, for every run with no miss, no probe, no core-fed instruction, no NOP policy and no
//     wait on the core (a "quiet" run): the cycles from START to the rows' end are 4 a row, 2 more an M row, 5 for
//     each row that sets its successor's pc (but the run's last), 6 to the first instruction (3 inside an expansion,
//     and 5 to a first padding row, with nothing to fetch), and the measure's 3 (17.9.7, 17.9.17; Snarky One's
//     tracer_unit README, The rows and their schedule; the 3 and the 5, which neither states, are the RTL's reference
//     figures: tests/tracerblock/rtl/, Cyclone's runs 1 and 42).
// At the end, the guest's memory, the data cache's dirty lines written back, against the interpreter's.
#pragma once

#include <cstdint>

namespace tracer_block {

// Runs the synthetic guest of `seed` in its three settings; prints each run's figures and the reports. 0: every check
// passed.
int synthetic(uint64_t seed, bool quiet);

}  // namespace tracer_block
