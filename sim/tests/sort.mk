# sort.mk: WORKER24's rules (P6: the sort and its kit), included by sim/Makefile.
# The kit's tools: v1.3's SortSummary and SortCompare (tests/sort_summary_main.cpp, sort_compare_main.cpp).
$(B)/sort_summary: $(B)/tests/sort_summary_main.o $(B)/tests/sha256.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/sort_compare: $(B)/tests/sort_compare_main.o $(B)/tests/sha256.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/test_sort_kit: $(B)/tests/test_sort_kit.o $(B)/tests/sha256.o $(HARNESS) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/sort_summary $(B)/sort_compare
