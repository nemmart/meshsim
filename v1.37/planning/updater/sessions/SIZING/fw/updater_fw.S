# updater_fw.S: the Updater's work as firmware on the master tile's core, in outline (SIZING/REPORT.md, part 5).
#
# NOT A PROGRAM TO RUN. Each PATH is the straight line of instructions one case executes, in order, so that its
# instructions and cycles can be counted (fw_cycles.py). It assembles (llvm-mc, rv32ic_zicond_zicsr_zmmul) so that
# every instruction is a real one. The marks, after '#':
#   T  a taken branch: 3 cycles more (core.md, "The core's cycle costs")
#   J  a jump (JAL): 2 cycles more
#   U  uses the result of the load just before it: 1 cycle more
#   W  a load from a window: WINDOW_LOAD_LATENCY 2, 1 cycle more than DMEM's
#   wait:NAME  the core waits here for memory: counted apart, since the controller waits as long
# Every other instruction is 1 cycle; loads and stores to DMEM retire one a cycle.
#
# WHAT THE CORE WOULD NEED THAT IT DOES NOT HAVE: a DMA of its own to LPDDR (pages and half pages, DMEM <-> LPDDR),
# here the window LDMA_*; and the cache's look-up port, here the window LK_*. Both are assumed, at window latency.
# The update words come into DMEM by Ingress's DMA, which exists (17.6.4); its cost, about 360 instructions a
# transfer of 256 words (WORKER4-REPORT.md), is the measured point and is added apart.
#
# Registers across the loop:
#   s0 the next update word in DMEM; s1 the end of the words landed; s2 WORD_COUNT; s3 the run's values left (0: no
#   run open); s4 the run's next word index; s5 the held page's number; s6 the held page's 4 KiB in DMEM; s7 the
#   halves had (bit 0, bit 1); s8 the halves dirty; s9 0x80000000; s10 the held page's LPDDR address; s11 the LRU
#   table (32 x 8 bytes, a ring); a7 the ring's head; a3-a5 the bit window (96 bits), a6 its bit count;
#   tp 66; gp the windows' base.

	.option norvc
	.equ LDMA_LOCAL, 0x00
	.equ LDMA_REMOTE, 0x04
	.equ LDMA_LEN, 0x08
	.equ LDMA_GO, 0x0C
	.equ LDMA_BUSY, 0x10
	.equ LK_ROOT, 0x20
	.equ LK_PAGE, 0x24
	.equ LK_ARG, 0x28
	.equ LK_GO, 0x2C
	.equ LK_BUSY, 0x30
	.equ LK_STATUS, 0x34
	.equ LK_ENTRY, 0x38
	.text

# PATH raw_header: a header word, raw (17.10.2: the run of values opens)
raw_header:
	beq s0, s1, refill
	lw t0, 0(s0)
	lw t1, 4(s0)
	addi s0, s0, 8
	addi s2, s2, -1
	beqz s3, rh_1             # T
rh_1:
	mv s4, t0
	mv s3, t1
	bnez s2, raw_header       # T
# END

# PATH raw_value: a value into the page held, raw, the general path (any run, any place)
raw_value:
	beq s0, s1, refill
	lw t0, 0(s0)
	lw t1, 4(s0)
	addi s0, s0, 8
	addi s2, s2, -1
	beqz s3, header
	srli t2, s4, 9            # the value's page
	bne t2, s5, newpage
	andi t3, s4, 511          # its word in the page
	srli t4, t3, 8            # its half
	srl t5, s7, t4
	andi t5, t5, 1
	beqz t5, halfmiss         # the half not had
	slli t3, t3, 3
	add t3, t3, s6
	sw t0, 0(t3)
	sw t1, 4(t3)
	addi t5, t4, 1            # 1 << half
	or s8, s8, t5             # the half dirty
	addi s4, s4, 1
	addi s3, s3, -1
	bnez s2, raw_value        # T
# END

