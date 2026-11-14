# pending.mk: what a stage of v1.36 broke and nobody has ported yet. NOT BUILT: nothing here is deleted or weakened: the
# sources and reference logs stay, and PENDING-v1.36.md has a row for each (what, why, what closes it). `make pending`
# lists them; `./run_tests.sh` prints its pending steps. An item leaves this file when it is ported: by the test's own
# commit. (The platform stage's branch has this file too, with its own entries: the two are to be joined at the merge.)

# ---- make check's tests (tests/test_<name>.cpp) ----
# The replayer stage (17.8): the slot's conformance replays ref's scripts on a slot with the replayer in it, and its
# reference observations are ref's, of v1.35's replayer (its busy, errors, events and queue occupancies cycle by cycle).
PENDING_TESTS := test_slotconf

# ---- the other default targets ----
PENDING_BIN :=

# $(call live,<targets>): the targets but the pending ones.
live = $(filter-out $(PENDING_BIN:%=$(B)/%),$(1))

.PHONY: pending
pending:
	@echo "pending tests (not built; PENDING-v1.36.md):"; for t in $(PENDING_TESTS); do echo "  PENDING $$t"; done
	@echo "pending tools:"; for t in $(PENDING_BIN); do echo "  PENDING $$t"; done
