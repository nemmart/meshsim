# WORKER7's division-family Tracer templates, re-run at jolt-cpp 821e1bd

`wip/tracer/harness/check_div.cpp` (WORKER7) rebuilt against PR #2's tracer. The only change needed: the opcode numbers
in `templates.py`'s `OP` map, renumbered to 821e1bd (`wip/tables/opcode_table.txt`; e.g. `VirtualAdvice` 122 -> 126,
`VirtualSignExtendWord` 110 -> 116). `check_div.txt` is **identical** to WORKER7's 4f36b3d result: the edge-case table
(Jolt's advice against RV64 M) and 8,040,000 random cases over the eight templates, 0 failures. So the division family's
expansion and advice code are unchanged at 821e1bd, as the expansion diff found.

These templates still use WORKER7's encoding (seq_remaining over every word, advice_reg = register - 128). The Tracer's
tables in `wip/tables/tracer/` re-encode them per DECISIONS-v1.25 s5 (seq_remaining over traced words, advice_reg
[61:56] = register - 146).