# PATH raw_fast4: four values of a run inside the page held, its halves had (the run's bulk, unrolled by 4)
raw_fast4:
	lw t0, 0(s0)
	lw t1, 4(s0)
	lw t2, 8(s0)
	lw t3, 12(s0)
	sw t0, 0(a0)
	sw t1, 4(a0)
	sw t2, 8(a0)
	sw t3, 12(a0)
	lw t0, 16(s0)
	lw t1, 20(s0)
	lw t2, 24(s0)
	lw t3, 28(s0)
	sw t0, 16(a0)
	sw t1, 20(a0)
	sw t2, 24(a0)
	sw t3, 28(a0)
	addi s0, s0, 32
	addi a0, a0, 32
	bne a0, a1, raw_fast4     # T
# END

# PATH raw_fast_setup: entering the fast path, once a stretch: n = min(values left, WORD_COUNT, to the page's end,
# words landed), rounded down to 4; the counts moved on by n; the halves touched marked dirty
raw_fast_setup:
	sub t0, s3, s2            # min(s3, s2), branch-free
	sltu t1, s3, s2
	czero.eqz t0, t0, t1
	add t0, s2, t0
	andi t2, s4, 511          # to the page's end: 512 - (index & 511)
	li t3, 512
	sub t3, t3, t2
	sub t4, t0, t3            # min(n, to the end)
	sltu t1, t0, t3
	czero.eqz t4, t4, t1
	add t0, t3, t4
	sub t3, s1, s0            # the words landed
	srli t3, t3, 3
	sub t4, t0, t3
	sltu t1, t0, t3
	czero.eqz t4, t4, t1
	add t0, t3, t4
	andi t0, t0, -4
	beqz t0, raw_value        # too short: the general path
	srli t5, s7, 0            # both halves had?
	xori t5, t5, 3
	bnez t5, raw_value        # no: the general path
	slli a0, t2, 3
	add a0, a0, s6
	slli t5, t0, 3
	add a1, a0, t5
	sub s2, s2, t0
	sub s3, s3, t0
	add s4, s4, t0
	srli t5, t2, 8            # the halves touched: from (start >> 8) to ((start + n - 1) >> 8)
	add t6, t2, t0
	addi t6, t6, -1
	srli t6, t6, 8
	addi t5, t5, 1
	addi t6, t6, 1
	or t5, t5, t6
	or s8, s8, t5
	j raw_fast4               # J
# END

# PATH lru_hit0: an LRU hit at index 0 (the code's first bit 1, then 5 bits): the decode, before the word's use
lru_hit0:
	blt a6, tp, lru_refill    # fewer than 66 bits in the window
	andi t0, a3, 1
	beqz t0, lru_miss
	srli t1, a3, 1
	andi t1, t1, 31           # i
	srli a3, a3, 6            # the window shifted by 6
	slli t2, a4, 26
	or a3, a3, t2
	srli a4, a4, 6
	slli t2, a5, 26
	or a4, a4, t2
	srli a5, a5, 6
	addi a6, a6, -6
	bnez t1, lru_move
	slli t2, a7, 3            # the entry at the ring's head
	add t2, t2, s11
	lw t0, 0(t2)
	lw t1, 4(t2)
# END

# PATH lru_move1: an LRU hit at index i > 0: the move to front, a cost each entry moved (the ring's entries head to
# head + i - 1 move back one; a wrap of the ring splits the loop in two, counted in lru_move_setup)
lru_move1:
	lw t3, 0(t4)
	lw t5, 4(t4)
	sw t3, 8(t4)
	sw t5, 12(t4)
	addi t4, t4, -8
	bne t4, t6, lru_move1     # T
# END

