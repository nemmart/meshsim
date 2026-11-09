# sim/: plain make (DECISIONS D4).
#
#   make             the release build with clang (build/clang-release/; D18: release and timing builds use clang -O2)
#   make debug       the debug build with clang (ASan + UBSan)
#   make check       every test, debug and release, with gcc and clang (D18: both compilers in both builds)
#   make run         every test of one build: make run CC_NAME=gcc MODE=debug
#   make conformance the port-conformance matrix: ref's side (tests/ref/portconf.sh, needs a JDK) against the C++ ports
#   make clean
#
# The allocation hook (src/base/alloc_guard.cpp) needs our own malloc calls wrapped: WRAP, on every link.

CC_NAME ?= clang
MODE    ?= release
CXX_gcc   := g++
CXX_clang := clang++
COMPILERS := gcc clang
ifeq ($(shell command -v clang++ 2>/dev/null),)
$(error clang++ not found: D18 needs clang for the release build and make check (apt install clang lld))
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
else
$(error MODE must be debug or release)
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
# W3's stage 1 sketches: compiled in every build (they must build against the API), never linked or run.
SKETCH   := $(patsubst %.cpp,$(B)/%.o,$(wildcard src/sketch/*.cpp))

.PHONY: all debug run check conformance clean
.SECONDARY:
all: $(TESTS) $(SKETCH)

debug:
	@$(MAKE) --no-print-directory MODE=debug all

$(B)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@

$(B)/test_%: $(B)/tests/test_%.o $(HARNESS) $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

run: $(TESTS) $(SKETCH)
	@set -e; for t in $(TESTS); do ./$$t; done

check:
	@set -e; for c in $(COMPILERS); do for m in debug release; do \
	  echo "==== $$c $$m"; $(MAKE) --no-print-directory CC_NAME=$$c MODE=$$m run; done; done; \
	echo "==== make check: all passed ($(COMPILERS); debug, release)"

# The conformance tool (the C++ side of PORTS.md 10) and the full matrix: 10^5 cycles per configuration, three seeds,
# ref's observations (tests/ref/portconf.sh, a JDK) against the framework's (clang release), file by file.
$(B)/portconf: $(B)/tests/portconf_main.o $(LIB_OBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

conformance:
	@$(MAKE) --no-print-directory CC_NAME=clang MODE=release build/clang-release/portconf
	./tests/ref/portconf.sh 100000 build/portconf
	@set -e; n=0; for s in build/portconf/*.script; do \
	  case $$s in *rlink*) continue;; esac; \
	  ./build/clang-release/portconf $$s | cmp -s - $${s%.script}.obs || { echo "DIFFERS from ref: $$s"; exit 1; }; \
	  n=$$((n + 1)); done; \
	echo "conformance: $$n scripts of 10^5 cycles equal to ref, line for line (the reserved link is M2's)"

clean:
	rm -rf build

include src/units/units.mk
include src/units/net.mk
include src/units/g4.mk
include src/units/mem.mk
include src/units/tracer.mk
include src/threads.mk
include bench/w10/w10.mk

-include $(shell find build -name '*.d' 2>/dev/null)
include src/units/cc.mk
