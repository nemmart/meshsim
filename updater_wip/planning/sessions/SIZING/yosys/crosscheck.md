| model | yosys logic, GE | count.py's estimate, GE | yosys flip-flops (GE, with their enables and resets) | what count.py's figure covers |
|---|---|---|---|---|
| upd_decoder | 10,719 | 10,198 | 2249 (20,515) | the decoder's operators (HIGH) but the run counters and the LRU clear, which yosys puts in its flip-flops |
| cache_tagrow | 2,820 | 2,321 | 0 (0) | tag compare, victim, touch, tag row write |
| cache_line | 5,029 | 4,737 | 0 (0) | entry_of and the patch |

upd_decoder's flip-flops with their enables and clears, count.py's way (HIGH): 20,671 GE
