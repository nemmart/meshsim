/* master_layout.h: v1.37's master tile program (master.c) and the harness that stands in for the host and the board:
 * the master tile's scratchpad, what the harness fills before the reset ends, and the results it reads. Word addresses
 * (5.4: one word is 32 bits); every area starts on a row (128 bits).
 *
 *   word       area
 *   0x00000    the images the master loads (the compute tiles' and the tracer tile's, and the load's COUNT row), where
 *              boot_desc says: the harness's, as for cc1.c
 *   0x40000    the page map: 128 KB, one bit for each 4 KB page of LPDDR, 1 a page that is taken. Row r, 128 bits, is
 *              pages 128 r to 128 r + 127, page p bit p mod 128. Written by the master alone
 *   0x48000    record s, 68 rows, as frame 7 has it, its word 66 with the roots and depths
 *   0x48200    record s + 1, 68 rows: the end state the tracer tile checks its last record against
 *   0x48400    two rows for the tracer tile's header: HASH_MUL, HASH_STEP, HASH_LINES, ADVICE_N; zeros (v1.37's
 *              third row, the places of words 21-23, is gone: the owner's ruling of 2026-11-19)
 *   0x48480    the reply (30.7): 73 rows, its header word and three of figures, then the tracer tile's block (30.6),
 *              69 rows, which it sends here by one DMA on the master's request. Then unused to 0x48880 (the scrap
 *              area until WORKER7), so that no address after it moves
 *   0x48880    advice log s, a row for each two values, to the scratchpad's end
 */
#ifndef MASTER_LAYOUT_H
#define MASTER_LAYOUT_H

#define ML_MAP               0x40000u
#define ML_MAP_ROWS          8192u         /* 2^20 pages, 128 a row */
#define ML_RECORD            0x48000u
#define ML_EXPECT            0x48200u
#define ML_HEADER            0x48400u
#define ML_REPLY             0x48480u      /* v1.37 (30.7): the reply, 73 rows; rows 4-72 the tracer tile's block (30.6) */
#define ML_REPLY_ROWS        73u
#define ML_ADVICE            0x48880u
#define ML_ADVICE_ROWS       ((0x100000u - ML_ADVICE) / 4u)

/* boot_desc, in the image's .data, filled by the harness before the reset ends (as cc1.c's): by word */
#define MD_MAGIC             0x4D313337u   /* "M137" */
#define MD_H_MAGIC           0
#define MD_H_TILES           1    /* the compute tiles' image: 5 words: its code's word address in the master's scratchpad, its rows, its data's, its rows, the COUNT row's */
#define MD_H_TRACER          6    /* the tracer tile's image, the same 5 */
#define MD_H_TR_RECORD       11   /* the tracer tile's layout (tracer_layout.h): its record's word address */
#define MD_H_TR_EXPECT       12   /* its expected end state's */
#define MD_H_TR_ADVICE       13   /* its advice's */
#define MD_H_TR_ADVICE_ROWS  14   /* and the rows the advice may take there */
#define MD_H_GO_LATENCY      15   /* the cycles after a release before anything else is sent (6.6) */
#define MD_H_WORDS           16

/* system_config, in .data: N and s, the system's configuration, which no frame carries (HOST-FRAMES.md) */
#define MS_MAGIC             0x4E415353u   /* "SSAN" */
#define MS_H_MAGIC           0
#define MS_H_ASICS           1    /* N: 64, 128 or 256 */
#define MS_H_SEGMENT         2    /* s: the segment this ASIC runs, below N */
#define MS_H_UNUSED          3    /* 0: the threshold until WORKER7; kept so that the layout does not move */
#define MS_H_WORDS           4

