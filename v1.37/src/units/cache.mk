# cache.mk: the rules (updater_wip, WORKER5: the LPDDR model, the AXI arbiter, the cache's controller and memory),
# included by the Makefile.
LPDDR_LINK := $(B)/src/units/lpddr_model.o $(B)/src/units/file_store.o
ARBITER_LINK := $(B)/src/units/axi_arbiter.o $(LPDDR_LINK)
$(B)/test_lpddr_model: $(LPDDR_LINK)
$(B)/test_axi_arbiter: $(ARBITER_LINK)
CACHE_LINK := $(B)/src/units/cache_ctl.o $(ARBITER_LINK)
$(B)/test_cache: $(CACHE_LINK) $(TRACER_LINK)
