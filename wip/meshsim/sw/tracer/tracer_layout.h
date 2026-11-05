/* C&C 3's big SRAM for the Tracer's firmware (tracer_ctl.c; meshsim B1, WORKER22): the contract between the harness that
 * stands in for C&C 1 and C&C 2 (the segment's frames, the tables, the templates; landed before C&C 3 boots) and the
 * firmware. Chunk addresses (one chunk = one 32-bit word, 4.1); every region starts on a 128-bit boundary.
 *
 *   chunk      region
 *   0x00000    header: 32 words (below)
 *   0x00040    tables: the decode table (256 x 128-bit words) then the ROM (512): one run to the Tracer's 0x1000
 *   0x00C40    template index: 16 B per template number, <= 128 (the replayer's, replay_layout.h)
 *   0x00E40    the segment's record: 68 x 128-bit words (frame 2's state, the advice count, word 66, word 67 0)
 *   0x00F80    the last window's record, as the 64th run's output queue 0 delivers it to C&C 3 itself (Q4)
 *   0x010C0    the end state expected (the next segment's frame 2 as words 0-65), or none (header word 8 = 0)
 *   0x02000    the segment's advice: 64-bit values, two per 128-bit entry, low value first
 *   0x58000    the controller-fed template words (128 bits each), as replay_layout.h
 * The header gives every region's address, so the placements are defaults. The tiles' replay regions are replay_layout.h's:
 * header words 16-19 give the record and stream chunks every tile uses.
 */
#ifndef TRACER_LAYOUT_H
#define TRACER_LAYOUT_H

#define TL_MAGIC             0x52435254u   /* "TRCR" */
#define TL_VERSION           1u
#define TL_H_MAGIC           0
#define TL_H_VERSION         1
#define TL_H_TABLES          2
#define TL_H_INDEX           3
#define TL_H_NTEMPLATES      4
#define TL_H_TEMPLATES       5
#define TL_H_RECORD          6
#define TL_H_OWN_RECORD      7    /* where the last window's queue 0 lands in C&C 3's own big SRAM */
#define TL_H_EXPECT          8    /* the expected end state (words 0-65), or 0: no check */
#define TL_H_ADVICE          9
#define TL_H_ADVICE_N        10   /* 64-bit advice values */
#define TL_H_RUN_CONFIG      11
#define TL_H_HASH_MUL        12
#define TL_H_HASH_STEP       13
#define TL_H_HASH_LINES      14
#define TL_H_WINDOWS         15   /* runs in the segment: 64 */
#define TL_H_T_RECORD        16   /* the tiles' replay region (replay_layout.h): the record's chunk */
#define TL_H_T_CODE          17
#define TL_H_T_RLOG          18
#define TL_H_T_WLOG          19
#define TL_H_FIRST_TILE      20   /* window n goes to tile FIRST_TILE + n: 0 */
#define TL_H_WORDS           32

/* results: tr_results[16] in data memory (the harness finds it in the .sym file) */
#define TL_R_STATUS          0    /* 1 ok; 2 a template missing; 3 ERRORS set; 4 a bad header; 5 advice left over; 6 the end state differs */
#define TL_R_WINDOWS         1    /* runs completed */
#define TL_R_NEWEXP          2    /* new expansions served */
#define TL_R_ADVICE_RUNS     3    /* advice refills */
#define TL_R_TRANSFERS       4    /* DMA transfers */
#define TL_R_MISSING         5    /* the word with no template */
#define TL_R_ERRORS          6    /* the Tracer's ERRORS at the end */
#define TL_R_ADVICE_LEFT     7    /* the advice field at the end */
#define TL_R_END_DIFF        8    /* the first record word (0-65) that differs from the expected end state, + 1; 0 none */
#define TL_R_CONDITIONS      9    /* the controller's ERRORS */

#endif
