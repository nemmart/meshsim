# tools/regs: gen_regs writes fw/include/mesh.h and fw/REGISTERS.md from the register table
# (regtable.h); tests/test_regs.cpp (make check) checks the committed files are its output, and the table against
# src/units/regmap.h.
#   build/<cc>-<mode>/gen_regs mesh.h fw/include/mesh.h; ... registers fw/REGISTERS.md; ... check fw/include/mesh.h fw/REGISTERS.md
$(B)/gen_regs: $(B)/tools/regs/gen_regs.o
	$(CXX) $(FLAGS) $^ -o $@
all: $(B)/gen_regs
