# net.mk: WORKER9's rules (the network's units and the chip's assembly in src/units/), included by sim/Makefile.
#
#   tests/ref/routerconf.sh --check   the router's conformance fixtures regenerated from ref and compared (needs a JDK)
#   tests/ref/cncconf.sh --check      C&C's likewise

NET_LINK := $(B)/src/units/net.o $(B)/src/units/router.o $(B)/src/units/inject.o $(B)/src/units/cnc.o

$(B)/test_router $(B)/test_net_headers: $(NET_LINK)
$(B)/test_cnc: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g2.o
