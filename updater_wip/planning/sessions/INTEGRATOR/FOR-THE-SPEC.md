# For the specification: where v1.37's specification is wrong, unclear or silent

*The INTEGRATOR's list, for the owner. Nothing here is edited in `spec/` without the owner's word. Each row: the
section, what was found, and what the simulator does meanwhile. Those that held the work up were asked one at a time;
the rest wait for the owner.*

| # | section | found | the simulator meanwhile |
|---|---|---|---|
| 1 | 17.10.2, 17.10.11 | "A page at 2²³ or above is UB" cannot arise: a page number is a value's 32-bit index over 512, so the index's own limit (a run past 2³² − 1, UB) is the only one a program can reach. The page rule is redundant | both checked; the page's check can never fire |
| 2 | 20, invariant 37 | as WORKER5's answer has it: the stale-line check catches only a still-cached clean line read again. The joint's checks (`cache_system.h`) now catch, where the Updater meets the cache, the first rule (an update before its page's write is answered), a direct write to a page the cache holds a line of and a direct read of one it holds a dirty line of. 23.6.11 could name them | the joint's checks counted, failing a run |
| 3 | 17.9.5, 23.6.4 | the fill port carries no request "taken" beyond its handshake, so a client that presents a request a cycle too long is served twice and the second answer reaches nobody. Nothing in the Tracer is wrong; it is a property of the port that a simulator should check | the joint's fill-port check: a response the Tracer did not take fails the run |
| 4 | 17.7, 21.5 | the RW engine's figure ("a `START` raises the first burst address on AR or AW from *t* + 3") is the RTL's at its own port. Behind the arbiter (23.11) its bursts reach `lpddr` a cycle later, and the RTL test's slave (`chip_memory_test.rs`) is no longer what answers it: the memory run's cycles are now the LPDDR model's | as 17.7 and 23.10 say |
