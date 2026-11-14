# sort.mk: the benchmarks (bench/sort/), included by sim/Makefile.
#   bench_chip   the chip on a program from cycle 0 (the sort's boot, wfi, regs), timed, by placement (timing: clang
#                release, D18): make build/clang-release/bench_chip; run from sim/ (it reads tests/golden-m2/images)
ifeq ($(filter bench_chip,$(PENDING_BIN)),)
$(B)/bench_chip: $(B)/bench/sort/bench_chip.o $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
else
$(B)/bench_chip:
	@echo "PENDING bench_chip: v1.35's machine and images (pending.mk; PENDING-v1.36.md, the platform stage)"; false
endif
#   trace_filter footprints from a lackey memory trace and bench_chip --trace-map's region map (stand-alone)
$(B)/trace_filter: bench/sort/trace_filter.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Werror $< -o $@
#   bench_sort   the 16K sort over M2Sort's counted span (L1's GO to L2's all-READY), by placement, phases, units, windows,
#                footprint traces (timing: clang release, D18); run from sim/ with --keys (KeyGen's uniform set)
ifeq ($(filter bench_sort,$(PENDING_BIN)),)
$(B)/bench_sort: $(B)/bench/sort/bench_sort.o $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
else
$(B)/bench_sort:
	@echo "PENDING bench_sort: v1.35's machine and images (pending.mk; PENDING-v1.36.md, the platform stage)"; false
endif
