/* host.h: v1.37's master tile, for firmware: the master.c names for Ingress/Egress (17.6, mesh.h's), the Updater, the
 * master tile's peripheral 2 (17.10), and the frames the host sends (chapter 30). Written by hand, beside mesh.h.
 *
 * mesh.h is generated from the register table. Where this file and spec/ differ, this file is wrong. Every chip's master
 * tile has this hardware since v1.37's stage 3 (CHANGES-v1.37.md).
 */
#ifndef HOST_H
#define HOST_H

#include "mesh.h"
/* mcycle's high half (6.3; mesh.h has the low): the reply carries the 64-bit count (29.14) */
static inline uint32_t mcycleh(void) { uint32_t c; __asm__ volatile ("csrr %0, mcycleh" : "=r"(c)); return c; }

/* ---- Ingress/Egress (17.6): mesh.h's IngressEgressRegs, generated from the register table since v1.37's stage 3 (its
 * two queues' lengths at words 64 and 65, DMA_FORWARD_COUNT at 66, EGRESS_COUNT at 67, the UART in block 1 and at 42).
 * `host_link` is the master.c name for it. ---- */
#define host_link                   ingress_egress
#define HL_STATUS_FOREGROUND_READY  IE_STATUS_FOREGROUND_READY    /* [2] live: the foreground queue holds words */
#define HL_STATUS_BACKGROUND_READY  IE_STATUS_BACKGROUND_READY    /* [3] live: the background queue holds words */
#define HL_EVENT_FOREGROUND_SERVICE IE_EVENT_FOREGROUND_SERVICE   /* [6] counted, 1: the queue asks for service (17.6.7) */
#define HL_EVENT_BACKGROUND_SERVICE IE_EVENT_BACKGROUND_SERVICE   /* [7] counted, 1: the same, DMA_FORWARD_COUNT's count at 0 too */
#define HL_QUEUE_FOREGROUND         0          /* DMA_HDR.queue (17.6.4) */
#define HL_QUEUE_BACKGROUND         1
#define HL_PACKET_WORDS             32         /* a link packet's data, at most (28) */
#define HL_QUEUE_WORDS              INGRESS_QUEUE_WORDS            /* a queue: 1,024 words, 16 KB (17.6.3) */
#define HL_FORWARD_COUNT            0x7FFFFFFFu   /* DMA_FORWARD_COUNT [30:0]: the words still to move */
#define HL_FORWARD_DISCARD          0x80000000u   /* DMA_FORWARD_COUNT [31]: drop them, not to the Updater */

/* ---- the Updater, peripheral type 5 (UPDATER-INTERFACE.md): the master tile's peripheral 2; it has no DMA ---- */
typedef struct {
    MESH_RO PARAMS;              /* 0x000: type 5 */
    MESH_RO STATUS;              /* 0x004: [0] busy */
    MESH_RO ERRORS;              /* 0x008: sticky until the reset */
    MESH_RO EVENTS;              /* 0x00C: [0] done, [3] free_page_credit; a load consumes. [2] reads 0 */
    MESH_RO EVENTS_PEEK;         /* 0x010 */
    MESH_RW EVENTS_MASK;         /* 0x014 */
    MESH_WO START;               /* 0x018: starts the run block 2 describes; during a run, UB */
    uint32_t _unmapped_01C[9];
    uint32_t _no_dma[16];        /* 0x040-0x07C: no DMA: block 1 reads 0 */
    MESH_RW RUN_CONFIG;          /* 0x080: lru [0], continue [1], tracer_copy [2], depth [4:3], root [6:5] */
    MESH_RW WORD_COUNT;          /* 0x084: the 64-bit update words this run applies; counts down */
    MESH_RW PAGE_COUNT;          /* 0x088: the pages that will be queued for this run; counts down by one a page taken */
    MESH_RW IMAGE_ROOT;          /* 0x08C: the root of the file the run updates, in 128-byte units */
    MESH_RW TRACER_ROOT;         /* 0x090: with tracer_copy, the root of the Tracer's copy */
    MESH_RW SOURCE_ROOT;         /* 0x094: with root 2, the root of the file being copied */
    uint32_t _unmapped_098[10];
    MESH_RO STAT_CYCLES;         /* 0x0C0: cycles with STATUS.busy at 1 */
    MESH_RO STAT_STALL_DATA;     /* 0x0C4: cycles waiting for a word on the data input */
    MESH_RO STAT_STALL_PAGES;    /* 0x0C8: cycles waiting on an empty free page queue */
    MESH_RO STAT_STALL_MEMORY;   /* 0x0CC: cycles waiting on the cache or on LPDDR */
    MESH_RO STAT_PAGES_READ;     /* 0x0D0: pages and half pages read */
    MESH_RO STAT_PAGES_WRITTEN;  /* 0x0D4: pages and half pages written */
    uint32_t _unmapped_0D8[74];
} UpdaterRegs;
_Static_assert(sizeof(UpdaterRegs) == 0x200 && __builtin_offsetof(UpdaterRegs, RUN_CONFIG) == 0x080 &&
               __builtin_offsetof(UpdaterRegs, SOURCE_ROOT) == 0x094 && __builtin_offsetof(UpdaterRegs, STAT_CYCLES) == 0x0C0 &&
               __builtin_offsetof(UpdaterRegs, STAT_PAGES_WRITTEN) == 0x0D4, "UpdaterRegs");
