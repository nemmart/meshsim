# chain.mk: WORKER17's rules (v1.35: T2's segments back to back), included by sim/Makefile.
CHAIN_LINK := $(B)/tests/chain.o $(TRACER_LINK) $(B)/tests/sha256.o
$(B)/test_chain: $(CHAIN_LINK)
# the chain driver (tests/chain_main.cpp): T2's harness and the M3 fingerprint's pieces, as the t2 tool
$(B)/chain: $(B)/tests/chain_main.o $(B)/tests/chain.o $(B)/tests/sha256.o $(B)/tests/statefp_m3.o $(B)/tests/statefp.o \
            $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(T2_LINK) $(LIB_OBJ)
	$(CXX) $(FLAGS) $(sort $^) -o $@ $(WRAP)