# PATH lru_move_setup: an LRU hit at index i > 0: the entry read, the loop's bounds, the entry put at the head
lru_move_setup:
	add t2, a7, t1
	andi t2, t2, 31
	slli t2, t2, 3
	add t2, t2, s11
	lw t0, 0(t2)
	lw a1, 4(t2)
	addi t4, t2, -8           # the first entry to move
	slli t6, a7, 3
	add t6, t6, s11
	addi t6, t6, -8           # the loop's end (one before the head)
	bltu t4, t6, lru_wrap     # the ring wraps: two loops (the second's setup is lru_wrap's 4 more)
	slli t2, a7, 3
	add t2, t2, s11
	sw t0, 0(t2)
	sw a1, 4(t2)
	mv t1, a1
	j lru_dispatch            # J
# END

# PATH lru_miss34: an LRU miss of 32 bits (0, 0, then 32 bits): the decode, the word in at the ring's front
lru_miss34:
	blt a6, tp, lru_refill
	andi t0, a3, 1
	beqz t0, lm34_1           # T
lm34_1:
	andi t0, a3, 2
	bnez t0, lru_miss66
	srli t0, a3, 2            # the value: bits 33:2
	slli t2, a4, 30
	or t0, t0, t2
	li t1, 0
	srli a3, a4, 2            # the window shifted by 34
	slli t2, a5, 30
	or a3, a3, t2
	srli a4, a5, 2
	li a5, 0
	addi a6, a6, -34
	addi a7, a7, -1           # the ring's head back one: the word at the front, the last falls out
	andi a7, a7, 31
	slli t2, a7, 3
	add t2, t2, s11
	sw t0, 0(t2)
	sw t1, 4(t2)
	j lru_dispatch            # J
# END

# PATH lru_miss66: an LRU miss of 64 bits (0, 1, then 64 bits)
lru_miss66:
	blt a6, tp, lru_refill
	andi t0, a3, 1
	beqz t0, lm66_1           # T
lm66_1:
	andi t0, a3, 2
	bnez t0, lm66_2           # T
lm66_2:
	srli t0, a3, 2            # the value: bits 65:2
	slli t2, a4, 30
	or t0, t0, t2
	srli t1, a4, 2
	slli t2, a5, 30
	or t1, t1, t2
	srli a3, a5, 2            # the window shifted by 66
	li a4, 0
	li a5, 0
	addi a6, a6, -66
	addi a7, a7, -1
	andi a7, a7, 31
	slli t2, a7, 3
	add t2, t2, s11
	sw t0, 0(t2)
	sw t1, 4(t2)
	j lru_dispatch            # J
# END

# PATH lru_refill32: 32 more bits into the bit window, at its count (34 to 65)
lru_refill32:
	lw t0, 0(s0)
	addi s0, s0, 4
	addi t1, a6, -64
	bgez t1, lrf_hi
	addi t2, a6, -32
	sll t3, t0, t2
	or a4, a4, t3
	li t4, 32
	sub t4, t4, t2
	srl t3, t0, t4
	or a5, a5, t3
	addi a6, a6, 32
	j lru_hit0                # J
# END

# PATH lru_place: an LRU-coded value, after its decode: into the page held (the raw general path's tail)
lru_place:
	addi s2, s2, -1
	beqz s3, header
	srli t2, s4, 9
	bne t2, s5, newpage
	andi t3, s4, 511
	srli t4, t3, 8
	srl t5, s7, t4
	andi t5, t5, 1
	beqz t5, halfmiss
	slli t3, t3, 3
	add t3, t3, s6
	sw t0, 0(t3)
	sw t1, 4(t3)
	addi t5, t4, 1
	or s8, s8, t5
	addi s4, s4, 1
	addi s3, s3, -1
	bnez s2, lru_hit0         # T
# END

# PATH lru_header: an LRU-coded header word, after its decode
lru_header:
	addi s2, s2, -1
	beqz s3, lh_1             # T
lh_1:
	mv s4, t0
	mv s3, t1
	bnez s2, lru_hit0         # T
# END

# PATH new_page: a value in another page: the page held (owned) written in place by its dirty halves, then the
# value's page looked up through the cache and held. The two waits are the controller's too (flush write, look-up).
new_page:
	beqz s8, np_clean
	li t0, 3
	li t1, 4096               # both halves dirty: the page
	li t2, 0
	beq s8, t0, np_1          # T
