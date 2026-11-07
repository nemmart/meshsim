# sim/bench: W6's benchmarks (plain make, sim/'s flags and allocation hook).
#
#   make                 release, gcc -O2: build/gcc-O2/{mesh,fp128}
#   make all-builds      also gcc -O3, gcc -O2 -march=native, clang -O2 (when clang is present)
#   make check           the debug build (ASan, UBSan): every variant's digests equal, the fold checked, the hook fires
#   make kit             build/kit.tgz: sim/src/base, sim/bench and sim/tools (machine.sh, D21), for a Quest task's kit
#   make clean

CC_NAME ?= gcc
OPT     ?= O2
CXX_gcc   := g++
CXX_clang := clang++
COMMON  := -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I../src -I. -MMD -MP -pthread
WRAP    := -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=aligned_alloc,--wrap=posix_memalign
OPT_O2     := -O2 -DNDEBUG
OPT_O3     := -O3 -DNDEBUG
OPT_native := -O2 -march=native -DNDEBUG
OPT_debug  := -O1 -g -DSIM_DEBUG -fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=all \
              -fno-omit-frame-pointer
FLAGS := $(COMMON) $(OPT_$(OPT)) -g
CXX   := $(CXX_$(CC_NAME))
B     := build/$(CC_NAME)-$(OPT)
BASE  := $(wildcard ../src/base/*.cpp)
BOBJ  := $(BASE:../src/base/%.cpp=$(B)/base/%.o)
HAVE_CLANG := $(shell command -v clang++ 2>/dev/null)

.PHONY: all all-builds check kit clean
all: $(B)/mesh $(B)/fp128

all-builds:
	@$(MAKE) --no-print-directory OPT=O2
	@$(MAKE) --no-print-directory OPT=O3
	@$(MAKE) --no-print-directory OPT=native
	$(if $(HAVE_CLANG),@$(MAKE) --no-print-directory CC_NAME=clang OPT=O2)

$(B)/base/%.o: ../src/base/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
$(B)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(FLAGS) -c $< -o $@
$(B)/%: $(B)/%.o $(BOBJ)
	$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

check:
	@$(MAKE) --no-print-directory OPT=debug
	./check.sh build/$(CC_NAME)-debug

kit:
	@mkdir -p build
	cd ../.. && tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -czf sim/bench/build/kit.tgz \
	  --exclude=sim/bench/build sim/src/base sim/bench sim/tools

clean:
	rm -rf build

.SECONDARY:
-include $(shell find build -name '*.d' 2>/dev/null)
