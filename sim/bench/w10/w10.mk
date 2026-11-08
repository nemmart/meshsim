# w10.mk: WORKER10's benchmarks (bench/w10/), included by sim/Makefile.
#   bench_chip   W9's chip on a program from cycle 0 (the sort's boot, wfi, regs), timed, by placement (timing: clang
#                release, D18): make build/clang-release/bench_chip; run from sim/ (it reads tests/golden-m2/images)
$(B)/bench_chip: $(B)/bench/w10/bench_chip.o $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
#   trace_filter footprints from a lackey memory trace and bench_chip --trace-map's region map (stand-alone)
$(B)/trace_filter: bench/w10/trace_filter.cpp
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Werror $< -o $@
#   bench_sort   the 16K sort over M2Sort's counted span (L1's GO to L2's all-READY), by placement, phases, units, windows,
#                footprint traces (timing: clang release, D18); run from sim/ with --keys (KeyGen's uniform set)
$(B)/bench_sort: $(B)/bench/w10/bench_sort.o $(NET_LINK) $(UNITS_LINK) $(GOLDEN_OBJ) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
