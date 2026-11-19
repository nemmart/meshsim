# updater_ctl.mk: the rules for the Updater's controller (v1.37, updater_wip WORKER6: src/units/updater_ctl.h,
# updater_top.h, both header-only; the test pieces tests/test_lpddr.h, standin_cache.h; the bench
# tests/ctl_bench.h), included by the Makefile.
# (the joined bench, the INTEGRATOR: the memory system, units/cache_system.h, and the Tracer it names)
$(B)/test_updater_ctl: $(UPDATER_BENCH) $(TRACER_LINK)
