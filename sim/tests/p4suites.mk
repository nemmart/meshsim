# p4suites.mk: WORKER22's rules (P4, inventory C3-C7, C11, C17, G1): v1.3's network, memory and engine suites as C++
# programs on W20's harness (tests/mach.h, tests/suite.h), each one's output against ref's log of the same run_tests.sh
# step (tests/p4suites/, from oracle/ref/p4suites.sh). Included by sim/Makefile (one line).
#   suite_net   NetTest (steps net, net-shuf, net-shuf2, skewtable)
#   suite_mem   MemTest (steps mem, mem-shuf, mem-shuf2)
P4_SUITES := $(B)/suite_net $(B)/suite_mem
$(B)/suite_net: $(B)/tests/suite_net.o $(MACH_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/suite_mem: $(B)/tests/suite_mem.o $(MACH_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
# the suites are run by test_p4suites (order-only: built with it, not linked into it)
$(B)/test_p4suites: | $(P4_SUITES)
