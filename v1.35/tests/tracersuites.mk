# tracersuites.mk: the rules (P7a): v1.3's Tracer suites as C++ programs on the suite.h, and the test that
# runs them in make check, each one's output against ref's log (tests/tracersuites/, from oracle/ref/tracersuites.sh).
#   suite_memsys   MemorySystemTest (run_tests.sh's step memsys)
#   suite_rvc      RvcTest (step rvc)
#   suite_tracer   TracerTest on TracerRig (step tracer)
TRACER_SUITES := $(B)/suite_memsys $(B)/suite_rvc $(B)/suite_tracer
$(B)/suite_memsys: $(B)/tests/suite_memsys.o $(B)/tests/suite.o $(TRACER_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/suite_rvc: $(B)/tests/suite_rvc.o $(B)/tests/suite.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/suite_tracer: $(B)/tests/suite_tracer.o $(B)/tests/tracer_rig.o $(B)/tests/suite.o $(TRACER_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
# the suites are run by test_tracer_suites (order-only: built with it, not linked into it)
$(B)/test_tracer_suites: | $(TRACER_SUITES)
$(B)/test_tracer_rig: $(B)/tests/tracer_rig.o $(TRACER_LINK) | $(B)/suite_tracer
