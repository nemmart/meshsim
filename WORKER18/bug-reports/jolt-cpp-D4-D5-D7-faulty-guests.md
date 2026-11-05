# jolt-cpp: three ways a faulty or unusual guest gets wrong rows or undefined behaviour instead of a clean error

jolt-cpp 88dca85 against a16z 7189216 (meshsim `WORKER18/UPSTREAM-DIFFS.md` D4, D5, D7).

1. **A removed UNIMP is stepped over with a drifting pc (D4).** jolt-cpp drops UNIMP words from the bytecode
   (`isa/virtual.cpp:1357`), and `finish_instruction` advances pc by the executed row's size and the index by one
   (`trace_owner.cpp:111-126`), so a program falling through an UNIMP gets rows whose pc (and link values) are behind by
   its size. a16z rejects such an ELF (`crates/jolt-program/src/image/decode.rs:81`), or for c.unimp skips it at decode
   and panics if it is executed. Reproducer: meshsim guest `unimp_fence_i` (jolt-cpp 3 rows, the last two with wrong pcs).
2. **A jump into the middle of an instruction is undefined behaviour (D5).** The target maps to bytecode index 0, which
   the interpreter doesn't handle (`trace_owner.cpp:654-655`, `unreachable`); the release build was seen aborting with
   "guest program executed UNIMP instruction". Expected: a clean "jump target not in the map" error, as the host tracer's
   job (owner's D5 ruling). Reproducer: guest `jump_into_gap`.
3. **EBREAK aborts in the JIT but ends the run in the interpreter (D7).** Both bytecodes hold `JAL vN, 0` for EBREAK, which
   a16z treats as the end (a pc stall); jolt-cpp's interpreter ends there too, but its JIT raises ERR_EBREAK
   (`transcode.cpp:2164-2165`). A program that reaches EBREAK in a JIT-run part aborts. Reproducer: guest `ebreak` shows
   the interpreter's behaviour (too short for the JIT).

**Impact**: only faulty or unusual guests (falling into padding, a corrupted jump target, an EBREAK end); (1) is silent
and gives a proof that fails verification, (2) is undefined behaviour, (3) an abort.
