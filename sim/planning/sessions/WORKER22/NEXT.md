# Parked for WORKER22 (from the integrator, 2026-11-12): send at its next stopping point

W22-19 is on main (W20, 1afa1973): ReqBus::delivered_log(), ref's delivered list, each request's delivery cycle,
source tile and words, in delivery order. W22-20 (req_send's many pushes between cycles) is W20's next item; then
RequestBusTest's basics, fairness, credits and back-pressure can all port. W22-14's body (W26's disjoint tracking) and
W22-16 are still to come; I'll tell you when each lands.