#define updater    ((volatile UpdaterRegs *)0xFFFFFC00u)    /* peripheral 2 on the master tile */

#define PERIPHERAL_TYPE_UPDATER     5
#define UPD_RUN_CONFIG_LRU          (1u << 0)   /* the data is LRU-coded: the frame header's compression type */
#define UPD_RUN_CONFIG_CONTINUE     (1u << 1)   /* the run carries on in the last one's frame: the decoder keeps its state */
#define UPD_RUN_CONFIG_TRACER_COPY  (1u << 2)   /* a copy of each page as it stands goes into the file in TRACER_ROOT first */
#define UPD_RUN_CONFIG_DEPTH(v)     (((uint32_t)(v) & 0x3u) << 3)   /* [4:3] the file's depth */
#define UPD_RUN_CONFIG_ROOT(v)      (((uint32_t)(v) & 0x3u) << 5)   /* [6:5] v1.37: the root the run first makes */
#define UPD_ROOT_EXISTS             0   /* the file exists */
#define UPD_ROOT_EMPTY              1   /* first write an empty root at IMAGE_ROOT */
#define UPD_ROOT_COPY               2   /* first copy SOURCE_ROOT's root, its owned bits cleared, to IMAGE_ROOT (and TRACER_ROOT) */
#define UPD_EVENT_FREE_PAGE_CREDIT  (1 << 2)   /* counted, 2: the free page queue's credit */
#define UPD_ERROR_QUEUE_OVERRUN     (1 << 0)   /* a push into a full free page queue; the push is dropped */
#define UPD_ERROR_BEYOND            (1 << 1)   /* an address past what the file's depth can reach */
#define UPD_ERROR_LPDDR_ERROR       (1 << 2)   /* a response from LPDDR that is not OKAY */
#define UPD_ERROR_OUT_OF_PAGES      (1 << 3)   /* a page was needed with PAGE_COUNT at 0 */
#define UPD_ERROR_NOT_OWNED         (1 << 4)   /* the cache refused an update */
#define UPD_QUEUE_FREE_PAGES        (PERIPHERAL_BASE(2) + 0x200000)   /* part 2: the free page queue's pushes, a page's address in [31:0] */
#define UPD_FREE_PAGE_CREDIT        128   /* the free page queue's: half its 256 entries */

/* ---- the frames (HOST-FRAMES.md): a frame's header word is 128 bits, the compression in [7:0], the type in [95:64],
 * the length, the data's 128-bit words, in [127:96]; as four 32-bit words, word 0, word 2 and word 3 ---- */
#define FRAME_COMPRESSION(w0)       ((w0) & 0xFFu)   /* 0 none, 1 LRU */
#define FRAME_PROGRAM_HEADER        1
#define FRAME_PROGRAM_DATA          2
#define FRAME_PROOF_HEADER          3
#define FRAME_TABLE_HEADER          4
#define FRAME_TABLE_DATA            5
#define FRAME_DATA_IMAGE            6
#define FRAME_REGISTERS             7
#define FRAME_ADVICE                8
#define FRAME_SEGMENTS              9
#define FRAME_REPLY                 16   /* the master tile's reply to the host (29.14), on the foreground */

/* ---- LPDDR as the master tile sees it in v1.37: pages of 4 KB, a page's address in 128-byte units ---- */
#define LPDDR_PAGES                 (1u << 20)   /* 4 GB: the page map's reach, one bit a page, 128 KB */
#define PAGE_ADDRESS(page)          ((uint32_t)(page) << 5)

#endif /* HOST_H */
