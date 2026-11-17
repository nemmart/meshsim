# The wire: make (gcc), make CXX=clang++, make all-variants.
#
# Variants, each in build/<compiler>/<variant>/:
#   release   -O2, for the cost (no checks in the library)
#   check     -O2 -DWIRE_CHECK: poison and the test pauses; what the checks run
#   plant1..5 the check build with a planted bug (tests/plants.sh)
#   asan      address and undefined-behaviour sanitizers, with WIRE_CHECK
#   tsan      the thread sanitizer: wire-threads only
CXX ?= g++
COMPILER := $(notdir $(firstword $(CXX)))
TOP := build/$(COMPILER)
VARIANT ?= check

CXXFLAGS_BASE := -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -pthread -g
ifeq ($(VARIANT),release)
  VFLAGS := -O2
else ifeq ($(VARIANT),check)
  VFLAGS := -O2 -DWIRE_CHECK
else ifeq ($(VARIANT),asan)
  VFLAGS := -O1 -DWIRE_CHECK -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
else ifeq ($(VARIANT),tsan)
  VFLAGS := -O1 -DWIRE_CHECK -fsanitize=thread
else ifneq ($(filter plant%,$(VARIANT)),)
  VFLAGS := -O2 -DWIRE_CHECK -DWIRE_PLANT=$(patsubst plant%,%,$(VARIANT))
else
  $(error unknown VARIANT $(VARIANT))
endif

OUT := $(TOP)/$(VARIANT)
LIB_SRC := wire.cpp wire_args.cpp
NODE_SRC := standin.cpp node_loop.cpp
HDRS := $(wildcard *.h)

ifeq ($(VARIANT),tsan)
  PROGS := $(OUT)/wire-threads
else
  PROGS := $(OUT)/wire-launch $(OUT)/wire-node $(OUT)/wire-ref $(OUT)/wire-threads
endif

.PHONY: build all-variants clean
build: $(PROGS)

$(OUT)/%.o: %.cpp $(HDRS) Makefile
	@mkdir -p $(OUT)
	$(CXX) $(CXXFLAGS_BASE) $(VFLAGS) -c $< -o $@

LIB_OBJ := $(LIB_SRC:%.cpp=$(OUT)/%.o)
NODE_OBJ := $(NODE_SRC:%.cpp=$(OUT)/%.o)

$(OUT)/wire-launch: $(OUT)/launch.o $(LIB_OBJ)
	$(CXX) $(CXXFLAGS_BASE) $(VFLAGS) $^ -o $@
$(OUT)/wire-node: $(OUT)/node_main.o $(NODE_OBJ) $(LIB_OBJ)
	$(CXX) $(CXXFLAGS_BASE) $(VFLAGS) $^ -o $@
$(OUT)/wire-ref: $(OUT)/ref_main.o $(OUT)/standin.o
	$(CXX) $(CXXFLAGS_BASE) $(VFLAGS) $^ -o $@
$(OUT)/wire-threads: $(OUT)/threads_main.o $(NODE_OBJ) $(LIB_OBJ)
	$(CXX) $(CXXFLAGS_BASE) $(VFLAGS) $^ -o $@

all-variants:
	@for v in release check asan tsan plant1 plant2 plant3 plant4 plant5; do \
	  $(MAKE) --no-print-directory VARIANT=$$v build || exit 1; done

clean:
	rm -rf build
