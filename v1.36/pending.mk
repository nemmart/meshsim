# pending.mk: what v1.36's platform stage broke and nobody has ported yet. NOT BUILT: each of these tests and tools is
# on v1.35's register map, control protocol (GO, DONE, commands), core or firmware images, which v1.36 replaces with the
# RTL's (spec/CHANGES-v1.35-to-v1.36.md). Nothing here is deleted or weakened: the sources stay, and
# PENDING-v1.36.md has a row for each (what, why, what closes it). `make pending` lists them; `./run_tests.sh` prints
# its pending steps and counts them in its summary. An item leaves this file when it is ported: by the test's own
# commit.

# ---- make check's tests (tests/test_<name>.cpp) ----
# v1.35's M1 bench (src/units/controller.h: the core with v1.35's register block and test bench) and its goldens: retire
# traces and fingerprints of v1.35's images on v1.35's core.
PENDING_TESTS := test_rv32 test_regblock test_golden test_controllers test_broken_units test_statefp_core test_g1_replay \
                 test_core_m2 test_statefp_g1
# One tile's control part driven by v1.35's stand (GO, the command block, v1.35's registers).
PENDING_TESTS += test_g1_tile test_tile_slot test_tileslotconf
# Unit conformance against v1.3's observations, whose scripts hold v1.35's DMA_CONFIG and table layouts.
PENDING_TESTS += test_ag test_gather test_mem test_engconf test_memconf
# The machine (tests/mach.h) and what is built on it: v1.35's chip with C&C's model or C&C 1-3 as cores, v1.35's images.
PENDING_TESTS += test_checkself test_chip test_sort test_cc_units test_cc_req test_t2_harness test_stageb \
                 test_p4suites test_ctl_suites test_sort_suites
# The register-table generator's tables (tools/regs): v1.35's map.
PENDING_TESTS += test_regs

# ---- the other default targets ----
PENDING_BIN := golden bench_controller replay_shard stageb

# $(call live,<targets>): the targets but the pending ones.
live = $(filter-out $(PENDING_BIN:%=$(B)/%),$(1))

.PHONY: pending
pending:
	@echo "pending tests (not built; PENDING-v1.36.md):"; for t in $(PENDING_TESTS); do echo "  PENDING $$t"; done
	@echo "pending tools:"; for t in $(PENDING_BIN); do echo "  PENDING $$t"; done
