// rv32_v135.h: v1.35's core, `ref`'s Controller.java (v1.3) line for line: one instruction a cycle, with stall cycles
// (a taken branch or jump 1, a multiply 2, a register-block or window load 1). Included inside `struct Rv32`
// (rv32.h), whose step runs it while `v135_platform` is set: the tile's register block, C&C and firmware are still
// v1.35's, and their protocols and references hold this core's cycles. The platform stage deletes this file, the
// option and the lines of rv32.h marked `v135_platform`.
//
// `v135_step(env, t)` is Controller.runCycle(t), preceded by the catch-up of the cycles since the core last ran (it
// sleeps through a stall, a WFI and the end of its run). Its environment is the tile's (rv32.h's header), with
// `irq()`, ERRORS != 0, as the maskable external interrupt.

    // ---- v1.35's state ----
    uint32_t stall = 0;
    uint64_t load_wait_t = kNever; uint32_t load_wait_pc = 0;
    uint64_t stall_branch = 0, stall_mul = 0;
    enum : uint32_t { V135_WFI = 0x10500073 };

    void v135_took_wake(bool go, int64_t entered, uint64_t t, bool rom) {
        if (go) go_wakes++; else event_wakes++;
        if (rom) return;
        if (wfi_n < kWfiLog) { wfi_log[wfi_n][0] = entered; wfi_log[wfi_n][1] = int64_t(t); wfi_log[wfi_n][2] = go ? 1 : 2; }
        wfi_n++;
    }

    // ================================================================ the cycle
    // Controller.runCycle(t), after catching up the cycles slept since `last`. Returns the next cycle the core needs
    // to run (kNever: only if its inputs change). A step at a cycle it didn't ask for is exactly ref's runCycle, so a
    // core stepped every cycle and one stepped only when it asks end in the same state.
    template <class Env>
    UNITS_NOINLINE uint64_t v135_step(Env& e, uint64_t t) {
        ck_t = t;                                                              // the cycle of this step's facts
        if (stop != Stop::running && stop != Stop::parked) return kNever;   // the run ended: nothing more (mcycle frozen)
        v135_catch_up(t);
        mcycle++;                                                       // 1.
        // 2. halted: a halt ends the run (below), so never here.
        if constexpr (requires { e.send_refused(); }) {                 // 3. a refused send: retry, the core stalls
            if (pending_t == t - 1 && e.send_refused()) {
                e.send(send_w[0], send_w[1], send_w[2]);
                pending_t = t; stall_send++; cycles_stalled++;
                return t + 1;
            }
        }
        if (v135_interrupt_pending(e) && stall == 0) {                       // 4.
            in_wfi = false; v135_trap(t, CAUSE_EXTERNAL, 0, pc, pc);
            return v135_after(t);
        }
        if (in_wfi) {                                                   // 5.
            if (!e.wake()) { cycles_wfi++; return kNever; }
            in_wfi = false;
            v135_took_wake(e.reg(EVENTS) >> EV_GO & 1, wfi_at, t, wfi_in_rom);   // statistics
            woken_in_done(e, t);
            if (log) log->add(t, T_WAKE, (e.reg(EVENTS) >> EV_GO & 1) ? 1 : 2);
        }
        if (stall > 0) { stall--; cycles_stalled++; return t + 1 + stall; }   // 6.
        v135_fetch_execute(e, t);                                            // 7.
        return v135_after(t);
    }

    // The cycles t0 = last + 1 .. t - 1 were skipped: each would have been a stall cycle or a cycle asleep in WFI (the
    // core asked for nothing earlier, and nothing it reads changed, or it would have been stepped).
    void v135_catch_up(uint64_t t) {
        uint64_t gap = t - last - 1;
        last = t;
        if (gap == 0) return;
        mcycle += gap;
        if (in_wfi) { cycles_wfi += gap; return; }
        DCHECK(gap <= stall, "core skipped %llu cycles with %u stall cycles left",
               (unsigned long long)gap, stall);
        stall -= uint32_t(gap); cycles_stalled += gap;
    }
    uint64_t v135_after(uint64_t t) {
        if (halt != RUN || ub) { stop = halt != RUN ? Stop::halt : Stop::ub; stop_cycle = t; return kNever; }
        // parked (the handler's park loop): the run's end as recorded (stop, stop_cycle), but the core goes on running
        // its loop every cycle, as ref's runCycle does (only a halt stops it)
        if (parked && stop == Stop::running) { stop = Stop::parked; stop_cycle = t; }
        if (in_wfi) return kNever;
        return t + 1 + stall;
    }
    template <class Env>
    bool v135_interrupt_pending(Env& e) const { return (mstatus & MSTATUS_MIE) && (mie & MEIE) && e.irq(); }

    // The core's digest: its state and its memories, not their addresses. A core that slept (ready-driven) and one stepped
    // every cycle (step-all) differ only in what catch_up adds per skipped cycle, so those are folded in relative to
    // `last`: mcycle and the stall and WFI cycle counts grow by one per cycle in every state the core can sleep in, and the
    // stall's end cycle doesn't move.
    uint64_t v135_digest() const {
        uint64_t h = 0;
        for (uint32_t v : x) h = digest_mix(h, v);
        const uint64_t w[] = {pc, mstatus, mie, mtvec, mscratch, mepc, mcause, mtval, mcycle - last, uint64_t(halt),
                              uint64_t(in_wfi) | uint64_t(in_handler) << 1 | uint64_t(parked) << 2 | uint64_t(ub) << 3,
                              stall ? stall + last : 0, load_wait_t, load_wait_pc, uint64_t(stop), stop_cycle,
                              instructions, cycles_stalled + cycles_wfi - last, stall_branch, stall_mul, stall_reg_load,
                              stall_request, traps, bus_errors, illegals, violations, alias_accesses, reserved_accesses,
                              trace_stores};
        for (uint64_t v : w) h = digest_mix(h, v);
        for (uint32_t i = 0; i < MEM_WORDS; i++) h = digest_mix(h, uint64_t(dmem[i]) | uint64_t(imem[i]) << 32);
        return h;
    }

    // ================================================================ traps and halts
    void v135_trap(uint64_t t, uint32_t cause, uint32_t tval, uint32_t at, uint32_t resume) {
        if (in_handler && cause != CAUSE_EXTERNAL) { stop_loudly(t, HALT_TRAP2, at, cause); return; }
        traps++; trap_at = int64_t(t); trap_cause = int32_t(cause);
        mepc = cause == CAUSE_EXTERNAL ? resume : at; mcause = cause; mtval = tval;
        mstatus = (mstatus & ~(MSTATUS_MIE | MSTATUS_MPIE)) | ((mstatus & MSTATUS_MIE) ? uint32_t(MSTATUS_MPIE) : 0u);
        in_handler = true; pc = mtvec & ~3u; stall = 0;
        if (log) log->add(t, T_TRAP, 0, mcause, mepc, mtval, pc);
    }
    // ecall, ebreak, a fetch fault, a misaligned jump, a trap inside the handler: ref stops loudly (and counts a violation).
    // (`detail`: the checker's text, as ref's stopLoudly message: the fetch's pc, the jump's target, the trap's cause)
    void stop_loudly(uint64_t t, int why, uint32_t at, uint32_t detail = 0) {
        violations++;
        if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_HALT, uint64_t(why), detail);
        halt_with(t, why, at);
    }
    template <class Env>
    void v135_illegal(Env& e, uint64_t t, uint32_t ipc, uint32_t ins) {
        illegals++; e.post(BUS_ERROR, COND_ILLEGAL, 0);
        v135_trap(t, CAUSE_ILLEGAL, ins, ipc, ipc);
    }
    template <class Env>
    void v135_bus_error(Env& e, uint64_t t, uint32_t ipc, uint32_t ins, uint32_t ea) {
        bus_errors++; e.post(BUS_ERROR, COND_BUS_ERROR, 0);
        uint32_t op = ins & 0x7F;
        v135_trap(t, op == 0x23 ? CAUSE_STORE_FAULT : op == 0x03 ? CAUSE_LOAD_FAULT : CAUSE_FETCH, ea, ipc, ipc);
    }
    void undefined(uint64_t t, bool store, uint32_t width, uint32_t ea, uint32_t ipc) {
        ub = true;
        if (log) log->add(t, T_UB, store, ea, ipc, width);
        if (ub_fatal_tile >= 0)                                // (harness-only; the request 11): ref's UndefinedBehavior
            FATAL("undefined behavior at cycle %llu, tile %d, controller: a misaligned %u-byte %s 0x%x at pc 0x%x "
                  "(DECISIONS-v1.25 s8: meshsim stops; the hardware does nothing special)", (unsigned long long)t, ub_fatal_tile,
                  width, store ? "store to" : "load from", ea, ipc);
    }
    int ub_fatal_tile = -1;                // (harness-only): >= 0, the run stops at undefined behaviour, naming this tile

    // ================================================================ fetch and execute
    template <class Env>
    UNITS_NOINLINE void v135_fetch_execute(Env& e, uint64_t t) {
        uint32_t ipc = pc, ins;
        if ((ipc & 3) == 0 && region(ipc) == R_PMEM) { ins = imem[mem_word(ipc)]; if (first_image_at < 0) first_image_at = int64_t(t); }
        else if ((ipc & 3) == 0 && region(ipc) == R_BOOTROM && offset(ipc) >> 2 < bootrom_words) ins = bootrom[offset(ipc) >> 2];
        else { stop_loudly(t, HALT_FETCH, ipc, ipc); return; }
        v135_execute(e, t, ins, ipc);
    }

    template <class Env>
    void v135_execute(Env& e, uint64_t t, uint32_t ins, uint32_t ipc) {
        instructions++;
        const uint32_t op = ins & 0x7F, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31,
                       rs2 = (ins >> 20) & 31, f7 = ins >> 25;
        const int32_t sins = int32_t(ins);
        const uint32_t a = x[rs1], b = x[rs2];
        uint32_t next = ipc + 4, res = 0;
        bool wr = false;
        switch (op) {
        case 0x37: res = ins & 0xFFFFF000u; wr = true; break;                                   // lui
        case 0x17: res = ipc + (ins & 0xFFFFF000u); wr = true; break;                           // auipc
        case 0x6F: {                                                                            // jal
            uint32_t imm = uint32_t(sins >> 31) << 20 | (ins & 0xFF000) | ((ins >> 20) & 1) << 11 | ((ins >> 21) & 0x3FF) << 1;
            res = next; wr = true; next = ipc + imm; stall += timing.branch_penalty; stall_branch += timing.branch_penalty;
            if (next & 3) { stop_loudly(t, HALT_JUMP, ipc, next); return; }
            if (next == ipc && in_handler) parked = true;                                       // the handler's park loop
            break;
        }
        case 0x67:                                                                              // jalr
            if (f3 != 0) { v135_illegal(e, t, ipc, ins); return; }
            res = next; wr = true; next = (a + uint32_t(sins >> 20)) & ~1u;
            stall += timing.branch_penalty; stall_branch += timing.branch_penalty;
            if (next & 3) { stop_loudly(t, HALT_JUMP, ipc, next); return; }
            break;
        case 0x63: {                                                                            // branches
            uint32_t imm = uint32_t(sins >> 31) << 12 | ((ins >> 7) & 1) << 11 | ((ins >> 25) & 0x3F) << 5 | ((ins >> 8) & 0xF) << 1;
            bool take;
            switch (f3) {
            case 0: take = a == b; break;
            case 1: take = a != b; break;
            case 4: take = int32_t(a) < int32_t(b); break;
            case 5: take = int32_t(a) >= int32_t(b); break;
            case 6: take = a < b; break;
            case 7: take = a >= b; break;
            default: v135_illegal(e, t, ipc, ins); return;
            }
            if (take) { next = ipc + imm; stall += timing.branch_penalty; stall_branch += timing.branch_penalty; }
            break;
        }
        case 0x03: {                                                                            // loads
            uint32_t ea = a + uint32_t(sins >> 20);
            if (timing.reg_load_latency > 1 && in_regs(ea) && !(load_wait_t == t - 1 && load_wait_pc == ipc)) {
                load_wait_t = t; load_wait_pc = ipc; instructions--; stall_reg_load++; cycles_stalled++;
                return;                                       // a block or window load samples in its second cycle
            }
            uint32_t width = 1u << (f3 & 3);
            if (f3 > 5 || f3 == 3) { v135_illegal(e, t, ipc, ins); return; }
            if (ea & (width - 1)) { undefined(t, false, width, ea, ipc); return; }
            uint32_t v;
            if (!load(e, ea, width, v)) { v135_bus_error(e, t, ipc, ins, ea); return; }
            uint32_t sh = (ea & 3) * 8;
            switch (f3) {
            case 0: res = uint32_t(int32_t(int8_t(v >> sh))); break;
            case 1: res = uint32_t(int32_t(int16_t(v >> sh))); break;
            case 2: res = v; break;
            case 4: res = (v >> sh) & 0xFF; break;
            default: res = (v >> sh) & 0xFFFF; break;
            }
            wr = true;
            break;
        }
        case 0x23: {                                                                            // stores
            uint32_t ea = a + (uint32_t(sins >> 25) << 5 | rd);
            if (f3 == 2 && in_regs(ea) && !in_windows(ea) && (ea & 3) == 0 && (ea - REG_BASE) >> 2 == REQUEST_SEND
                && !e.can_push()) {
                if (log) log->add(t, T_REFUSED);
                instructions--; stall_request++; cycles_stalled++;
                return;                                       // a full request FIFO: re-executed at t + 1
            }
            if (f3 == 2 && in_windows(ea) && window_present(window_of(ea))) {
                if constexpr (requires { e.window_stalls(0u, 0u); }) {
                    if (e.window_stalls(window_of(ea), (ea & 0x1FF) >> 2)) {
                        instructions--; cycles_stalled++;
                        return;                                   /* 8.14: UART_TX while the TX FIFO is full: retried */
                    }
                } else FATAL("tile core: a store to peripheral %u's window at 0x%x: this bench has no peripheral slot", window_of(ea), ea);
            }
            if (f3 > 2) { v135_illegal(e, t, ipc, ins); return; }
            uint32_t width = 1u << f3;
            if (ea & (width - 1)) { undefined(t, true, width, ea, ipc); return; }
            uint32_t sh = (ea & 3) * 8;
            uint32_t mask = (f3 == 2 ? 0xFFFFFFFFu : f3 == 1 ? 0xFFFFu : 0xFFu) << sh;
            if (!store(e, t, ea, b << sh, mask, ipc)) { v135_bus_error(e, t, ipc, ins, ea); return; }
            break;
        }
        case 0x13: {                                                                            // op-imm
            uint32_t imm = uint32_t(sins >> 20), sh = rs2;
            switch (f3) {
            case 0: res = a + imm; break;
            case 2: res = int32_t(a) < int32_t(imm); break;
            case 3: res = a < imm; break;
            case 4: res = a ^ imm; break;
            case 6: res = a | imm; break;
            case 7: res = a & imm; break;
            case 1: if (f7 != 0) { v135_illegal(e, t, ipc, ins); return; } res = a << sh; break;
            default:
                if (f7 == 0) res = a >> sh;
                else if (f7 == 0x20) res = uint32_t(int32_t(a) >> sh);
                else { v135_illegal(e, t, ipc, ins); return; }
            }
            wr = true;
            break;
        }
        case 0x33:                                                                              // op
            if (f7 == 1) {                                    // Zmmul; the divide opcodes are illegal
                int64_t la = int32_t(a), lb = int32_t(b);
                switch (f3) {
                case 0: res = a * b; break;
                case 1: res = uint32_t(uint64_t(la * lb) >> 32); break;
                case 2: res = uint32_t(uint64_t(la * int64_t(uint64_t(b))) >> 32); break;
                case 3: res = uint32_t((uint64_t(a) * uint64_t(b)) >> 32); break;
                default: v135_illegal(e, t, ipc, ins); return;
                }
                stall += timing.mul_latency - 1; stall_mul += timing.mul_latency - 1;
            } else if (f7 == 0 || f7 == 0x20) {
                if (f7 == 0x20 && f3 != 0 && f3 != 5) { v135_illegal(e, t, ipc, ins); return; }
                switch (f3) {
                case 0: res = f7 == 0 ? a + b : a - b; break;
                case 1: res = a << (b & 31); break;
                case 2: res = int32_t(a) < int32_t(b); break;
                case 3: res = a < b; break;
                case 4: res = a ^ b; break;
                case 5: res = f7 == 0 ? a >> (b & 31) : uint32_t(int32_t(a) >> (b & 31)); break;
                case 6: res = a | b; break;
                default: res = a & b; break;
                }
            } else { v135_illegal(e, t, ipc, ins); return; }
            wr = true;
            break;
        case 0x0F: if (f3 > 1) { v135_illegal(e, t, ipc, ins); return; } break;                      // fence: a no-op
        case 0x73:
            if (ins == 0x00000073) { stop_loudly(t, HALT_ECALL, ipc); return; }
            if (ins == 0x00100073) { stop_loudly(t, HALT_EBREAK, ipc); return; }
            if (ins == V135_WFI) {
                if (in_handler && !v135_interrupt_pending(e)) parked = true;
                wfi_at = int64_t(t);                          // statistics (wfiLog)
                if (!e.wake()) { in_wfi = true; wfi_in_rom = region(ipc) == R_BOOTROM; }
                else { v135_took_wake(e.reg(EVENTS) >> EV_GO & 1, int64_t(t), t, region(ipc) == R_BOOTROM); woken_in_done(e, t); }   // falls through
            } else if (ins == 0x30200073) {                   // mret: legal, flagged (every trap is terminal)
                violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_MRET);
                next = mepc;
                mstatus = (mstatus & ~MSTATUS_MIE) | ((mstatus & MSTATUS_MPIE) ? uint32_t(MSTATUS_MIE) : 0u) | MSTATUS_MPIE;
            } else if (f3 != 0 && f3 != 4) {                  // the CSR instructions
                uint32_t csr = ins >> 20, old;
                if (!v135_csr_read(e, csr, old)) { v135_illegal(e, t, ipc, ins); return; }
                uint32_t src = f3 >= 5 ? rs1 : a;
                bool writes = (f3 & 3) == 1 || rs1 != 0;
                if (writes) {
                    uint32_t v = (f3 & 3) == 1 ? src : (f3 & 3) == 2 ? old | src : old & ~src;
                    if (!v135_csr_write(csr, v)) { v135_illegal(e, t, ipc, ins); return; }
                }
                res = old; wr = true;
            } else { v135_illegal(e, t, ipc, ins); return; }
            break;
        default: v135_illegal(e, t, ipc, ins); return;
        }
        bool w = wr && rd != 0;
        if (log) log->add(t, T_RETIRE, w, ipc, ins, a, b, res);
        if (w) {
            x[rd] = res;
            if (min_sp_watch && rd == 2 && res != 0 && int32_t(res) < min_sp) min_sp = int32_t(res);   // (above)
        }
        pc = next;
    }

    // ================================================================ the CSRs
    template <class Env>
    bool v135_csr_read(Env& e, uint32_t csr, uint32_t& v) const {
        uint64_t mt = mcycle / (timing.ref_cycle ? timing.ref_cycle : 1);
        switch (csr) {
        case MSTATUS: v = mstatus | 3u << 11; return true;
        case MISA: v = 1u << 30 | 1u << 8 | 1u << 12; return true;
        case MIE_CSR: v = mie; return true;
        case MTVEC: v = mtvec; return true;
        case MSCRATCH: v = mscratch; return true;
        case MEPC: v = mepc; return true;
        case MCAUSE: v = mcause; return true;
        case MTVAL: v = mtval; return true;
        case MIP: v = e.irq() ? uint32_t(MEIE) : 0u; return true;
        case MCYCLE: v = uint32_t(mcycle); return true;
        case MCYCLEH: v = uint32_t(mcycle >> 32); return true;
        case MTIME: v = uint32_t(mt); return true;
        case MTIMEH: v = uint32_t(mt >> 32); return true;
        default: return false;
        }
    }
    bool v135_csr_write(uint32_t csr, uint32_t v) {
        switch (csr) {
        case MSTATUS: mstatus = v & (MSTATUS_MIE | MSTATUS_MPIE); return true;
        case MIE_CSR: mie = v & MEIE; return true;
        case MTVEC: mtvec = v & ~3u; return true;
        case MSCRATCH: mscratch = v; return true;
        case MEPC: mepc = v & ~3u; return true;
        case MCAUSE: mcause = v; return true;
        case MTVAL: mtval = v; return true;
        case MCYCLE: mcycle = (mcycle & ~0xFFFFFFFFull) | v; return true;
        case MCYCLEH: mcycle = (mcycle & 0xFFFFFFFFull) | uint64_t(v) << 32; return true;
        case MISA: case MIP: return true;                                     // read-only here: writes ignored
        default: return false;                                                // mtime and mtimeh included
        }
    }
