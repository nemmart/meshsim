# v1.36: plain make.
#
#   make             the release build with clang (build/clang-release/; D18: release and timing builds use clang -O2)
#   make debug       the debug build with clang (ASan + UBSan)
#   make check       the copies from v1.3 (tools/check_copies.sh), then every test, debug and release, with gcc and
#                    clang (D18: both compilers in both builds)
#   make run         every test of one build: make run CC_NAME=gcc MODE=debug
#   make clean
#
# The allocation hook (src/base/alloc_guard.cpp) needs our own malloc calls wrapped: WRAP, on every link.

CC_NAME ?= clang
MODE    ?= release
CXX_gcc   := g++
CXX_clang := clang++
COMPILERS := gcc clang
ifeq ($(shell command -v clang++ 2>/dev/null),)
$(error clang++ not found: D18 needs clang for the release build and make check (apt install clang))
endif

COMMON  := -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Isrc -MMD -MP -pthread
DEBUG   := -O1 -g -DSIM_DEBUG -fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=all \
           -fno-omit-frame-pointer
RELEASE := -O2 -DNDEBUG
WRAP    := -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=aligned_alloc,--wrap=posix_memalign

ifeq ($(MODE),debug)
FLAGS := $(COMMON) $(DEBUG)
else ifeq ($(MODE),release)
FLAGS := $(COMMON) $(RELEASE)
else ifeq ($(MODE),suite)
FLAGS := $(COMMON) $(RELEASE) -DSIM_CHECKS   # the suite build, release with the checker's facts (src/check.h)
else
$(error MODE must be debug, release or suite)
endif
CXX := $(CXX_$(CC_NAME))
ifeq ($(CXX),)
$(error CC_NAME must be gcc or clang)
endif

B        := build/$(CC_NAME)-$(MODE)
LIB_SRC  := $(wildcard src/base/*.cpp src/*.cpp)
LIB_OBJ  := $(LIB_SRC:%.cpp=$(B)/%.o)
TEST_SRC := $(wildcard tests/test_*.cpp)
TESTS    := $(TEST_SRC:tests/%.cpp=$(B)/%)
HARNESS  := $(B)/tests/harness.o

.PHONY: all debug run check clean
.SECONDARY:
all: $(TESTS)

debug:
	@$(MAKE) --no-print-directory MODE=debug all

$(B)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/test_%: $(B)/tests/test_%.o $(HARNESS) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

# suite.h (Java's T for the ported suites): its own test links it.
$(B)/test_suite: $(B)/tests/test_suite.o $(B)/tests/suite.o $(HARNESS) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done

check:
	@sh tools/check_copies.sh
	@set -e; for c in $(COMPILERS); do for m in debug release; do \
	  echo "==== $$c $$m"; $(MAKE) --no-print-directory CC_NAME=$$c MODE=$$m run; done; \
	  $(MAKE) --no-print-directory CC_NAME=$$c MODE=debug build/$$c-debug/suite_bus && ./build/$$c-debug/suite_bus; done; \
	echo "==== make check: all passed ($(COMPILERS); debug, release)"

# The port kinds' conformance tool (the C++ side of src/PORTS.md 10).
$(B)/portconf: $(B)/tests/portconf_main.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

clean:
	rm -rf build

include src/units/tracer.mk
include src/units/units.mk
include src/units/net.mk
include src/units/mem.mk
include src/threads.mk

-include $(shell find build -name '*.d' 2>/dev/null)
include tests/chain.mk
include tests/tracersuites.mk
include tests/mach.mk
include tools/regs/regs.mk
include tools/delivery/delivery.mk
include tools/fwimage/fwimage.mk
include tests/p7b.mk
include tests/bus.mk
include tools/doccheck.mk

# Acceptance A (v1.36's platform): the RTL's tile programs on one compute tile (tests/test_rtl_tile.cpp).
RTL_TILE_LINK := $(NET_LINK) $(B)/src/units/trace_log.o $(B)/src/units/tile_ctl.o $(B)/src/units/periph_slot.o \
                 $(B)/src/units/replayer.o $(TRACER_LINK)
$(B)/test_rtl_tile: $(RTL_TILE_LINK)
