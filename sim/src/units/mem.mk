# mem.mk: WORKER11's rules (G3: the memory path's tests), included by sim/Makefile.
$(B)/test_mem: $(B)/src/units/inject.o $(B)/src/units/net.o
# P2d: the memory path against ref (tests/ref/MemConf.java, memconf.sh; fixtures in tests/memconf/)
$(B)/test_memconf: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(UNITS_LINK) $(GOLDEN_OBJ)
