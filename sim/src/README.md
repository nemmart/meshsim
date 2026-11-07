# sim/src: the C++ framework

Empty at the start (2026-11-06). First: the primitives (fixed-size slots and rings, the delivery lists, the per-type run
lists, the free lists, the port kinds), each tested on its own; then an RV32 controller running real firmware, checked
against `../ref` cycle for cycle. See `../README.md` and `wip/planning/SIMULATOR-NEXT.md`.
