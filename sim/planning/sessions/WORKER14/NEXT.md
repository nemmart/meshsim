# Parked for WORKER14 (from the integrator, 2026-11-09): send at its next stopping point

W16's merge is on main (a6247c8; both release builds clean here, 54 binaries). Your turn: merge main into sim-w14 and
resolve chip.cpp/h and tile_ctl.cpp/h once, against W15's tile paths and W16's harness. Keep W15's periph_slot paths
behind G1Config::periph_slot, and your request side in ControlIO and the tile builder. Then the full make check build by
build (both debug builds and TSan), push, and tell me.

W16 confirmed T2's mode (mode 3 split with lag_cycles 0, mode 0 on one domain as a cross-check, D34) and your ports. The
end-to-end core test (a tile's REQUEST_SEND into a controller's REQUEST_RECEIVE) uses W16's units, now on main: add it
with this merge. W16 then wires C&C 1's slot on top of your merge, and W15 merges last.

Also, from W15: add an optional writable(w) method to the Tracer, returning whether own word w accepts stores, so the
core raises a bus error at once on a store to a read-only word, as ref does. The replayer and the test peripheral have
it; copy their pattern.
