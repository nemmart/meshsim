# g4.mk: WORKER12's rules (G4: the AG, the gather engine, the sort end to end), included by sim/Makefile.
#
#   tests/ref/engconf.sh --check   the engines' conformance fixtures regenerated from ref and compared (needs a JDK)

$(B)/test_sort: $(NET_LINK) $(B)/tests/statefp.o $(B)/tests/statefp_g1.o $(B)/tests/statefp_g2.o $(UNITS_LINK) $(GOLDEN_OBJ)
