# net.mk: the rules (the network's units and the chip's assembly in src/units/), included by the Makefile.

NET_LINK := $(B)/src/units/net.o $(B)/src/units/router.o $(B)/src/units/inject.o $(B)/tests/bench_master.o $(B)/src/units/chip.o

$(B)/test_router $(B)/test_net_headers: $(NET_LINK)
$(B)/test_cnc: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g2.o
# the router of v1.36's chip from the specification (tests/test_special_router.cpp)
$(B)/test_special_router: $(B)/src/units/net.o $(B)/src/units/router.o
