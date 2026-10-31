/* The Stage B replay layout in big SRAM (2 MB), the contract between the Java-side loader (which writes the window's
 * files into big SRAM before the tile runs) and replay_ctl.c. All addresses are big-SRAM chunk addresses (one chunk =
 * one 32-bit word, 4.1); every region starts on a 128-bit (4-chunk) boundary.
 *
 *   chunk      bytes        region
 *   0x00000    0x000000     header: 16 words (below)
 *   0x00040    0x000100     tables: the decode table (256 x 128-bit words) then the ROM (512 words), 12 KB, in the
 *                           order mode 9 writes them: one run to 0x00400 covers both (0x400 + 4 x 256 = 0x800)
 *   0x00C40    0x003100     template index: 16 B per template number, as the tracer delivers it, <= 128
 *   0x00E40    0x003900     the window's state record: 67 x 128-bit words (word 0 the bitmap, 1-66 region 0)
 *   0x01000    0x004000     controller-fed template words (128-bit each), up to 112 KB
 *   0x08000    0x020000     code stream (128-bit entries), up to 256 KB (65,536 raw words)
 *   0x18000    0x060000     read log, up to 512 KB
 *   0x38000    0x0E0000     write log, up to 512 KB
 *   0x58000    0x160000     free (to 0x7FFFF, the end of 2 MB)
 *
 * The header gives every region's chunk address and size, so the placements above are defaults, not rules.
 */
#ifndef REPLAY_LAYOUT_H
#define REPLAY_LAYOUT_H

#define RL_MAGIC             0x594C5052u   /* "RPLY" */
#define RL_VERSION           1u

/* header word indexes */
#define RL_H_MAGIC           0
#define RL_H_VERSION         1
#define RL_H_TABLES          2   /* chunk of the decode table + ROM (768 x 128-bit) */
#define RL_H_INDEX           3   /* chunk of the template index */
#define RL_H_NTEMPLATES      4   /* index entries, 0-128 */
#define RL_H_TEMPLATES       5   /* chunk of template word 0 */
#define RL_H_RECORD          6   /* chunk of the state record (67 x 128-bit) */
#define RL_H_CODE            7   /* chunk of the code stream */
#define RL_H_CODE_N          8   /* its 128-bit entries */
#define RL_H_RLOG            9
#define RL_H_RLOG_N          10
#define RL_H_WLOG            11
#define RL_H_WLOG_N          12
#define RL_H_PASSES          13  /* passes to run (each a full 65,536-row pass from the same checkpoint) */
#define RL_H_WORDS           16

/* results: the program writes them to data memory, uint32_t rp_results[16] (the harness finds it in the .sym file) */
#define RL_R_STATUS          0   /* 1 ok, 2 a presented instruction has no template, 3 a condition was raised */
#define RL_R_PASSES          1   /* passes completed */
#define RL_R_CONDITIONS      2   /* TILE_CONDITIONS at the end */
#define RL_R_NEWEXP          3   /* new-expansion events served */
#define RL_R_REFILLS         4   /* refill runs */
#define RL_R_MISSING         5   /* the instruction word that had no template, if any */
#define RL_R_SLOW            6   /* new expansions served by the slow path (first of a pass, NOP policy, table miss) */
#define RL_R_DM_MULT         7   /* the direct-mapped table's multiplier */
#define RL_R_DM_COLLISIONS   8   /* words that share a slot (they take the slow path); 0 normally */
#define RL_R_LAST_HITS       9   /* REPLAY_LAST_WORD builds: expansions whose word equals the previous one */
#define RL_R_DM_TRIES        10  /* multipliers tried */

/* the template index entry (16 B), as the tracer delivers it; the count is the file size / 16 (the loader puts it in
 * the header's RL_H_NTEMPLATES) */
#define RL_IX_WORD           0   /* the exact instruction word, as PERIPH_STATUS_1 presents it */
#define RL_IX_OFFSET         1   /* in 128-bit words from RL_H_TEMPLATES */
#define RL_IX_LENGTH         2   /* in words */
#define RL_IX_META           3   /* byte 12: rd0 (0 none, 1 NOP virtual, 2 remap, 3 NOP not virtual); byte 13: decode class (1, 2) */
#define RL_IX_RD0(meta)      ((meta) & 0xFFu)
#define RL_IX_CLASS(meta)    (((meta) >> 8) & 0xFFu)

#endif
