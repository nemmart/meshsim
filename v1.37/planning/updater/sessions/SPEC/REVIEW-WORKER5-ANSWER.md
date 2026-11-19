# WORKER5's answer: the cache, the AXI arbiter and the LPDDR model

*To `REVIEW-WORKER5.md`. Read against the code on `main` at `e7243dd`, which holds WORKER5's code unchanged from
`updater-w5`: `src/units/cache_ctl.{h,cpp}`, `cache_memory.h`, `cache_system.h`, `axi_arbiter.{h,cpp}`,
`lpddr_model.{h,cpp}`. File paths are in `v1.37_wip/`, and "cache_ctl.cpp:223" is a line of the file on `main`.*

I re-measured on `main`:
- `test_cache cache_idle_figures` prints 6, 173, 87, 10, 351, 9 and 17 cycles, as 23.6.7 has them.
- `test_axi_arbiter arbiter_a_line_takes_80` puts a line read's first beat at *t* + 72 and its last at *t* + 79.
- A line write I ran alone, client 0, beats offered at once, had its address taken at 1 and its response at the
  client at 81, so *t* + 80.
- One run of mine, shown under 1.1, gives the bug of 2.1.

## Your 21 claims

| # | Verdict |
|---|---|
| 1 | **Right.** 1,539 = 4 × 264 + 483, at the hardware's widths. The code's lists are 16 deep with a response field in both; at `CLIENT_BURSTS` 4 your 2-bit head and 3-bit count are what hardware needs, and the read list's response field is unused (`axi_arbiter.h`, `List`) |
| 2 | **Right** (`axi_arbiter.cpp:edge`: controller side, clients' side, AR turn at 102, AW turn at 117, outputs) |
| 3 | **Right, with a condition** (1.6): the slot empties in that edge only if the AR output is free and the turn goes to that client |
| 4 | **Right** (`axi_arbiter.cpp:145`; the AW turn at 117 tests `!w_cur_` after step 2 cleared it) |
| 5 | **Right** (`axi_arbiter.cpp:59`). The pieces of one burst have one ID, so "in the order the responses came" is also "in piece order" |
| 6 | **Right as argued.** AR and AW each skip empty slots from the pointer, so a waiting slot is served within 4 turns of its direction. The only unbounded waits are the ones you name: the controller's `ready`s, and on AW the client in turn sending its beats. A client that never sends its beats stops every client's writes, and nothing checks that (1.8) |
| 7 | **Right, measured:** read +72 and +79, write +80 |
| 8 | **Right** (`lpddr_model.cpp:189`: the write lands as its B is chosen, before the R beat of the same edge takes its data) |
| 9 | **Right:** 192 + 24 = 216 bits; 512 rows is 13.5 KiB |
| 10 | **Right to the count, 6,866, with one register missing** (1.4) |
| 11 | **Right, measured: 6** |
| 12 | **Right, but for the pointer** (1.5) |
| 13 | **Right in the common case; not guaranteed** (1.3) |
| 14 | **Right** (`M_FAIL`: the way is invalidated from the row copied at the hand-over, so a clean victim is lost; a dirty one was already written back) |
| 15 | **Right:** `start` sets the line count to 0, so a replayed discard begins at its first line. It does the same when it meets the miss handler's set |
| 16 | **Right** (`cache_ctl.cpp:223–230`: only `J_FREAD` sets `error_`) |
| 17 | **Right:** the B response in *t*, the reply on the bus in *t* + 1. A walker reply that lost the bus waits in `W_REPLY` and goes the next cycle; only the one slot can take the bus, so it waits one cycle |
| 18 | **Right** (`start`: fill `x_ = root + (line & 31)` at depth 0, beyond at line ≥ 32; look-up and update at depth 0 `beyond`) |
| 19 | **Right** (`TagRow`'s defaults; `CacheCtl::reset`) |
| 20 | **Partly wrong** (1.2) |
| 21 | **Right.** The idle figures are re-measured above. The percentages are Quest 209 against Quest 210, the cycles from the tracer tile's load to the windows' landing: −2.71 % to +4.39 % over the 37 proof runs; Cyclone 22's s 3 is +4.39 % and Cyclone 2²⁹'s s 0 +3.06 % (8,574 write-backs); EVM Zone 2²⁹'s s 42 is −2.71 %. "The model" there is the memory model in `mem_system.h` |
| 17.11 | **Right as to what sets it:** `error_` is set at `cache_ctl.cpp:227`, a fill read answered from a failed fetch, and at 403, a write-back's error response, and nowhere else. Two notes. It is set when the read is answered at its replay, not when the fetch's response comes. And see 1.3: a fill read whose replay finds the line already fetched by another request never sets it. The flag is not wired to any tile (2.3) |

## 1. Where the specification misdescribes what the code does

1. **23.6.4, "What `fill` requires the cache keeps: … a read of a line sees a write of it already accepted, since
   the line is in the cache, dirty, from the write's response on."** The code does not keep this; see 2.1.
   - The reasoning covers only a read accepted after the write's **response**. A read accepted after the write's
     **handshake** but before its response can be served first.
   - **Round robin:** `cache_ctl.cpp:421` takes source 4, `fill`'s read, before source 5, its write, when both
     wait and the pointer is below 4.
   - **Waiting for the miss handler:** a fill write abandoned with `wait_mh` while its read hits the old line.
   - **Measured:** a look-up kept the walker busy; then a write of line 0 (data 0x2222) was taken, and a read of
     line 0 the next cycle. The read was answered with 0x1111, the old data.
   - The same sentence in 3's `fill` row and 23.6.4 is right as a rule; it is the claim that the code keeps it that
     is wrong.
2. **23.6.8, 23.6.11, invariant 37, "the simulator catches the third and the fifth."** Too strong in one way and too
   weak in another.
   - **It catches only when the walker reads that line again while it is still in the cache and clean.** A stale
     line evicted or never read again goes unseen. A breach by the Tracer's own dirty lines is never seen, since
     dirty lines are not compared (`cache_ctl.cpp`, `step_data`, `!wdirty_`).
   - **By the same mechanism it also catches breaches of the first rule:** a page whose line was fetched before its
     direct write landed, read again while still cached.
   - **It also catches the fourth,** when an old clean line of a reused page is still cached and is read after the
     page's new contents were written directly.
   - **It cannot see the second or the sixth,** which concern direct reads, or the fourth's other case: an old
     dirty line written back over new contents.
   - Suggested: "the stale-line check catches, at the next read of a still-cached clean line, a page changed in
     LPDDR under the cache: so breaches of the first, third, fourth and fifth rules when they leave such a line;
     not the second or the sixth".
3. **23.6.5, "A request whose own fetch failed is answered at its replay, at that line"** (your claim 13). This
   holds only if the replay misses that line again.
   - The failed line is compared on a miss alone (`cache_ctl.cpp:223`).
   - **With hits under a miss:** a request the walker held when the fetch failed can miss the same line next and
     fetch it successfully. The failed request then hits at its replay and is served normally: no error, and for a
     fill read no error flag.
   - The miss handler's owner gets first pick (`prio_`), so this needs a request already in the walker.
   - Suggested: "answered at its replay with the error, if the line is still missing then; if another request has
     fetched it meanwhile, served from it".
4. **23.6.5's register table omits the guard for "a tag row read before the miss handler's write to the same set
   landed."** In the code: `tag_read_t_`, `mh_tag_write_t_` and `mh_tag_write_set_` (`cache_ctl.h`;
   `cache_ctl.cpp:168`).
   - Hardware needs the set the miss handler last wrote (9 bits) and a bit for "written in or after the cycle of
     my tag read".
   - About 10 flip-flops more: some 6,876 in all. The total's "about 6,870" stands.
   - (`diverged_`, the stale check's list of lines whose update write failed, is the simulator's, rightly left
     out.)
5. **23.6.5, the front end: "round robin … from the one after the last picked."** The pointer moves only on a
   round-robin pick (`cache_ctl.cpp:421`). A pick made for the miss handler's owner (`prio_`) does not move it. So
   it is "from the one after the last picked by round robin".
6. **23.11, "A burst of at most `TURN_BEATS` beats leaves its slot at once"** (claim 3). Only if, in that edge, the
   AR (or AW) output is free and the turn goes to that client: another client's waiting slot ahead of it in the
   round robin takes the turn.
   - For AW it also needs no write piece in turn (`axi_arbiter.cpp:117`).
   - Suggested: "at once when it gets that edge's turn".
7. **23.6.5, the AXI writer: "AW, then the line's 8 beats."** The writer offers `wvalid` with its first beat in the
   same cycle as `awvalid` (`cache_ctl.cpp:473`: `wvalid` does not wait for the address's handshake); AXI allows
   it. The beats go a beat a cycle as `wready` comes. (The arbiter takes them only once the piece is in turn.)
8. **23.13 and invariant 39 list checks the simulator does not make.**
   - **A size or a type other than 16 bytes and INCR:** `AxSIZE` and `AxBURST` are not in `axi::MasterOut`. They
     are constants of the port (`axi_port.h`), so nothing can carry another value and nothing checks one.
   - **"Its W beats back to back":** neither the arbiter nor the model stops on a client that pauses inside a write
     burst. It is a rule (23.10) the code relies on, not one it checks.
   - What it does check is the rest of 23.13's list:
     - `check_burst` in `lpddr_model.cpp` (alignment, 4 KiB, ID);
     - the arbiter's `check` (alignment and 4 KiB on a client's port);
     - the `wlast` checks in both;
     - the handshake checks on `lpddr`'s AR, AW and W.
9. **23.6.11, "Every dirty line written back must be one a fill write made."** There is no such check. It holds by
   construction: only `fill`'s write sets a dirty bit (`step_cmp`, and `M_INSTALL` for an allocation). The sentence
   is from `cache_ctl.h`'s header comment (line 48), which overstates the code; drop it, or call it a property.
10. **23.6.11 omits checks the code makes:**
    - two reads or two writes of one memory in a cycle (`cache_memory.h:53,57,92,97`);
    - a write-back's response out of turn (`cache_ctl.cpp`, the B handling);
    - cycles skipped while the cache system works (`cache_system.h:76`).

    The first matters to a builder: it is how the port use below is enforced.
11. **23.6.5 omits the memories' port priority.**
    - **The miss handler goes first:** it uses the data memory's read port in "read the victim", and both write
      ports in "install" and the tag write port in "fail".
    - **The walker then waits that cycle:** in compare on a hit (data read and tag write), in compare for a fill
      write's hit or a discard's invalidation (tag write), and in read at an update's write.
    - Code: `cache_ctl.cpp:183, 203, 212, 281`, flags `mh_data_read_` and `mh_writes_`.
    - A builder needs it: without it the walker and the miss handler collide on a port.

## 2. Where the specification is right and the code is wrong or silent

1. **`fill`'s ordering rule** (3, 17.9.5, 23.6.4: a read of a line sees a write of it already accepted) **is not
   kept** (1.1). The Tracer relies on it (`spec/jolt/tracer.md`: "no read of its line is requested until the write
   is accepted: the memory system orders only a read after a write already accepted").
   - No run failed on it: the Tracer's write-backs seldom meet a read of the same line in the narrow window.
   - It is a bug.
   - **The fix:** keep `fill`'s write ahead of its read when both wait, and hold the read while the write waits for
     the miss handler or the slot. Either serve the write register before the read register whenever it is valid,
     or compare lines and hold a read whose line equals a waiting write's.
2. **The memory system as the memory tile's peripheral 1 (17.11) is not built.**
   - No window, `PARAMS` or `ERRORS` register.
   - The cache's error flag is an output of `CacheCtl` (`out().error`) that reaches no tile's error line: on the
     chip nothing reads it.
   - Only the proof harness checks it at the end of a run (`tests/proof_bench.cpp:210`, `cachefig::faults`).
3. **The reset (23.6.10, 23.11, 17.11) is not reached from the chip.** `CacheCtl::reset`, `AxiArbiter::reset` and
   `LpddrModel::reset` exist and do what the specification says, but `CacheSystem` has no reset, and the chip's
   reset does not call them. Each is built fresh with the machine, so it is cold once.
4. **The clients' ports on the chip (17.11 "Its ports", 23.10's table) are not wired.**
   - **The Updater** is still the black box: no AXI port (client 1) and no look-up port (port 1). It writes the
     store directly.
   - **The RW engine** stays on v1.36's own slave; its adapter (`RwEngineOnArbiter`) is tested on the arbiter
     alone.
   - With `cache_ctl` on, the arbiter's only client is the cache (`cache_system.h`, `tick`).
