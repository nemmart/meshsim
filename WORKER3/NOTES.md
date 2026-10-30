# WORKER3 running notes

Review of DECISIONS-v1.2 (registers, peripherals, software impact). Running notes; REVIEW.md is the
deliverable. Line numbers: D = `wip/planning/DECISIONS-v1.2.md`, S = `wip/spec/machine-spec.md`.

## Status
- [x] Read DECISIONS-v1.2 in full
- [x] Read spec 4.11-4.20 (relevant parts), 6.1-6.6, 8
- [x] REGISTERS.md (s.1), mesh.h, planning notes to-do 13-14, spec 9 invariants
- [x] sort code, meshsim code: migration inventory (grep table below)
- [x] register-map overlap check: D11 block 0x800-0x870 has no overlaps; all 40 v1.1 registers accounted for
- [x] write REVIEW.md (23 findings, W3-01..W3-23; pushed)

## Candidate findings (raw)
1. UART errors (RX overrun, framing) become sticky peripheral ERRORS on C&C 1 (D15 l.453); C&C 1 is never
   halted (bit 64 unused, D15 l.450) so they never clear -> C&C 1 hard interrupt + C&C pin high forever.
   They are environmental, not program errors (S 4.13 l.919 principle). v1.1 had W1C in UART_STATUS (S l.2125).
2. Peripheral EVENTS "sticky; a load consumes" (D10 l.316) vs replayer low-water lines = live with
   credit-on-claim (S 4.19.8 l.1457-1462), UART RX/TX events live (S l.1679). Standard window doesn't
   admit live/claim kinds; 4.17.3's three kinds not carried over.
3. Live tile EVENTS[9..11] (D10 l.326) derived from peripheral EVENTS&MASK: latency between a peripheral
   EVENTS load's consume and the tile bit falling not defined -> WFI right after the load may fall through
   on a stale bit. Sampling rule (S l.758) must cover peripheral windows.
4. STATUS renumbered (D18 l.532-542) though D18 l.517 says "v1.1's positions kept wherever a field
   survives": send_busy 5->3, mem_idle 7->4, cm_idle 8->5, done 9->6, cmd landed 10->7, request_busy 11->8.
5. D9 l.297 lists `UART_CONTROL`; v1.1 has `UART_DIVISOR` (S l.2126), as D11 l.376 says. Nit.
6. D7 l.255: "a change of dst alone, the common case in replies ... still one store" - false with
   ROUTER_ADDR auto-advance: v1.1 mode-8 replies ignored addr; v1.2 mode-0 reply to word 2*me must
   re-store ROUTER_ADDR after each send (it advanced). Check sort's reply code.
7. ROUTER_ADDR auto-advance rule undefined for v1.2 modes (v1.1 rule keyed on modes 3/6/7/9, S l.788):
   units (Q10), modes 1-4 (address unused?), carry across region boundary.
8. S 4.11 l.792 / 6.6 l.1832: C&C's port treats mode 3 as write-stream data; v1.2 mode 3 = augmented.
9. S 6.2 l.1747: staging is mode 2 at PTR[64]; v1.2 mode 2 = bucket, by-source = mode 1.
10. Strict Harvard (S l.761, l.773: loads/stores to instruction memory are bus errors) vs D5 l.197 "the
    controller sees 0x00, 0x01 and 0xFF": does the controller load/store region 0x01? Question.
11. Resident loader address (S l.765, 0x1F00) and HALT->RUN PC (S l.1729) need new values in 0x01; command
    block "top of data memory" (S l.801) and reply block move with 32 KB; not decided. Also x0-relative
    reach: data memory 0x000-0x7FF now x0-reachable (could place command/reply blocks there).
12. 6.4 l.1803: "a tile that only sends still waits for mem_idle before done" vs D3/D17 (mem_idle out of
    done rule, STATUS diagnostic only).
13. 17: stale DMA run done [6]: is [6] cleared at PULSE_DMA? If not, a wait on "counter n & [6]" can be
    satisfied by the previous run's [6]. Also zero-arrival tiles: counter never armed -> no done event.
14. Naming collision: tile PULSE_DMA (address-generator run) vs peripheral PULSE_DMA (output engine);
    same names PARAMS/STATUS/ERRORS/EVENTS at both levels -> mesh.h needs a scheme.
15. Bad packet [1] (D18 l.552) "a region it can't reach": absent peripheral region; reserved 0x06-0xFE;
    offset beyond memory size (32 KB in 24-bit offset); write running off a region's end; modes 1-4's
    address field. v1.1 mode 9 to an absent peripheral was counted at once (S l.1066); now uncounted.
16. Register-load latency rule (S l.757) and store-stall list (l.759) must extend to peripheral windows
    (UART TX stall now a peripheral word; SENDER_SEND gone).
17. C&C 1 registers not placed: READY bits, receive FIFO, watchdog, host interface (S 6.6 l.1821-1840).
18. D4: STREAM_COUNT [31:25] called *d*; D12 calls [31:25] dst. Nit: same field.

19. D12 l.390 calls STREAM_COUNT [31:25] "dst"; v1.1 STREAM_DEST *d* is a RELATIVE offset for 0-63
    (S l.367, l.385, l.437); ROUTER_HDR/DMA_HDR dst is absolute. "One layout" hides different semantics.
20. ROUTER_ADDR auto-advance breaks one-store replies: sort exchange.c l.12-16, bt_run.c l.70-74,
    bt_plan.c l.93-105, split.c (hdr x10), rcp.h l.38 all do `ROUTER_SET_HDR = hdr | DST(dst)` per dst with a
    fixed address (mode 8 or mode 6 + me). v1.2 needs ROUTER_HDR + ROUTER_ADDR + send per dst.
