# Parked for WORKER20 (from the integrator, 2026-11-12): send at its next stopping point

Two small items that now block other workers; please fit them in before the rest of your queue:
1. Chip::new_phase (agreed form): between cycles, call BigSram::new_phase() on every tile and wake it. W26's side
   (BigSram::new_phase(), the per-phase read and written bits, the DISJOINT fact, and the GO Wire trigger) is ready to
   start as soon as the hook is on main, and W24 (paused) then wires SortTest's new phase at every core wake.
2. From W26: a harness option to put ref's test peripheral on a tile, e.g. MachConfig::test_peripheral_tile, giving
   that tile's slot the test peripheral as ref's Config.testPeripheral does (MachConfig::test_periph_tile puts the
   replayer there). W26's DMA_HDR/DMA_ADDR-mid-transfer case needs it; the test's window stores already work.
