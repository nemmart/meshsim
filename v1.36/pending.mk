# pending.mk: what v1.36's move to the RTL's hardware broke and nobody has ported yet. NOT BUILT: each of these tests and
# tools is on v1.35's register map, control protocol (GO, DONE, commands), core, tile or firmware images, which v1.36
# replaces with the RTL's (spec/CHANGES-v1.35-to-v1.36.md). Nothing here is deleted or weakened: the sources and the
# reference logs stay, and PENDING-v1.36.md has a row for each (what, why, what closes it; the platform stage, section
# 3, and the replayer's and the Tracer's stages). `make pending` lists them; `./run_tests.sh` prints its pending steps
# and counts them in its summary. An item leaves this file when it is ported: by the test's own commit.

# ---- make check's tests (tests/test_<name>.cpp) ----
# v1.35's M1 bench (src/units/controller.h: the core with v1.35's register block and test bench) and its goldens: retire
# traces and fingerprints of v1.35's images on v1.35's core.
PENDING_TESTS := test_regblock test_golden test_controllers test_broken_units test_statefp_core test_g1_replay \
                 test_statefp_g1
# One tile's control part driven by v1.35's stand (GO, the command block, v1.35's registers).
PENDING_TESTS += test_g1_tile test_tile_slot test_tileslotconf
# Unit conformance against v1.3's observations of v1.35's address generator, gather unit and memory path, cycle by
# cycle: SUPERSEDED. v1.36's units are the specification's (8-15), so no line of those observations can be produced
# again; test_ag, test_gather, test_mem and test_receive, written from the specification, stand for them. Kept unbuilt
# until the old suites are deleted (PENDING-v1.36.md, the platform stage, sections 3 and 7).
PENDING_TESTS += test_engconf test_memconf
# v1.35's conformance of C&C 1's peripheral against ref (READY_LINES, PULSE_GO, the row collectors' words, a toast pin on
# any error line): SUPERSEDED by test_ingress_egress (17.6 from the specification and the RTL's unit); kept unbuilt
# until the old suites are deleted.
PENDING_TESTS += test_cnc1_periph
# ref's SlotConf scripts store and load v1.35's control-block words (the run and DMA pulses at 3 and 4, EVENTS at 5-7, the
# DMA at 8 and 9) and reserved words at random, on a slot with the replayer in it, and their observations are ref's of
# v1.35's replayer and DMA. v1.36's control and DMA blocks are tested from the specification and the RTL's
# peripheral_control and peripheral_dma in test_periph_slot; a conformance against the RTL's blocks closes it.
PENDING_TESTS += test_slotconf
# What v1.3's suites and goldens drive on the chip: v1.35's images under C&C's commands, C&C 1-3 as cores, v1.3's
# fingerprints. (tests/mach.h itself is ported: test_mach; the chip end to end is test_rtl_sort.) test_checkself: ref's
# CheckSelf sequence, which provokes
# the checker through v1.35's commands, replies and strays. test_p4suites, test_ctl_suites, test_sort_suites,
# test_stageb run the suite programs that run_tests.sh has pending; test_stageb, stageb and replay_shard run v1.35's
# replay controller on v1.35's tile (they passed on the replayer stage's branch, whose tile was still v1.35's);
# test_t2_harness boots v1.35's C&C 3 with v1.35's tracer firmware.
PENDING_TESTS += test_checkself test_chip test_sort test_cc_units test_cc_req test_t2_harness test_stageb \
                 test_p4suites test_ctl_suites test_sort_suites

# ---- the other default targets ----
PENDING_BIN := golden bench_controller replay_shard stageb
# bench/sort's bench_sort and bench_chip time v1.35's sort images on v1.35's machine (the M1 bench, C&C's commands, the
# phases from GO): they do not compile, and `make build/<cc>-<mode>/bench_sort` says so (bench/sort/sort.mk).
PENDING_BIN += bench_sort bench_chip

# $(call live,<targets>): the targets but the pending ones.
live = $(filter-out $(PENDING_BIN:%=$(B)/%),$(1))

.PHONY: pending
pending:
	@echo "pending tests (not built; PENDING-v1.36.md):"; for t in $(PENDING_TESTS); do echo "  PENDING $$t"; done
	@echo "pending tools:"; for t in $(PENDING_BIN); do echo "  PENDING $$t"; done