21. wait_tables() is a state poll (idempotent); event [6] is an edge (sticky until consumed). Replacing the 15
    calls by WFI on [6] hangs where no run happened since [6] was last consumed (config.c:13, local.c:32,46,55,
    95, main.c:30 before ov_load's run...) and passes early on a stale unconsumed [6] (v1.1 [11] is cleared
    only by a consuming load and HALT->RUN, S l.874/892; wfi()'s reset mask does not consume it).
22. NULL: v1.1 address 0 = instruction memory, data access = bus error (S l.773) -> free NULL check. v1.2
    data memory at 0x00000000: NULL deref silently valid; same word the all-zero header writes (D6 l.225).
23. Trap handler residency: HALT->RUN PC = loader; loader is 64 words (S l.794); handler (D1.4) must be
    resident too and outside every overlay region (6.5 l.1813); mtvec reset value/when set. loader.S `ebreak`
    on unknown command, crt0.S `ebreak` exit (meshsim test convention), riscv_test.h PASS/FAIL `ebreak`
    all change meaning with traps (-> W1 for semantics; inventory here).
24. C&C 1 boot ROM (planning notes s.5: 16 KB ROM copied into C&C 1, boots from LVDS or the UART, ASCII
    monitor) must be linked at 0x01000000 and use the UART through C&C 1's peripheral window; C&C 1's reset
    PC (no RUN line) undefined.
25. TRACE_REG 0xFFFFFFF0 (split_regs.h l.19, ISS only) and rv32emu RV_SENTINEL 0xFFFFFFF0 now fall in
    peripheral 3's window (word 124). Relevant to Q12's "simulator-only print register" placement.
26. Q10 consequence: byte offsets make a C pointer into data/program memory equal to its packet address
    (CM_WORD/PACKED_WORD/cmWord disappear); word offsets need a conversion everywhere. Big-SRAM addresses
    exist in two formats either way: 21-bit (PTR, STREAM_SRC, SRAM_RD_ADDR_QUEUE, DMA_RECORD_BASE_ADDRESS)
    vs 32-bit region+offset (STREAM_DEST, ROUTER_ADDR, DMA_ADDR).
27. Gaps in the standard window: bit numbers for STATUS's standard bits (run, DMA), PARAMS's type field,
    a standard event for run done / DMA done (v1.1 had sender idle [20]); PULSE_RUN/PULSE_DMA while busy;
    DMA_HDR/ADDR changed during a transfer; EVENTS_MASK reset and HALT behaviour; access to reserved words
    10-31, undefined words, unused DMA words, absent peripheral windows, controller block 0x874-0x9FC (v1.1:
    unmapped = bus error, undefined PERIPH_STATUS read 0).
28. Invariants touched: 4 (mem_idle/cm_idle), 5 (bad packet), 11 (now convention; slot-0 writes now fire
    command landed), 14 ("mode 7"), 15 (TILE_CONDITIONS, C&C error bit), 16 (SENDER_SEND, rp_latency),
    17 (mode 3 to C&C, d = 64), 18 (modes 2/4/5, mode 9).
29. to-do 13 (planning notes l.288-301): "keep ROUTER_SET_HDR as a deprecated alias" impossible after D7's
    split (different layout); SENDER_ADDR R/W moot. Nit.
30. Per-unit counters: v1.1 4 counters -> 3. Replayer pass with an LPDDR fetch + refill + request reply uses 3;
    no headroom (inferred). Sort uses at most 2 at once (bt_run.c l.64-65).

## Inventory grep (counts per file)
Done with a grep over sw/, sort/, meshsim/{sw,src,test}; table kept in REVIEW.md section 6.
Combined TILE_PULSE stores: none in software (only TileControl.java l.259-264 and CtrlTest.java l.274).
uart_putc/uart_puts: no callers outside mesh.h; UART exercised by regs.c, CtrlTest, HooksTest, RegDocTest.

## Round 2 (PROMPT-2.md)
Log re-read in full (1,096 lines, through 30). Code under wip/sw, wip/sort, wip/meshsim unchanged since round 1.
Compiled a struct-per-block draft with riscv64-unknown-elf-gcc 13.2 (-march=rv32i_zmmul -O2), in /home/claude/st:
- single access: `lw a0,-2024(zero)` (x0-relative, as v1.1);
- two or more accesses in a function: GCC hoists `li a5,-2048` and uses offsets -> +1 instruction on the
  GO -> start path (wait_for_command's load of EVENTS, counter, PULSE_DMA) vs v1.1's MESH_REG form, which
  stays x0-relative throughout;
- `TileRegs t = tile;` compiles and loads all 128 words (consumes EVENTS, pops REQUEST_RECEIVE);
  `snap = tile` (assignment) is rejected only because members are const;
- R registers as `const volatile` give compile errors on stores: good;
- request-port shared offsets need anonymous unions (C11);
- "type extends PeripheralStd" needs a member-list macro (no inheritance in C) or -fms-extensions.
Findings drafted: credit leak (expansion queue, short pushes) -> deadlock; item-FIFO kind missing (tags,
25); reset values (done, gate, GO pending into ROM; W1-23 overlap); struct issues incl. `Tile` clash with
skew.h l.154; 28 review; alignment of auto-advancing runs (regs.c l.40-42 relies on misaligned 64-bit);
ERRORS [1] vs [7] for misaligned read-port reads; stale text list; TRACE word flagged by meshsim;
STREAM_COUNT 16 bits vs the full-size sort's n = 65,536 (local.c l.33, 63-64, 79).