np_1:
	sw s6, LDMA_LOCAL(gp)
	add t3, s10, t2
	sw t3, LDMA_REMOTE(gp)
	sw t1, LDMA_LEN(gp)
	sw t0, LDMA_GO(gp)
np_w1:
	lw t4, LDMA_BUSY(gp)      # W wait:flush_write
	bnez t4, np_w1
	sw a2, LK_ROOT(gp)        # the image's root
	sw t2, LK_PAGE(gp)
	sw zero, LK_ARG(gp)       # a look-up
	sw t0, LK_GO(gp)
np_w2:
	lw t4, LK_BUSY(gp)        # W wait:lookup
	bnez t4, np_w2
	lw t4, LK_STATUS(gp)      # W
	bnez t4, fault            # U
	lw t5, LK_ENTRY(gp)       # W
	slli s10, t5, 1           # U   where it is written: the entry's address
	srli s10, s10, 1
	slli s10, s10, 7          # in bytes
	mv s5, t2
	li s7, 0
	li s8, 0
	j raw_value               # J
# END

# PATH fault_clear4: a fault's index page, loaded into DMEM: four entries' owned bits cleared, holes kept
fault_clear4:
	lw t0, 0(a0)
	lw t1, 4(a0)
	lw t2, 8(a0)
	lw t3, 12(a0)
	addi t4, t0, 1            # 0 for a hole
	czero.eqz t4, s9, t4      # the owned bit, unless a hole
	and t4, t0, t4
	xor t0, t0, t4
	addi t4, t1, 1
	czero.eqz t4, s9, t4
	and t4, t1, t4
	xor t1, t1, t4
	addi t4, t2, 1
	czero.eqz t4, s9, t4
	and t4, t2, t4
	xor t2, t2, t4
	addi t4, t3, 1
	czero.eqz t4, s9, t4
	and t4, t3, t4
	xor t3, t3, t4
	sw t0, 0(a0)
	sw t1, 4(a0)
	sw t2, 8(a0)
	sw t3, 12(a0)
	addi a0, a0, 16
	bne a0, a1, fault_clear4  # T
# END

# PATH fault_level: one (level, file) of a fault, besides fault_clear4's 256 passes for an index page: its source read
# whole, a free page taken, the new page written whole, the entry above it updated (17.10.5's four steps)
fault_level:
	sw a0, LDMA_LOCAL(gp)
	sw a2, LDMA_REMOTE(gp)    # the source
	li t0, 4096
	sw t0, LDMA_LEN(gp)
	sw zero, LDMA_GO(gp)      # a read
fl_w1:
	lw t4, LDMA_BUSY(gp)      # W wait:load
	bnez t4, fl_w1
	addi a1, a0, 1024         # (fault_clear4 runs here: 1,024 entries, for an index page)
	lw t1, 0(s11)             # a free page from the queue (in DMEM, as the core keeps it)
	addi s11, s11, 4
	addi a6, a6, -1           # PAGE_COUNT
	sw t1, LDMA_REMOTE(gp)    # U
	li t2, 1
	sw t2, LDMA_GO(gp)        # a write
fl_w2:
	lw t4, LDMA_BUSY(gp)      # W wait:new_write
	bnez t4, fl_w2
	sw a3, LK_ROOT(gp)
	sw a4, LK_PAGE(gp)
	or t1, t1, s9             # the entry: the new page, owned
	sw t1, LK_ARG(gp)
	sw t2, LK_GO(gp)
fl_w3:
	lw t4, LK_BUSY(gp)        # W wait:new_update
	bnez t4, fl_w3
	lw t4, LK_STATUS(gp)      # W
	bnez t4, fault            # U
	addi a5, a5, 1            # the next (level, file)
	j fault_level             # J
# END

# targets of the branches the paths leave by
refill: header: newpage: halfmiss: fault: lru_refill: lru_miss: lru_move: lru_wrap: lru_dispatch: np_clean: lrf_hi:
	nop
