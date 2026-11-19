# updater_wip/link: the host link's protocol on its own (WORKER8). README.md says what each program is.
#   make            the programs, optimized (build/)
#   make check      the header's tests, the link's tests, the kept cases, small searches of a link and of a chain, the
#                   planted bugs against the search, and forty random runs,
#                   under gcc with the sanitizers and under clang; then the interface (hostlink.h): its test, an
#                   ASIC reset in a running chain (a few runs: build/test_reset alone makes 1,380), the README's two
#                   examples built against the library alone, and the hardware tables
#   make lib        build/libhostlink.a, for whoever builds on hostlink.h
#   make clean
CXX      ?= g++
COMMON   := -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MMD -MP
OPT      := -O2 -g
DEBUG    := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
LIB      := link_end.cpp bench.cpp node.cpp chain.cpp hostlink.cpp
PROGS    := test_header test_link linkcase linksearch linkrun linkcost test_hostlink test_reset linkhw
API      := hostlink.cpp link_end.cpp node.cpp
EXAMPLES := back_to_back chain_reply
# HARDWARE.md is beside this file in a copy of the folder that stands alone, and with WORKER8's papers otherwise.
HARDWARE := $(if $(wildcard HARDWARE.md),HARDWARE.md,../planning/sessions/WORKER8/HARDWARE.md)
VECTORS  ?=

all: $(PROGS:%=build/%) lib

build/%: %.cpp $(LIB) *.h | build
	$(CXX) $(COMMON) $(OPT) $< $(LIB) -o $@
build/san/%: %.cpp $(LIB) *.h | build
	g++ $(COMMON) $(DEBUG) -DLINK_PLANTS $< $(LIB) -o $@
build/clang/%: %.cpp $(LIB) *.h | build
	clang++ $(COMMON) $(OPT) -DLINK_PLANTS $< $(LIB) -o $@
build:
	mkdir -p build/san build/clang build/api

# The library: the interface and what is behind it, and nothing of the benches.
lib: build/libhostlink.a
build/api/%.o: %.cpp *.h | build
	@mkdir -p build/api
	$(CXX) $(COMMON) $(OPT) -c $< -o $@
build/libhostlink.a: $(API:%.cpp=build/api/%.o)
	rm -f $@ && ar rcs $@ $^
# An example sees hostlink.h and the library, as a user would: C++17, the compiler's own defaults.
build/ex_%: examples/%.cpp hostlink.h build/libhostlink.a
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I. $< build/libhostlink.a -o $@

check: $(PROGS:%=build/san/%) $(PROGS:%=build/clang/%) $(PROGS:%=build/%)
	build/san/test_header $(VECTORS)
	build/clang/test_header $(VECTORS)
	build/san/test_link
	build/clang/test_link
	build/san/linkcase cases/*.case
	build/clang/linkcase cases/*.case
	build/san/linksearch --packets 1 0 1 0 --damage 1 | tail -1
	build/clang/linksearch --packets 1 0 1 0 --damage 2 | tail -1
	! build/linksearch --rules hl+stop_holds_again --packets 1 3 0 0 --queue 4 --damage 1 --hold 2 > /dev/null
	build/clang/linksearch --chain 2 --cpackets 0 0 1 --damage 0 --hold 3 | tail -1
	build/san/linkrun --seeds 1 8 --cycles 60000 | grep '^runs'
	build/clang/linkrun --seeds 9 40 --cycles 100000 | grep '^runs'
	./plants.sh 300000 | tail -1
	build/san/test_hostlink | tail -1
	build/clang/test_hostlink | tail -1
	build/san/test_reset 2 | tail -1
	build/clang/test_reset 10 | tail -1
	$(MAKE) --no-print-directory examples
	./readme_examples.sh
	build/linkhw > build/hardware.txt
	@if grep . build/hardware.txt | grep -Fxv -f $(HARDWARE) | grep -q .; then echo "HARDWARE.md does not hold linkhw's tables:"; grep . build/hardware.txt | grep -Fxv -f $(HARDWARE) | head -5; exit 1; fi
	@echo "$(HARDWARE) holds linkhw's tables as they are"
	@echo "make check: ok"

examples: $(EXAMPLES:%=build/ex_%)
	@for e in $(EXAMPLES); do build/ex_$$e > build/ex_$$e.txt || { echo "example $$e failed"; exit 1; }; done; echo "examples: ok"

clean:
	rm -rf build
.PHONY: all check clean lib examples
