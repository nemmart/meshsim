# WORKER3 running notes

Review of DECISIONS-v1.2 (registers, peripherals, software impact). Running notes; REVIEW.md is the
deliverable. Line numbers: D = `wip/planning/DECISIONS-v1.2.md`, S = `wip/spec/machine-spec.md`.

## Status
- [x] Read DECISIONS-v1.2 in full
- [x] Read spec 4.11-4.20 (relevant parts), 6.1-6.6, 8
- [ ] REGISTERS.md, mesh.h, CONVENTIONS/RECIPES
- [ ] sort code, meshsim code: migration inventory
- [ ] register-map overlap check (script)
- [ ] write REVIEW.md

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
