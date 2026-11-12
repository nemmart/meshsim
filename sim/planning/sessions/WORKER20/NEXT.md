# Parked for WORKER20 (from the integrator, 2026-11-12): send at its next stopping point

One more request, from W22 (W22-19, RequestBusTest's basics, fairness and credits groups): the request bus's
per-request delivery records, as ref keeps them: for each request delivered to a target, the cycle it was delivered
and its source tile (Chip::reqbus() gives only a count today). Fit it after the two blocking items (new_phase, the test
peripheral on a tile) and with drainReports and results.
