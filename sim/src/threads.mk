# threads.mk: WORKER4's rules (threads: TSan builds of the thread tests), included by sim/Makefile.
#
#   make tsan          the thread tests under ThreadSanitizer, gcc and clang (build/<cc>-tsan/); also run by make check
#   make tsan-one      one compiler: make tsan-one CC_NAME=gcc
#   bench_threads      the threads' benchmark (bench/threads/), built by `make` in every build
#
# TSan can't share a process with ASan, so it is its own build, debug-like (-O1 -g, SIM_DEBUG's checks on).

TSAN_TESTS := test_parallel test_threads test_topology test_matrix
TB         := build/$(CC_NAME)-tsan
TSAN_FLAGS := $(COMMON) -O1 -g -DSIM_DEBUG -fsanitize=thread -fno-omit-frame-pointer -pthread
TB_LIB     := $(LIB_SRC:%.cpp=$(TB)/%.o)
TB_TESTS   := $(addprefix $(TB)/,$(filter $(TSAN_TESTS),$(TEST_SRC:tests/%.cpp=%)))

FLAGS += -pthread

$(TB)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(TSAN_FLAGS) -c $< -o $@

# clang links TSan's own operator new/delete statically, beside W1's hook (alloc_guard.cpp): the first definition, the
# hook's, is kept; it allocates through malloc, which TSan intercepts, so TSan still sees every allocation.
$(TB)/test_%: $(TB)/tests/test_%.o $(TB)/tests/harness.o $(TB_LIB)
	$(CXX) $(TSAN_FLAGS) $^ -o $@ $(WRAP) -Wl,--allow-multiple-definition

# The threads' benchmark (timing: clang release, D18): make build/clang-release/bench_threads
$(B)/bench_threads: $(B)/bench/threads/bench_threads.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)
all: $(B)/bench_threads

.PHONY: tsan tsan-one
tsan-one: $(TB_TESTS)
	@set -e; for t in $(TB_TESTS); do TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1" ./$$t; done

tsan:
	@set -e; for c in $(COMPILERS); do echo "==== $$c tsan"; $(MAKE) --no-print-directory CC_NAME=$$c tsan-one; done

check: tsan
