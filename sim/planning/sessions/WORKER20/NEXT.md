# Parked for WORKER20 (from the integrator, 2026-11-11): send at its next stopping point

Two more harness requests, from W23 (P5; found in CcCoresTest's error path, and CondTest needs the first too):
9. setting a tile's ERRORS bits from the test, applied right after the register block's end of the cycle just run, as
   your existing harness writes are;
10. switching the ERRORS invariant check off mid-run.
W23 has marked stand-ins for both in tests/ctl/ctl.h until they land (the ERRORS one may act a cycle early).
