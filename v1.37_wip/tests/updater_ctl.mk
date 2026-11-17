# updater_ctl.mk: the rules for the Updater's controller (v1.37, updater_wip WORKER6: src/units/updater_ctl.h,
# updater_top.h, segment_shim.h, all header-only; the test pieces tests/test_lpddr.h, standin_cache.h; the bench
# tests/ctl_bench.h), included by the Makefile.
$(B)/test_updater_ctl: $(UPDATER_BENCH)