/* master_results, by word; the harness finds it by its symbol */
#define MR_STATUS            0    /* 0 running; MR_OK; or what stopped it, below */
#define MR_DETAIL            1    /* with a failure: which frame type [7:0], and which check [15:8] */
#define MR_FRAMES            2    /* frames taken out of Ingress */
#define MR_RUNS              3    /* the Updater's runs that ended */
#define MR_PAGES_TAKEN       4    /* pages taken from the page map: roots and the free page queue's entries */
#define MR_LOADS             5    /* LOAD requests */
#define MR_RESULTS           6    /* results answered */
#define MR_OTHERS            7    /* other requests */
#define MR_ROOT_PROGRAM      8    /* the files' roots, in 128-byte units */
#define MR_ROOT_BASE         9
#define MR_ROOT_TABLE        10
#define MR_ROOT_CYCLE0       11
#define MR_ROOT_START        12
#define MR_ROOT_FINAL        13
#define MR_ROOT_TRACER       14
#define MR_DEPTHS            15   /* the images' depth [7:0], the table's [15:8] */
#define MR_TILE_ERRORS       16   /* at the end, or at the trap: the tile's ERRORS, Ingress's, the Updater's, ERROR_LINES_0-2 */
#define MR_INGRESS_ERRORS    17
#define MR_UPDATER_ERRORS    18
#define MR_ERROR_LINES       19   /* 19-21 */
#define MR_MCAUSE            22
#define MR_MEPC              23
#define MR_BOOT              24   /* 1 the LOADs are in; 2 the compute tiles run; 3 they have armed (the first barrier); 4 the tracer tile is loaded; 5 the replay is released */
#define MR_HANDED            25   /* mcycle at the start of the tracer tile's load */
#define MR_RELEASED          26   /* mcycle at the last release: the windows have landed */
#define MR_END               27   /* mcycle at the exit */
#define MR_PAGES_QUEUED      28   /* pages pushed into the free page queue */
#define MR_LINK_WORDS        29   /* words copied out of Ingress by its DMA (the forward count's are Ingress's to count) */
#define MR_WAKES             30   /* the core's wakes: the WFIs it returned from, from the reset to the exit */
#define MR_LINK_READS        31   /* the transfers of Ingress's DMA it started */
#define MR_PAGE_COUNT        32   /* 32-39: PAGE_COUNT at each run's done */
#define MR_RUN_START         40   /* 40-47: mcycle at each run's START */
#define MR_RUN_DONE          48   /* 48-55: at each run's done */
#define MR_FRAME_TYPE        56   /* 56-71: each frame's type */
#define MR_FRAME_START       72   /* 72-87: mcycle when its header word was in */
#define MR_FRAME_WAKES       88   /* 88-103: MR_WAKES's count when its header word was in */
#define MR_FRAME_READS       104  /* 104-119: MR_LINK_READS's count then */
#define MR_COUNT_STORES      120  /* stores to DMA_FORWARD_COUNT */
#define MR_HANDED_WAKES      121  /* MR_WAKES's count at the start of the tracer tile's load */
#define MR_HANDED_READS      122  /* MR_LINK_READS's then */
#define MR_FRAME_AWAKE       123  /* 123-138: mcycle less the cycles in WFI when each frame's header word was in (WORKER7):
                                   * the first is the boot's cost */
#define MR_REPLY_MCYCLE      139  /* 139-140: v1.37 (30.7), the mcycle the reply carries, [31:0] then [63:32] */
#define MR_REPLIED           141  /* 1 once the reply's 73 words are in the outgoing queue (EGRESS_COUNT back to 0) */
#define MR_WORDS             142
#define MR_MAX_RUNS          8
#define MR_MAX_FRAMES        16

#define MR_OK                1    /* the proof's frames applied, the segment traced and replayed, every result in */
#define MR_TRAP              2    /* a trap: an error bit's NMI (a bad packet on the link, an Updater error, a tile's error line), or the program's own */
#define MR_CONFIG            3    /* boot_desc or system_config is not as this file says */
#define MR_FRAME             4    /* a frame is not as HOST-FRAMES.md says, or not as its header frame said: MR_DETAIL */
#define MR_ROOM              5    /* advice log s does not fit, the page map is used up, or there are more frames or runs than the results hold */

/* MR_DETAIL's checks [15:8] */
#define MR_D_TYPE            1    /* a frame type that is none of 1-9 */
#define MR_D_SHORT           2    /* a header frame ended before its fields did */
#define MR_D_LONG            3    /* a frame's data did not end where its fields did */
#define MR_D_VERSION         4    /* frame 3's format version is not 1 */
#define MR_D_LENGTH          5    /* a data frame's length is not its header frame's, or frame 7's is not 68 N */
#define MR_D_ORDER           6    /* a data frame with no header frame before it; a base named and none established */
/* 7 is not used: it was v1.37's check that a frame ends with its packet, dropped with the threshold (WORKER4) */
#define MR_D_COUNTS          8    /* frame 3's page counts for s cannot be: the final image's below the Tracer's copy's */

#endif
