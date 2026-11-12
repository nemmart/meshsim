# Parked for WORKER20 (from the integrator, 2026-11-12): send at its next stopping point

One more request, from W22 (W22-20, RequestBusTest's back-pressure, credits and fairness groups): TileRef::req_send
takes only one push per cycle, because it uses put_late on the core's request link. ref's RequestPort.send appends
straight to the FIFO, any number between two cycles, and flags a push into a full FIFO (the violation already recorded
by W26). RequestBusTest relies on that: 128 pushes at once in back-pressure, 100 and 29 in credits, 4 per tile in
fairness; today the second push in a cycle is FATAL. Make req_send take any number of pushes between two cycles, as ref
does, with the full-FIFO case flagged. Do it with W22-19 (the delivery records), which the same groups need.
