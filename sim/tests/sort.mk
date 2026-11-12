# sort.mk: WORKER24's rules (P6: the sort and its kit), included by sim/Makefile.
# The kit's tools: v1.3's SortSummary and SortCompare (tests/sort_summary_main.cpp, sort_compare_main.cpp).
$(B)/sort_summary: $(B)/tests/sort_summary_main.o $(B)/tests/sha256.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/sort_compare: $(B)/tests/sort_compare_main.o $(B)/tests/sha256.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/test_sort_kit: $(B)/tests/test_sort_kit.o $(B)/tests/sha256.o $(HARNESS) $(LIB_OBJ)   # the tools' mains included (SORT_KIT_NO_MAIN)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/sort_summary $(B)/sort_compare
# the sort driver (tests/sort_main.cpp: v1.3's SortTest) on W20's machine harness
$(B)/sort_test: $(B)/tests/sort_main.o $(B)/tests/sha256.o $(B)/tests/statefp.o $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(MACH_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $(sort $^) -o $@ $(WRAP)
# sort_test's quick checks in make check (test_sort_suites runs it; order-only: built with it, not linked into it)
$(B)/test_sort_suites: | $(B)/sort_test
# Sweep (tests/sweep_main.cpp: v1.3's Sweep) on the machine harness
$(B)/sweep: $(B)/tests/sweep_main.o $(B)/tests/sha256.o $(MACH_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $(sort $^) -o $@ $(WRAP)
