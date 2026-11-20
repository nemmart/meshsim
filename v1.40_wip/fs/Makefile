# The file system's emulation (v1.40): make test builds the workflows and runs them; their call logs go to tests/out/.
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -g

OBJS = fs_emu.o stand_ins.o

all: tests/workflows

fs_emu.o: fs_emu.cpp fs_emu.h fs.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
stand_ins.o: stand_ins.cpp stand_ins.h fs_emu.h fs.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
tests/workflows: tests/workflows.cpp $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# fs.h compiles as freestanding C, as a tile's program would include it
header-check: fs.h
	$(CC) -std=c99 -ffreestanding -Wall -Wextra -fsyntax-only -x c fs.h

test: tests/workflows header-check
	mkdir -p tests/out && cd tests && ./workflows

clean:
	rm -f $(OBJS) tests/workflows

.PHONY: all test clean header-check
