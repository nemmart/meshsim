# updater_wip/link: the host link's protocol on its own (WORKER8). README.md says what each program is.
#   make            the programs, optimized (build/)
#   make check      the header's tests, the link's tests, the kept cases, two small searches and the planted bugs,
#                   under gcc with the sanitizers and under clang
#   make clean
CXX      ?= g++
COMMON   := -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -MMD -MP
OPT      := -O2 -g
DEBUG    := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
LIB      := link_end.cpp bench.cpp
PROGS    := test_header test_link linkcase linksearch
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
	build/san/linksearch --rules proposed --packets 1 0 1 0 --damage 1 | tail -1
	build/clang/linksearch --rules proposed --packets 1 0 1 0 --damage 2 | tail -1
	! build/linksearch --rules as_written --damage 1 > /dev/null
	./plants.sh 300000 | tail -1
	@echo "make check: ok"

clean:
	rm -rf build
.PHONY: all check clean
