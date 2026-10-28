# Spec items still open after the simulator session (meshsim on 2026-10-26n; checked against 26r)

The six problems logged earlier in this session are fixed in 26r (parts K and L), each checked against 26r's text:
4.13's condition 1 lists mode 10, mode 7 above 32 bits and mode 8 at 128 bits; 4.1 and 4.5 call read-port results
mode-6 packets; section 5 gives register and table-window loads 1 or 2 cycles (meshsim's reference 2) and lists the
full `UART_TX` FIFO among the stalls; nothing mentions the withdrawn wake-event mode's "prove"; 4.17.1's type 1 on
every mesh tile is confirmed; 4.2's reserved-encodings bullet says *d* = 64 with a mode other than 3. 26r changes
nothing else that meshsim models (the replayer's operand code 145 is cp11's). Still open (6.2, 4.3 and 6.4 are the same
in 26r as in 26n):

1. **6.4: the start path's figures** (measured, RecipeTest sync and a2a, 2-cycle register loads). From C&C's GO store,
   the shortest start path (the counter write, then the start store) puts the start store 8-9 cycles later and the
   first request 12-13 cycles after GO for a stream run; a group run's first request comes 20-21 cycles after GO on
   the all-to-all recipe's longer start path. v1.0, with 1-cycle loads: 5-6, 9-10 and 17-18. The extra cycles are
   GO's event becoming visible the cycle after GO is seen (4.12) and wait_for_command()'s two register loads (WAKE_EVENTS,
   then the command word) at 2 cycles each. 4.2 gives only the lead-in from the start store (2L + 2, 4L + 3), which is
   unchanged.

2. **4.3: the rule against overlapping read-port reads with incoming traffic, to revisit.** With 4.5's token pool (a
   loopback packet holds its token until it leaves the loopback FIFO) and 4.7's eject priority (the loopback served
   only when every other eject FIFO is empty), meshsim measures no loss in either case that motivated the rule:
   64 queued reads per tile during a full all-to-all lose 0 packets (v1.0: 561), and 32 results against a full-rate
   stream overflow nothing (v1.0: overflow) [meas: ReadPortGatherTest]; a full-rate stream plus 120 self-sends peaks the
   64-deep loopback FIFO at 64 with the send port waiting 167 cycles for tokens, no overflow [meas: NetTest loopback].
   The all-to-all's cycle count is unchanged by the loopback-last priority (no drift). meshsim still checks the rule
   (invariant 14) and the software follows it; whether the spec can relax it is the spec session's call.

3. **New: 6.2's announced staging needs an ordering rule.** Staging after boot reaches a running tile only "after a
   command that tells it what to expect", so the tile arms DMA_SRAM_REMAINING first. Nothing orders the tile's
   counter write before C&C's first staging packet: the tile writes the counter about 10 cycles after GO (its start
   path), while C&C may send as soon as GO has settled (go_latency + 1) and its first packet reaches tile 0 within a
   few cycles. meshsim's harness waits 64 cycles after GO (SortTest's STAGE_MARGIN); with no margin the first packets
   would be unexpected arrivals. Suggest 6.2 state a minimum wait from GO to the first staging packet (at least the
   longest start path plus the 0/+1 skew), as 6.4 argues for tile senders.

4. **For information: reqbus_latency** (3.3, an estimate of 15-20 cycles): meshsim measures 17 cycles unloaded, the
   same from every row, from the REQUEST_SEND store to the request in its target's queue, at its own hop latency of 3
   cycles per hop (meshsim's choice, not the spec's) [meas: RequestBusTest].

## Answers to part M's handoff (v.2), after the merge

- **The arming cycle** (to replace 6.2's derived "about 10"), cycles after C&C's GO store, every tile [meas]:
  - the shortest start path (`wait_for_command()`, then the counter store): the counter store at **+7 to +8** (RecipeTest
    sync), armed from the end of that cycle; the start store follows at +8 to +9;
  - the sort's `CMD_STAGE` path (`wait_for_command()`, the type test, then the counter store): **+14 to +15**
    (SortTest staging). With `stage_delay` = 32, C&C's first staged packet is sent at +32 and the first one is counted at
    +38; the sort passes (P1 54, every built-in set exact).
  - The spread is the 0/+1 skew; the bound in 6.2 should be tied to the slowest start path that announces staging.
- **Item 116, what removed v1.0's 561 lost packets** [meas: ReadPortGatherTest's overlap case, with the three loopback
  features switched one at a time, `-Dmeshsim.loopbackLast`, `-Dmeshsim.loopbackTokens`, `-Dmeshsim.loopbackDepth`]:
  the **64-deep loopback FIFO** (4.7: at least the token count). With v1.0's 16-deep FIFO, no tokens and no
  loopback-last priority, meshsim loses exactly v1.0's 561 packets; with the 16-deep FIFO and both other features on it
  still loses 580 (the tokens bound the loopback at 64, more than 16). At 64 deep nothing is lost in any of the four
  combinations of tokens and priority. So the depth removes the loss in this case; the tokens make it impossible in
  general (a token-holding packet can't find the FIFO full); the eject priority changes only the order (9,527 vs 9,081
  packets accepted with results outstanding, the all-to-all's time unchanged).
