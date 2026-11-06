# Parked for WORKER28 (send at its next report; remove once sent)

Coordination: WORKER30 is folding the 11-05/11-06 decisions into the spec on branch v1.3-spec-final. It leaves the spec's
latency lines to you (section 10's go_latency, ready_latency and error_latency, and the "1 in simulation" text): your latency
commit changes them. Keep your edits to those lines, so the two branches merge cleanly.

Also from WORKER29 (P2): its change B, eager bus commit (a put registers its bus on a per-chip dirty list, committed at the
cycle's end), changes `BusBase` on its branch sim-step-cost (off sim-idle-skip) and keeps your `wakeChip` hook. Tell me if
anything in stage 1 depends on the lazy commit's timing, so the two stay compatible.
