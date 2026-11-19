# cache_fw.S: the cache's controller as firmware on the memory tile's core, in outline (SIZING/REPORT.md, part 5).
# The marks are updater_fw.S's (T taken, J jump, U load-use, W window load, wait:NAME memory).
#
# WHAT THE CORE WOULD NEED THAT IT DOES NOT HAVE: the cache's memories mapped to it as a table, 32-bit words at
# TABLE_LOAD_LATENCY 1 (6.4), the tag memory at TAGS (a set a 64-byte row: 8 way words of valid [31] and tag
# [21:0], the dirty bits and the ages in a ninth and a tenth) and the data memory at DATA (a line a 128-byte row);
# the look-up ports' request registers and the reply bus as a window, REQ_* and REP_*; the fill port's the same, and
# a DMA of lines between the data memory and LPDDR, LDMA_*. All assumed.
#
# Registers: gp the windows; s1 TAGS; s2 DATA; s9 0x80000000; a0 the request's op, depth, level; a1 its root; a2
# its page; t2 its depth; a3 the page at level k; a4 k.

	.option norvc
	.equ REQ_VALID, 0x00
	.equ REQ_OPS, 0x04
	.equ REQ_ROOT, 0x08
	.equ REQ_PAGE, 0x0C
	.equ REP_ENTRY, 0x10
	.equ REP_STATUS, 0x14
	.equ REP_GO, 0x18
	.equ FRR_DATA, 0x40
	.equ FRR_GO, 0xC0
	.equ LDMA_ROW, 0xD0
	.equ LDMA_REMOTE, 0xD4
	.equ LDMA_GO, 0xD8
	.equ LDMA_BUSY, 0xDC
	.text

# PATH c_take: a look-up taken from its port's register, checked for beyond
c_take:
	lw t0, REQ_VALID(gp)      # W
	beqz t0, c_take           # U
	lw a0, REQ_OPS(gp)        # W
	lw a1, REQ_ROOT(gp)       # W
	lw a2, REQ_PAGE(gp)       # W
	andi t1, a0, 3            # op
	bnez t1, c_other
	srli t2, a0, 2
	andi t2, t2, 3            # depth
	beqz t2, c_beyond
	slli t3, t2, 3            # 10 x depth
	slli t4, t2, 1
	add t3, t3, t4
	srl t4, a2, t3
	bnez t4, c_beyond
	mv a3, a1
	li a4, 0
# END

# PATH c_level: one level of the walk, its line in the cache, found at the k-th way probed (the probe is c_probe),
# the replacement order not kept (the lean case)
c_level:
	sub t0, t2, a4            # the index at level k: (page >> 10 (depth - 1 - k)) & 1023
	addi t0, t0, -1
	slli t1, t0, 3
	slli t0, t0, 1
	add t0, t0, t1
	srl t0, a2, t0
	andi t0, t0, 1023
	srli t1, t0, 5            # the line: the page + (i >> 5)
	add t1, a3, t1
	andi t3, t1, 511          # its set
	srli t4, t1, 9            # its tag
	or t4, t4, s9             # with the valid bit: the way word to find
	slli t5, t3, 6
	add t5, t5, s1            # the set's row: then the probes (c_probe), the one that hits a taken branch
c_hit:
	slli t6, t3, 10           # the line's row: (set x 8 + way) x 128; the way, a constant at its label
	addi t6, t6, 384
	add t6, t6, s2
	andi t0, t0, 31
	slli t0, t0, 2
	add t6, t6, t0
	lw a5, 0(t6)              # the entry
	addi t0, a5, 1            # U  a hole?
	beqz t0, c_hole
	bgez a5, c_notowned       # not owned?
	addi a4, a4, 1
	beq a4, t2, c_found       # the last level: answered (c_reply)
	slli a3, a5, 1            # the next page: entry & 0x7FFF_FFFF
	srli a3, a3, 1
	j c_level                 # J
# END

# PATH c_probe: one way of the set probed: its word loaded and compared
c_probe:
	lw t6, 0(t5)
	beq t6, t4, c_hit         # U
# END

