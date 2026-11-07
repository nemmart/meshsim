# sim/: plain make (DECISIONS D4).
#
#   make             the release build with gcc (build/gcc-release/)
#   make debug       the debug build with gcc (ASan + UBSan)
#   make check       every test, debug and release, with gcc and with clang where present
#   make run         every test of one build: make run CC_NAME=clang MODE=debug
#   make clean
#
# The allocation hook (src/base/alloc_guard.cpp) needs our own malloc calls wrapped: WRAP, on every link.

CC_NAME ?= gcc
MODE    ?= release
CXX_gcc   := g++
CXX_clang := clang++
COMPILERS := gcc $(if $(shell command -v clang++ 2>/dev/null),clang)

COMMON  := -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Isrc -MMD -MP
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
LIB_SRC  := $(wildcard src/base/*.cpp)
LIB_OBJ  := $(LIB_SRC:%.cpp=$(B)/%.o)
TEST_SRC := $(wildcard tests/test_*.cpp)
TESTS    := $(TEST_SRC:tests/%.cpp=$(B)/%)
HARNESS  := $(B)/tests/harness.o
# W3's stage 1 sketches: compiled in every build (they must build against the API), never linked or run.
SKETCH   := $(patsubst %.cpp,$(B)/%.o,$(wildcard src/sketch/*.cpp))

.PHONY: all debug run check clean
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

clean:
	rm -rf build

include src/units/units.mk

-include $(shell find build -name '*.d' 2>/dev/null)
