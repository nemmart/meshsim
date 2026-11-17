# updater_wip/link: the host link's protocol on its own (WORKER8). README.md says what each program is.
#   make            the programs, optimized (build/)
#   make check      the header's tests, the link's tests, the kept cases, small searches of a link and of a chain, and
#                   forty random runs, under gcc with the sanitizers and under clang. The planted bugs (plants.sh) and
#                   the cases in cases/not-yet are of the header before 2026-11-17 and are not run: REPORT.md.
#   make clean
CXX      ?= g++
COMMON   := -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MMD -MP
OPT      := -O2 -g
DEBUG    := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
LIB      := link_end.cpp bench.cpp node.cpp chain.cpp
PROGS    := test_header test_link linkcase linksearch linkrun
VECTORS  ?=

all: $(PROGS:%=build/%)

build/%: %.cpp $(LIB) *.h | build
	$(CXX) $(COMMON) $(OPT) $< $(LIB) -o $@
build/san/%: %.cpp $(LIB) *.h | build
	g++ $(COMMON) $(DEBUG) -DLINK_PLANTS $< $(LIB) -o $@
build/clang/%: %.cpp $(LIB) *.h | build
	clang++ $(COMMON) $(OPT) -DLINK_PLANTS $< $(LIB) -o $@
build:
	mkdir -p build/san build/clang

check: $(PROGS:%=build/san/%) $(PROGS:%=build/clang/%) $(PROGS:%=build/%)
	build/san/test_header $(VECTORS)
	build/clang/test_header $(VECTORS)
	build/san/test_link
	build/clang/test_link
	build/san/linkcase cases/*.case
	build/clang/linkcase cases/*.case
	build/san/linksearch --packets 1 0 1 0 --damage 1 | tail -1
	build/clang/linksearch --packets 1 0 1 0 --damage 2 | tail -1
	! build/linksearch --packets 1 3 0 0 --queue 4 --damage 1 --hold 2 > /dev/null
	build/clang/linksearch --chain 2 --cpackets 0 0 1 --damage 0 --hold 3 | tail -1
	build/san/linkrun --seeds 1 8 --cycles 60000 | grep '^runs'
	build/clang/linkrun --seeds 9 40 --cycles 100000 | grep '^runs'
	@echo "make check: ok"

clean:
	rm -rf build
.PHONY: all check clean
