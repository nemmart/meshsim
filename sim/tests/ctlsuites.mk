# ctlsuites.mk: WORKER23's rules (P5): v1.3's control suites, programs and recipes as C++ programs on W20's harness
# (tests/mach.h, suite.h), and the test that runs them in make check, each one's output against ref's log
# (tests/ctlsuites/, from oracle/ref/ctlsuites.sh). The programs are named by v1.3's suites; run_tests.sh's steps
# call them with v1.3's arguments.
#   suite_cccores   CcCoresTest (steps cccores, cccores-shuf)
#   suite_ccbench   CcBench (step ccbench)
#   suite_ctrl      CtrlTest (steps ctrl, ctrl-shuf, ctrl-full; in progress)
#   suite_recipes   RecipeTest (steps recipes, recipes-shuf; in progress)
#   suite_hooks     HooksTest (steps hooks, hooks-shuf)
#   suite_regdoc    RegDocTest (step regdoc)
#   suite_accounting AccountingTest (steps accounting, accounting-full; the suite or debug build)
CTL_SUITES := $(B)/suite_cccores $(B)/suite_ccbench $(B)/suite_ctrl $(B)/suite_recipes $(B)/suite_hooks $(B)/suite_regdoc $(B)/suite_accounting
$(CTL_SUITES): $(B)/suite_%: $(B)/tests/ctl/suite_%.o $(MACH_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
# the suites are run by test_ctl_suites (order-only: built with it, not linked into it)
$(B)/test_ctl_suites: | $(CTL_SUITES)
# gen_book (fw/recipes/gen_book.cpp, its region code in fw/recipes/book.h): v1.3's gen_book.py in C++ (D40); test_gen_book runs it with --check
$(B)/gen_book: $(B)/fw/recipes/gen_book.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
$(B)/test_gen_book: | $(B)/gen_book