# PATH c_touch_lru: the hit way made the most recent, true LRU (23.6.1): the ages (3 bits a way, a word) read, every
# younger way aged by one, the way's age 0
c_touch_lru:
	lw t6, 36(t5)             # the ages
	srli a6, t6, 9            # the hit way's age (way 3: shift 9)
	andi a6, a6, 7
	li a7, 0
	srli t0, t6, 0            # way 0: older than the hit way? (sltu) then + 1 in its field
	andi t0, t0, 7
	sltu t0, t0, a6
	add a7, a7, t0
	srli t0, t6, 3            # way 1
	andi t0, t0, 7
	sltu t0, t0, a6
	slli t0, t0, 3
	add a7, a7, t0
	srli t0, t6, 6            # way 2
	andi t0, t0, 7
	sltu t0, t0, a6
	slli t0, t0, 6
	add a7, a7, t0
	srli t0, t6, 12           # way 4
	andi t0, t0, 7
	sltu t0, t0, a6
	slli t0, t0, 12
	add a7, a7, t0
	srli t0, t6, 15           # way 5
	andi t0, t0, 7
	sltu t0, t0, a6
	slli t0, t0, 15
	add a7, a7, t0
	srli t0, t6, 18           # way 6
	andi t0, t0, 7
	sltu t0, t0, a6
	slli t0, t0, 18
	add a7, a7, t0
	srli t0, t6, 21           # way 7
	andi t0, t0, 7
	sltu t0, t0, a6
	slli t0, t0, 21
	add a7, a7, t0
	add t6, t6, a7            # every younger way + 1
	li t0, 7
	slli t0, t0, 9
	not t0, t0
	and t6, t6, t0            # the hit way's age 0
	sw t6, 36(t5)
# END

# PATH c_reply: the reply on the bus, and back to the ports
c_reply:
	sw a5, REP_ENTRY(gp)
	slli t0, a4, 3
	sw t0, REP_STATUS(gp)     # status ok, level
	sw zero, REP_GO(gp)       # the client's number
	j c_take                  # J
# END

# PATH c_miss: a line missing at a level, the miss handler's work: the victim (the first invalid way was seen in
# the probes; else the oldest, which is not the update slot's), its write-back if dirty, the fetch, the install
c_miss:
	lw t6, 36(t5)             # the ages: the way of age 7
	li t0, 7
	li a6, 0
cm_1:
	andi a7, t6, 7
	beq a7, t0, cm_2          # (eight at most; the mean, counted as four by c_victim1)
	srli t6, t6, 3
	addi a6, a6, 1
	j cm_1                    # J
cm_2:
	lw a7, 32(t5)             # the dirty bits
	srl a7, a7, a6
	andi a7, a7, 1
	beqz a7, cm_fetch         # T  (a clean victim: counted as the common case; c_writeback adds a dirty one's)
cm_fetch:
	slli t6, t3, 3            # the victim's row: set x 8 + way
	add t6, t6, a6
	sw t6, LDMA_ROW(gp)
	slli t0, t1, 7
	sw t0, LDMA_REMOTE(gp)    # the line's address
	sw zero, LDMA_GO(gp)      # a fetch
cm_w:
	lw t0, LDMA_BUSY(gp)      # W wait:fetch
	bnez t0, cm_w
	slli t0, a6, 2            # the way word: valid and the tag
	add t0, t0, t5
	sw t4, 0(t0)
	j c_level                 # J  (walked again: its probe now hits)
# END

# PATH c_victim1: the oldest way's search, a way more than the first
c_victim1:
	srli t6, t6, 3
	addi a6, a6, 1
	andi a7, t6, 7
	beq a7, t0, cm_2          # T
# END

# PATH c_writeback: a dirty victim written back first
c_writeback:
	slli t6, t3, 3
	add t6, t6, a6
	sw t6, LDMA_ROW(gp)
	sw t0, LDMA_REMOTE(gp)    # the victim's address (from its tag and set)
	li t0, 1
	sw t0, LDMA_GO(gp)        # a write
cw_w:
	lw t0, LDMA_BUSY(gp)      # W wait:write_back
	bnez t0, cw_w
# END

# PATH c_fill_line8: a fill read's data line, found: 8 of its 32 words copied from the data memory to the fill port's
# response (fill reads are the Tracer's traffic: 1,024 bits a line); four passes a line
c_fill_line8:
	lw t0, 0(t6)
	lw t1, 4(t6)
	lw a6, 8(t6)
	lw a7, 12(t6)
	sw t0, 0(s3)
	sw t1, 4(s3)
	sw a6, 8(s3)
	sw a7, 12(s3)
	lw t0, 16(t6)
	lw t1, 20(t6)
	lw a6, 24(t6)
	lw a7, 28(t6)
	sw t0, 16(s3)
	sw t1, 20(s3)
	sw a6, 24(s3)
	sw a7, 28(s3)
	addi t6, t6, 32
	addi s3, s3, 32
	bne t6, s4, c_fill_line8  # T
# END

# targets
c_other: c_beyond: c_hole: c_notowned: c_found:
	nop
