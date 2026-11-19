#!/usr/bin/env python3
"""count.py: the size of v1.37's two controllers, the AXI arbiter and the tile's core (SIZING/PROMPT.md).

Everything here is counted from the code in v1.37/src/units/ (cache_ctl.*, updater_ctl.h, updater_top.h,
axi_arbiter.*), with machine-spec.md 23.6, 17.10 and 23.11 as the guide to what each register is for. Run it:

    python3 count.py            # prints the tables, and writes COUNTS.md beside it

Three parts are kept apart for each unit, as the brief asks:
  - the flip-flops: every register a chip needs, at its width (REGS below);
  - the logic between them: the datapath's operators (OPS) and the next-state and output logic (CONTROL);
  - the memories, in bits (MEMS).

THE COSTS (COST) are NAND2 equivalents (GE). The ratios are the Nangate 45 nm Open Cell Library's X1 cells over
NAND2_X1 (0.798 um2): DFF_X1 4.522 um2 -> 5.67; FA_X1 -> 5.33; HA_X1 -> 3.33; MUX2_X1 -> 2.33; XOR2_X1 -> 2.0;
AND2/OR2 -> 1.33. Another library's figures go in COST and everything is redone.

LOW AND HIGH. Each unit is costed twice, and LOW_HIGH says what separates the two; every assumption is a named switch
in ASSUME, so either can be redone with others.
"""
import json
import os
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))

# ============================================================================================ the gate costs, GE
COST = OrderedDict(
    ff=5.67,     # a D flip-flop, no scan, no reset (DFF_X1). A clock enable is counted apart, as a mux bit
    fa=5.33,     # a full adder bit (FA_X1): an adder, a subtractor, a magnitude comparator done as a subtract
    ha=3.33,     # a half adder bit (HA_X1): an incrementer or decrementer
    mux=2.33,    # a 2:1 multiplexer bit (MUX2_X1). An n:1 multiplexer bit is n - 1 of them
    eq=3.0,      # an equality comparator bit: an XNOR (2.0) and its share of the AND tree (about 1)
    lt=4.0,      # a magnitude comparator bit (a carry chain without sums)
    zd=1.0,      # a bit compared with a constant, an all-ones or all-zeros detect, an OR reduction: its share of a tree
    and2=1.33,   # an AND2 or OR2 bit: a gated or masked bit
    xor=2.0,     # an XOR2 bit
    icg=7.0,     # a clock-gating cell, one for each clock-gated register group (low only)
)

# What separates LOW from HIGH. True in HIGH, as coded; False in LOW, the lean build. Each is used below by name.
ASSUME = OrderedDict(
    hold_mux=("every register with an enable holds through a mux bit (an enable flop)",
              "registers of 8 bits or more are clock-gated: holding costs no mux, one ICG a register"),
    all_ports=("the cache's four look-up ports, as specified (23.6.2)",
               "v1.37's one client: look-up port 1 only (ports 0, 2 and 3 have no client in v1.37)"),
    line_regs=("the cache's five 1,024-bit line registers, as coded",
               "three: the walker's line register and the miss handler's buffer dropped (the patch made on the way "
               "to the AXI writer, R beats written lane by lane into the data memory, which has 8 lane enables)"),
    redundant=("the registers the code keeps that copy another (a set beside its line, a victim's tag beside its "
               "row, the Updater's look_status and look_entry, a row counter kept twice), as coded",
               "dropped"),
    lru_clear=("the Updater's LRU table cleared at START bit by bit (an AND a bit)",
               "a valid bit an entry (32 flip-flops), the read gated"),
    pm_rdreg=("the page memory's read register as 128 flip-flops (17.10.7 counts it)",
              "in the SRAM macro, as a macro's output latch normally is"),
    aligned_add=("an address plus an offset as an adder",
                 "a page's address is a multiple of 32 units (17.10.9), so a page plus a line or a half is a "
                 "concatenation: no adder"),
    shared_cut=("the arbiter's burst cutter in every AR and AW slot (8)", "one cutter a direction, muxed (2)"),
    shared_r=("the arbiter's R data register in each client's outputs (4 x 128)",
              "one R data register shared by the four clients' outputs, each with its own valid"),
    ctl_row=("next-state and output logic at 8 GE a control-word row, plus an OR term a bit asserted",
             "at 4 GE a row, plus the same OR terms"),
    buffering=("15% added to the logic for buffering the wide selects and timing repair", "none"),
)
CTL_ROW_GE = {True: 8.0, False: 4.0}
CTL_OR_GE = 1.33
BUFFERING = {True: 1.15, False: 1.0}


def lean(flag, high):
    """True in HIGH; False in LOW for the switches that LOW turns off."""
    return high


# ============================================================================================ the registers
# A register: (name, bits, count, sources, kind, note)
#   sources: the next values besides holding it (1: loaded from one place; 2: from two places, a 2:1 mux; ...)
#   kind: 'hw' a register a chip needs as coded; 'line' a cache line register LOW may drop; 'port' a look-up port's
#         (LOW keeps one); 'red' a redundant copy (LOW drops); 'pmreg' the page memory's read register; 'lruv' LOW's
#         LRU valid bits (LOW only); 'out' an output register, loaded every cycle (no hold); 'flag' a 1-bit flag whose
#         set and clear are in CONTROL
#   note: what it holds, and where it differs from the specification's count
def R(name, bits, count=1, sources=1, kind='hw', note=''):
    return dict(name=name, bits=bits, count=count, sources=sources, kind=kind, note=note)


# Not counted, and why: the simulator's own (statistics, checks) or a wire of the code's.
NOT_HW = {
    'cache': [
        ('statistics: hits, misses, evictions, write_backs, ..., latencies, walk_lines', 'the simulator\'s own'),
        ('Held.missed_line (32 a source) and Held.t_accept (64 a source)', 'statistics only: the hit/miss classes and latencies'),
        ('diverged_[16], n_diverged_, check_store', 'the stale-line check (23.6.11), not hardware'),
        ('walker row_ (216)', 'a wire: the tag memory\'s output register, which only the walker reads, holds the row '
                              'until the walker\'s next read. 23.6.5 counts it'),
        ('walker wdirty_ (1)', 'used only by the stale-line check. 23.6.5 counts it'),
        ('walker set_ (9)', 'a wire: x[8:0]. 23.6.5 does not count it either'),
        ('tag_read_t_, mh_tag_write_t_ (cycle numbers)', 'hardware keeps one bit for them, counted in the guard'),
        ('bus_taken_, mh_data_read_, mh_writes_', 'wires within a cycle'),
    ],
    'updater': [
        ('lookups, updates, read_bursts, ..., values (Ctl\'s counts)', 'the simulator\'s figures'),
        ('shadow_uncut, cut_half_reads_run', 'a shadow for the comparison with the black box'),
        ('UpdaterTop.pages_seen (2^23 bits), push_src', 'invariant 36\'s checks, beside the controller, not hardware'),
        ('PageMemory.reads, writes', 'counts'),
        ('the root and free page checks at CHECK and TAKE, the header word\'s wrap check', 'UB the simulator stops on'),
    ],
    'arbiter': [
        ('read_pieces, write_pieces, read_bursts, write_bursts, read_waits, write_waits', 'statistics'),
        ('List entries 4 to 15 (MAX_LIST 16)', 'CLIENT_BURSTS is 4 by default: 4 entries are reachable'),
        ('List.resp[] of the read lists', 'unused: R beats carry their own rresp'),
    ],
}

REGS = OrderedDict()

# ---- the cache's controller (cache_ctl.h), LPDDR_ADDRESS_BITS 32
REGS['cache'] = [
    # front end: a register a source (23.6.5's first three rows)
    R('look-up port request: valid, parked, wait for the miss handler, wait for the slot', 4, 4, kind='flag'),
    R('look-up port request: op 2, root 31, depth 2, page 30, level 2, entry 32', 99, 4, 1, 'port'),
    R('look-up port request: the failed line 31 and its valid bit', 32, 4, 1, 'port'),
    R('fill read register: valid, wait for the miss handler, behind_write', 3, 1, kind='flag',
      note='behind_write (23.6.4\'s ordering rule) is in the code and not in 23.6.5\'s 102: +1'),
    R('fill read register: root 31, depth 2, line 35', 68),
    R('fill read register: the failed line and its bit', 32),
    R('fill write register: valid, wait for the miss handler, wait for the slot', 3, 1, kind='flag'),
    R('fill write register: root 31, depth 2, line 35', 68),
    R('fill write register: the line, 1,024 bits', 1024, 1, 1, 'hw'),
    R('fill write register: the failed line and its bit', 32),
    R('front end: round-robin pointer 3, the source a miss was handled for 3 and its valid bit', 7),
    # the walker
    R('walker: state', 2, kind='flag'),
    R('walker: source 3', 3),
    R('walker: level k 2', 2, 1, 2),
    R('walker: the discard\'s line count 6', 6, 1, 2),
    R('walker: the page at level k 31', 31, 1, 2),
    R('walker: the line being looked up 31', 31, 1, 1),
    R('walker: the hit way 3', 3),
    R('walker: every entry owned so far, a read pending', 2, kind='flag'),
    R('walker: the line register', 1024, 1, 2, 'line', 'the line as read, patched for an update: LOW drops it'),
    R('walker: a reply waiting for the bus: status 3, level 2, entry 32', 37),
    # the miss handler
    R('miss handler: state', 3, kind='flag'),
    R('miss handler: allocates, error, arvalid, a copy made', 4, kind='flag'),
    R('miss handler: the line to install 31', 31),
    R('miss handler: its set 9', 9, kind='red', note='x[8:0]: a copy'),
    R('miss handler: its way 3, the source it serves 3, the beats in 4', 10, 1, 2),
    R('miss handler: the victim\'s tag 22, valid, dirty', 24, kind='red', note='the row as read holds them'),
    R('miss handler: the set\'s tag row', 216),
    R('miss handler: the line buffer', 1024, 1, 3, 'line', 'R beats, the victim from the data memory, a fill '
      'write\'s line: LOW drops it'),
    # the AXI writer
    R('AXI writer: busy, the address taken', 2, kind='flag'),
    R('AXI writer: the beat 4', 4, 1, 2),
    R('AXI writer: the line address', 25, 1, 2, note='25 bits at LPDDR_ADDRESS_BITS 32: AW\'s address is 25 bits; '
      '23.6.5 counts 31'),
    R('AXI writer: the line', 1024, 1, 2),
    R('AXI writer: the queue of kinds: 4 kinds, head 2, count 3', 9, note='2 would do: one update and one '
      'write-back at most are outstanding'),
    # the update slot
    R('update slot: valid', 1, kind='flag'),
    R('update slot: source 2, way 3, level 2, the entry before 32, the line 31', 70, note='the source is a look-up '
      'port: 2 bits (23.6.5 counts 3)'),
    R('update slot: its set 9', 9, kind='red', note='the line\'s [8:0]: a copy'),
    R('the tag-write guard: set 9, in or after the read 1', 10),
    R('the error', 1, kind='flag'),
    # the outputs (every output a flip-flop)
    R('outputs: the six readies', 6, kind='flag'),
    R('outputs: the reply bus: valid 1, client 2, status 3, level 2, entry 32', 40, 1, 3, 'out'),
    R('outputs: fill read response valid 1, status 2', 3, 1, 1, 'out', note='status 0 to 3: 2 bits (23.6.5: 3)'),
    R('outputs: fill read response data', 1024, 1, 1, 'out'),
    R('outputs: fill write response valid, status', 2, 1, 1, 'out'),
    R('outputs: AR valid 1, line address 25; AW the same', 52, 1, 1, 'out'),
    R('outputs: W valid 1, wdata 128, wlast 1', 130, 1, 1, 'out'),
    R('outputs: the error', 1, 1, 1, 'out'),
]

# ---- the Updater's controller (updater_ctl.h Regs; updater_top.h's window is wiring and checks)
REGS['updater'] = [
    R('words 32-37: RUN_CONFIG 7, the three roots 96', 103, 1, 1, note='written by the bus'),
    R('WORD_COUNT, PAGE_COUNT', 64, 1, 2, note='the bus, or counted down'),
    R('the statistics counters, 6 x 32', 192, 1, 1),
    R('the control block\'s: busy, ERRORS 5, the done and credit pulses', 8, kind='flag'),
    R('the input queue: 4 words of 128', 512, 1, 1),
    R('the input queue: head 2, count 3, ready', 6, kind='flag'),
    R('decoder: the bit buffer', 194, 1, 1, 'out', note='loaded every cycle from the shift-and-insert network, '
      'which holds it by shifting 0'),
    R('decoder: its bit count 8', 8, 1, 1, 'out'),
    R('decoder: the LRU table, entries 1 to 31', 31 * 64, 1, 1, note='each loads the entry in front of it'),
    R('decoder: the LRU table, entry 0', 64, 1, 2, note='a hit\'s entry or a miss\'s word'),
    R('decoder: LOW\'s LRU valid bits', 32, 1, 1, 'lruv'),
    R('decoder: the run of values: next index 32, values left 32', 64, 1, 2),
    R('decoder: the output register: valid 1, index 32, value 64, type 2', 99),
    R('free page queue: head 8, count 9, gone since the credit 7', 24),
    R('state machine: state 5, level 2, file 1', 8, kind='flag'),
    R('state machine: for each file the look-up\'s level 2 and whether it takes pages', 6),
    R('state machine: for each file the look-up\'s status 3 and entry 32', 70, kind='red',
      note='never read: look_status is not used, and look_entry[TRACER] is not; look_entry[IMAGE] equals src[IMAGE] '
           'wherever it is read'),
    R('state machine: for each file its source at this level 32', 64, 1, 2),
    R('state machine: the page memory\'s contents: valid 1, source 32, level 2', 35),
    R('state machine: the next level\'s entry caught 32, the new page 31, the fill row 8', 71, 1, 1),
    R('state machine: the held page: valid, owned, a hole; halves had and dirty', 7, kind='flag'),
    R('state machine: the held page: number 23, where written 31, where its halves come from 31', 85, 1, 3),
    R('AXI port: AR valid', 1, kind='flag'),
    R('AXI port: AR address 32, len 8', 40, 1, 1, 'out', note='its low 11 bits and 7 of len\'s are constant: a '
      'half page at a multiple of 2,048, len 127 or 255'),
    R('AXI port: the read: active, clearing owned bits, error; row 8; the entry to catch 10 and its valid bit', 22),
    R('AXI port: AW valid', 1, kind='flag'),
    R('AXI port: AW address 32, len 8', 40, 1, 1, 'out'),
    R('AXI port: the write: active, error, each half\'s source 4; rows left 9, next row 8', 23),
    R('AXI port: the W path: a row pending, its row 8', 1, kind='flag'),
    R('AXI port: the W path: the pending row 8', 8, kind='red', note='always equal to the next row'),
    R('AXI port: W valid, wlast, the beat 128', 130, 1, 2, 'out', note='holds while wready is 0'),
    R('look-up port: valid', 1, kind='flag'),
    R('look-up port: op 2, root 31, a 23-bit page, level 2, entry 32', 90, 1, 1, 'out'),
    R('look-up port: depth 2', 2, kind='red', note='RUN_CONFIG\'s depth'),
    R('look-up port: a reply awaited', 1, kind='flag'),
    R('look-up port: the last reply: status 3, level 2, entry 32', 37),
    R('the page memory\'s read register', 128, 1, 1, 'pmreg'),
]

# ---- the AXI arbiter (axi_arbiter.h), 4 clients, CLIENT_BURSTS 4, TURN_BEATS 32
REGS['arbiter'] = [
    R('AR slot, a client: valid', 1, 4, kind='flag'),
    R('AR slot, a client: the next piece\'s beat address 28, beats left 9', 37, 4, 1),
    R('AW slot, a client: valid', 1, 4, kind='flag'),
    R('AW slot, a client: the next piece\'s beat address 28, beats left 9', 37, 4, 1),
    R('read list, a client: 4 x pieces 4, head 2, count 3', 21, 4, 1),
    R('write list, a client: 4 x (pieces 4, response 2), head 2, count 3', 29, 4, 1),
    R('a client\'s outputs: arready, awready, wready, rvalid, rresp 2, rlast, bvalid, bresp 2', 10, 4, 1, 'out'),
    R('a client\'s outputs: rdata', 128, 4, 1, 'shr', note='LOW: one register for the four (see ASSUME shared_r)'),
    R('the round-robin pointers, AR and AW', 4),
    R('the write piece in turn: valid, client 2, beats to take 6, last piece', 10),
    R('the W queue: 2 x (beat 128, wlast), head 1, count 2', 261, 1, 1),
    R('outputs to lpddr: AR valid, address 28, len 8, ID 2; AW the same', 78, 1, 1, 'out'),
    R('outputs to lpddr: W valid, wdata 128, wlast', 130, 1, 1, 'out'),
]

# The specification's counts, for the comparison (23.6.5, 17.10.7, 23.11)
SPEC_FF = {'cache': 6876, 'updater': 4193, 'arbiter': 1539}


def reg_included(r, unit, high):
    k = r['kind']
    if k == 'lruv':
        return not high
    if high:
        return True
    if k in ('line', 'red', 'pmreg'):
        return False
    return True


def reg_count(r, unit, high):
    """How many of this register LOW or HIGH builds."""
    if r['kind'] == 'port' and not high:
        return 1
    if r['kind'] == 'flag' and unit == 'cache' and 'look-up port request' in r['name'] and not high:
        return 1
    if r['kind'] == 'shr' and not high:
        return 1
    return r['count']


def flops(unit, high):
    return sum(r['bits'] * reg_count(r, unit, high) for r in REGS[unit] if reg_included(r, unit, high))


def flops_as_coded(unit):
    """The code's registers a chip needs, at their signals' widths: HIGH's, less LOW's LRU valid bits."""
    return sum(r['bits'] * r['count'] for r in REGS[unit] if r['kind'] != 'lruv')


def reg_mux_bits(unit, high):
    """The registers' input multiplexers: (sources - 1) mux bits a bit, and in HIGH one more to hold."""
    n = 0
    icg = 0
    for r in REGS[unit]:
        if not reg_included(r, unit, high):
            continue
        c = reg_count(r, unit, high)
        src = r['sources']
        if unit == 'cache' and not high and r['name'].startswith('AXI writer: the line'):
            src = 1                      # with no MH buffer both writes come from the data memory's output
        b = r['bits'] * c
        if r['kind'] == 'flag':
            continue                     # their set and clear are in CONTROL
        n += b * (src - 1)
        hold = r['kind'] not in ('out',)
        if hold:
            if high or r['bits'] < 8:
                n += b
            else:
                icg += c
    return n, icg


# ============================================================================================ the datapath operators
# An operator: (name, kind, bits, count, when, note). kind is a COST key, or 'mux:N' for an N:1 multiplexer of `bits`
# bits, or 'ge' for random logic given in GE. when: 'both', 'high' or 'low'.
def O(name, kind, bits, count=1, when='both', note=''):
    return dict(name=name, kind=kind, bits=bits, count=count, when=when, note=note)


OPS = OrderedDict()
OPS['cache'] = [
    O('tag compare: 8 ways x 22 bits', 'eq', 22, 8),
    O('tag compare: valid AND, the hit\'s encoder 8 -> 3', 'ge', 30),
    O('the miss handler\'s set against the line\'s set', 'eq', 9),
    O('the guard\'s set against the line\'s set', 'eq', 9),
    O('the slot\'s set and way against the line\'s (and the victim\'s exclusion)', 'eq', 12),
    O('the slot\'s way decoded 3 -> 8 for the victim', 'ge', 10),
    O('the failed line: the source\'s, 6:1 (high) or 3:1 (low, three sources)', 'mux:6', 31, 1, 'high'),
    O('the failed line: the source\'s, 3:1', 'mux:3', 31, 1, 'low'),
    O('the failed line against the line looked up', 'eq', 31),
    O('the victim: the first invalid way; else age 7, or 6 if 7 is the slot\'s', 'zd', 3, 16),
    O('the victim: its priority', 'ge', 25),
    O('touch: the touched way\'s age, 8:1', 'mux:8', 3),
    O('touch: each way\'s age below it', 'lt', 3, 8),
    O('touch: each way\'s age plus one', 'ha', 3, 8),
    O('touch: each way\'s new age: 0, age + 1 or age', 'mux:3', 3, 8),
    O('touch: the way, the walker\'s or the miss handler\'s', 'mux:2', 3),
    O('tag row write: the installed way\'s tag', 'mux:2', 22, 8),
    O('tag row write: the row, as read or the miss handler\'s', 'mux:2', 216),
    O('tag row write: valid and dirty bits: set, clear', 'ge', 64),
    O('tag write address: the line\'s set or the miss handler\'s', 'mux:2', 9),
    O('the entry: 32 of the line\'s 1,024 bits, 32:1', 'mux:32', 32),
    O('the entry: a hole (all ones), owned bit', 'zd', 32),
    O('the index: the page or the line\'s page', 'mux:2', 30),
    O('the index: its 10-bit field at the level, 3:1', 'mux:3', 10),
    O('the next line: base (root or the page at k)', 'mux:2', 31),
    O('the next line: offset (index >> 5, line & 31, discard count), 3:1', 'mux:3', 6),
    O('the next line: base + offset', 'fa', 6, 1, 'high'),
    O('the next line: carry into the base\'s upper bits', 'ha', 25, 1, 'high'),
    O('beyond: the page or line past the depth\'s reach, the level against the depth', 'zd', 65),
    O('beyond: the masks by depth', 'ge', 40),
    O('the discard\'s 32nd line; k at the depth', 'zd', 10),
    O('the update\'s patch: the entry into its 32 bits of the line', 'mux:2', 1024),
    O('the update\'s patch: the 5 -> 32 decode', 'ge', 40),
    O('the data memory\'s write data: a fill write\'s line, the patched line, the miss handler\'s line (HIGH) or an '
      'R beat (LOW)', 'mux:3', 1024),
    O('the data memory: lane enables', 'ge', 20),
    O('the data memory: read address, the walker\'s or the miss handler\'s', 'mux:2', 12),
    O('the data memory: write address, 3:1', 'mux:3', 12),
    O('the fill read response: the line or zeros', 'and2', 1024),
    O('the W beat: 8:1 of 128 bits', 'mux:8', 128),
    O('the miss handler\'s beat lane decode', 'ge', 15),
    O('the victim\'s tag for its write-back address, 8:1', 'mux:8', 22),
    O('counters: k, the discard count, the beats in, the beat out', 'ha', 16),
    O('front end: each source eligible; the round robin over six with the miss\'s source first', 'ge', 120),
    O('front end: the readies', 'ge', 24),
    O('the kinds queue', 'ge', 20),
]
OPS['updater'] = [
    O('decoder: the code\'s bits needed, twice (this code, the next)', 'ge', 30),
    O('decoder: the bits there against the bits needed, twice', 'lt', 8, 2),
    O('decoder: the bit buffer shifted right by 0, 6, 34, 64 or 66', 'mux:5', 194),
    O('decoder: a 128-bit word put in at 0 to 65 (a 7-level shifter)', 'mux:2', 7 * 160),
    O('decoder: the word ORed in; the buffer cleared at START', 'and2', 194, 2),
    O('decoder: the bit count: minus the code, plus 128', 'fa', 8, 2),
    O('decoder: the LRU hit\'s entry, 32:1 of 64 bits', 'mux:32', 64),
    O('decoder: the move to front: which entries move (k <= i, or every one on a miss)', 'ge', 100),
    O('decoder: the update word: raw, a hit, a 34-bit or a 66-bit miss', 'mux:4', 64),
    O('decoder: the LRU table cleared at START, bit by bit (HIGH)', 'and2', 2048, 1, 'high'),
    O('decoder: the LRU read gated by its valid bit (LOW)', 'and2', 64, 1, 'low'),
    O('decoder: WORD_COUNT - 1, the next index + 1, the values left - 1', 'ha', 32, 3),
    O('decoder: min(values left, WORD_COUNT)', 'lt', 32),
    O('decoder: min\'s mux', 'mux:2', 32),
    O('decoder: the type: n >= 512, n >= 256, index & 511, index & 255, values left 0', 'zd', 80),
    O('the input queue: its head word, 4:1 of 128', 'mux:4', 128),
    O('the input queue: write enables; head and count', 'ge', 30),
    O('next: the value\'s page against the held page, 23 bits', 'eq', 23),
    O('next: beyond (the page past 2^(10 depth))', 'zd', 23),
    O('the page memory: its row, 5:1 of 8 bits', 'mux:5', 8),
    O('the page memory: its write data: an R beat, the value (to both halves), zeros', 'mux:3', 128),
    O('the page memory: its half enables', 'ge', 10),
    O('R: an index page\'s entries: a hole detect a lane, the owned bit cleared', 'zd', 128),
    O('R: owned bits cleared', 'and2', 4),
    O('R: the entry to catch: its row against the beat\'s, its lane 4:1', 'eq', 8),
    O('R: the caught lane', 'mux:4', 32),
    O('W: the beat from the page memory, zeros or ones', 'and2', 256),
    O('AR and AW: the address\'s source, 5:1 and 4:1 of 21 bits', 'mux:5', 21),
    O('AW: the address\'s source', 'mux:4', 21),
    O('AR and AW: + 2,048 x half (an adder in HIGH)', 'ha', 21, 2, 'high'),
    O('the look-up request: root (image or Tracer), page, level, entry', 'mux:2', 31 + 23),
    O('the look-up request: level and entry, 3:1', 'mux:3', 2 + 32),
    O('the digit of the held page at a level, 3:1 of 10 bits', 'mux:3', 10),
    O('the page memory holds the source: pm_src against from, pm_level against k', 'eq', 34),
    O('the fault\'s pages needed: two (depth - level), their sum, against PAGE_COUNT', 'fa', 3, 3),
    O('the fault\'s pages needed against PAGE_COUNT', 'lt', 32),
    O('PAGE_COUNT - 1', 'ha', 32),
    O('counters: the read row, the fill row, the rows left, the next row, the free page queue\'s head, count, gone',
      'ha', 8 + 8 + 9 + 8 + 8 + 9 + 7),
    O('the fill row\'s last; the credit\'s 128th; the rows left 1', 'zd', 24),
    O('the free page queue: its write address, head + count', 'fa', 8),
    O('the statistics counters + 1', 'ha', 32, 6),
    O('the reply\'s status decoded; the errors', 'ge', 30),
    O('the window: words 32-37 and 48-53 read, 12:1 of 32 bits', 'mux:12', 32),
    O('the window: words 32-37 written, the decode', 'ge', 30),
    O('the control block: done and credit event counts (depth 1 and 2)', 'ge', 40),
]
OPS['arbiter'] = [
    O('a slot\'s next address + 32 beats (an incrementer at bit 5)', 'ha', 23, 8, 'high'),
    O('a slot\'s beats left - 32, and below 32', 'fa', 9, 8, 'high'),
    O('a slot\'s piece length: min(left, 32)', 'mux:2', 6, 8, 'high'),
    O('the shared cutter: the served slot\'s fields, 4:1 of 37 bits, a direction', 'mux:4', 37, 2, 'low'),
    O('the shared cutter: + 32 beats, - 32 beats, min', 'ha', 23, 2, 'low'),
    O('the shared cutter: - 32 beats', 'fa', 9, 2, 'low'),
    O('the shared cutter: min', 'mux:2', 6, 2, 'low'),
    O('a burst\'s pieces: (len >> 5) + 1, a client a direction', 'ha', 4, 8),
    O('a list\'s head entry, 4:1 of 4 bits, its count - 1, its count at 0, a client a direction', 'mux:4', 4, 8),
    O('a list\'s count - 1 and its zero', 'ha', 4, 8),
    O('a list\'s new entry\'s write decode, head and count counters, a client a direction', 'ge', 30, 8),
    O('a write list\'s first error kept', 'ge', 6, 4),
    O('the round robins, AR and AW, over four', 'ge', 40, 2),
    O('AR and AW to lpddr: the served slot\'s address and len, 4:1 of 36 bits', 'mux:4', 36, 2),
    O('the W queue\'s input: the client in turn\'s beat, 4:1 of 129 bits', 'mux:4', 129),
    O('the W queue\'s output, 2:1 of 129 bits', 'mux:2', 129),
    O('the write piece in turn: beats to take - 1 and its zero; wlast\'s check is the simulator\'s', 'ha', 6),
    O('R and B steered by ID: the client decode', 'ge', 20),
    O('the readies: slot empty and the list below CLIENT_BURSTS', 'ge', 8, 8),
]


def op_ge(o):
    k = o['kind']
    if k == 'ge':
        return o['bits'] * o['count']
    if k.startswith('mux:'):
        n = int(k.split(':')[1])
        return (n - 1) * o['bits'] * o['count'] * COST['mux']
    return o['bits'] * o['count'] * COST[k]


def op_included(o, high):
    return o['when'] == 'both' or (o['when'] == 'high') == high


def ops_ge(unit, high):
    return sum(op_ge(o) for o in OPS[unit] if op_included(o, high))


# ============================================================================================ the control words
# For each machine: its named states and the flip-flops that hold its state; the signals its state logic drives
# into the rest of the block, with their widths; and its rows: each path through its state's logic in a cycle
# (code's branches), with the signals it sets (unset signals are 0: hold, none). A row's word is the set of its
# signals' values; the distinct words are what a table would hold.
MACHINES = OrderedDict()


def M(unit, name, states, state_ff, signals, rows, note='', named=False):
    """named: its states are an enum in the code (WState, MState, State); otherwise they are implicit in flags."""
    MACHINES[(unit, name)] = dict(states=states, state_ff=state_ff, signals=OrderedDict(signals), rows=rows, note=note,
                                  named=named)


# ---- the cache: the walker (cache_ctl.cpp's edge, W_*; start, step_cmp, step_data)
W_ST = {'idle': 0, 'compare': 1, 'read': 2, 'reply': 3}
M('cache', 'walker', ['idle', 'compare', 'read (W_DATA)', 'reply'], 2, [
    ('next', 2), ('pick', 1), ('k_op', 2), ('dk_op', 2), ('cur_sel', 2), ('x_ld', 1), ('x_base', 2), ('x_off', 2),
    ('way_ld', 1), ('own_op', 2), ('line_op', 2), ('pend_ld', 1), ('tag_re', 1), ('tag_we', 1), ('tag_src', 2),
    ('data_re', 1), ('data_we', 1), ('data_wsrc', 1), ('data_1lane', 1), ('rep_valid', 1), ('rep_pend', 1),
    ('rep_status', 3), ('rep_level_k', 1), ('rep_entry_e', 1), ('frr_valid', 1), ('frr_status', 2), ('frr_zero', 1),
    ('fwr_valid', 1), ('fwr_status', 1), ('req_free', 1), ('req_wait', 2), ('req_park', 1), ('req_failclr', 1),
    ('mh_start', 1), ('mh_alloc', 1), ('wr_upd', 1), ('slot_ld', 1), ('err_set', 1),
], [], named=True)
_w = MACHINES[('cache', 'walker')]['rows']


def wrow(name, st, **s):
    s.setdefault('next', W_ST[st] if 'next' not in s else s['next'])
    _w.append((name, s))


START = dict(pick=1, k_op=1, cur_sel=1, x_ld=1, own_op=1, tag_re=1, next=1)
wrow('idle: nothing to pick', 'idle', next=0)
wrow('idle: a discard starts', 'idle', **dict(START, dk_op=1, x_base=1, x_off=2))
wrow('idle: a look-up, an update or a fill (depth > 0) starts', 'idle', **dict(START, x_base=0, x_off=0))
wrow('idle: a fill of a depth-0 file starts', 'idle', **dict(START, x_base=0, x_off=1))
for st, (stat, lvl, ent) in {'beyond': (3, 0, 0)}.items():
    wrow('idle: a look-up or update beyond, the bus free', 'idle', pick=1, rep_valid=1, rep_status=stat, req_free=1, next=0)
    wrow('idle: a look-up or update beyond, the bus the slot\'s', 'idle', pick=1, pend_ld=1, rep_status=stat, next=3)
wrow('idle: a fill read beyond', 'idle', pick=1, frr_valid=1, frr_status=3, frr_zero=1, req_free=1, next=0)
wrow('idle: a fill write beyond', 'idle', pick=1, fwr_valid=1, fwr_status=1, req_free=1, next=0)
wrow('compare: the row read before the miss handler\'s write: read again', 'compare', tag_re=1, next=1)
wrow('compare: the same, the write not landed yet: wait', 'compare', next=1)
wrow('compare: the miss handler\'s set, or a miss with it busy: wait for it', 'compare', req_wait=1, next=0)
wrow('compare: the slot\'s line (a discard\'s or a fill write\'s): wait for the slot', 'compare', req_wait=2, next=0)
wrow('compare: a hit, the ports the miss handler\'s this cycle: wait', 'compare', next=1)
for inv in (0, 1):
    base = dict(tag_we=1, tag_src=2) if inv else {}
    pre = 'compare: a discard\'s line ' + ('in the cache, invalidated' if inv else 'not in the cache')
    wrow(pre + '; the next line', 'compare', **dict(base, dk_op=2, x_ld=1, x_base=1, x_off=2, tag_re=1, next=1))
    wrow(pre + '; the 32nd: the reply', 'compare', **dict(base, rep_valid=1, rep_status=0, req_free=1, next=0))
    wrow(pre + '; the 32nd: the reply waits for the bus', 'compare', **dict(base, pend_ld=1, rep_status=0, next=3))
wrow('compare: a fill write\'s data line hits: written, dirty; answered', 'compare', data_we=1, data_wsrc=0, tag_we=1,
     tag_src=1, fwr_valid=1, fwr_status=0, req_free=1, next=0)
wrow('compare: a hit: the line read, the way touched', 'compare', data_re=1, tag_we=1, tag_src=0, way_ld=1, line_op=1,
     next=2)
wrow('compare: its fetch failed: lpddr_error, the bus free', 'compare', rep_valid=1, rep_status=4, rep_level_k=1,
     req_free=1, req_failclr=1, next=0)
wrow('compare: its fetch failed: lpddr_error, waits for the bus', 'compare', pend_ld=1, rep_status=4, rep_level_k=1,
     req_failclr=1, next=3)
wrow('compare: a fill read\'s fetch failed: beyond, the error set', 'compare', frr_valid=1, frr_status=3, frr_zero=1,
     err_set=1, req_free=1, req_failclr=1, next=0)
wrow('compare: a fill write\'s fetch failed: error', 'compare', fwr_valid=1, fwr_status=1, req_free=1, req_failclr=1, next=0)
wrow('compare: a miss: the victim and the line to the miss handler', 'compare', mh_start=1, req_wait=1, next=0)
wrow('compare: a fill write\'s data line misses: allocated', 'compare', mh_start=1, mh_alloc=1, req_wait=1, next=0)
for own in (0, 2):
    wrow('read: a fill read\'s data line: ' + ('ok' if own == 0 else 'not_owned'), 'read', frr_valid=1,
         frr_status=own, req_free=1, next=0)
for st, code in (('hole', 1), ('not_owned', 2), ('ok', 0)):
    wrow('read: a look-up\'s (or an update\'s above its level) ' + st + ', the bus free', 'read', rep_valid=1,
         rep_status=code, rep_level_k=1, rep_entry_e=1, req_free=1, next=0)
    wrow('read: the same, ' + st + ', waits for the bus', 'read', pend_ld=1, rep_status=code, rep_level_k=1,
         rep_entry_e=1, next=3)
for off in (0, 1):
    for own in (0, 2):
        wrow('read: on to the next level' + (' (the data line)' if off else '') + (', not owned' if own else ''),
             'read', cur_sel=2, k_op=2, x_ld=1, x_base=2, x_off=off, tag_re=1, own_op=own, next=1)
wrow('read: an update\'s target, the AXI writer busy or the port the miss handler\'s: wait', 'read', next=2)
wrow('read: an update\'s target: patched, written, handed to the writer, parked', 'read', line_op=2, data_we=1,
     data_wsrc=1, data_1lane=1, wr_upd=1, slot_ld=1, req_park=1, next=0)
wrow('read: a fill read\'s hole', 'read', frr_valid=1, frr_status=1, frr_zero=1, req_free=1, next=0)
wrow('read: a fill write\'s hole or not owned', 'read', fwr_valid=1, fwr_status=1, req_free=1, next=0)
wrow('reply: the bus the slot\'s again: wait', 'reply', next=3)
wrow('reply: the waiting reply on the bus', 'reply', rep_valid=1, rep_pend=1, req_free=1, next=0)

# ---- the cache: the miss handler and its AXI reader (step_mh, edge's AR and R)
MH_ST = dict(idle=0, wb_read=1, wb_hand=2, wb_wait=3, fetch=4, fill=5, install=6, fail=7)
M('cache', 'miss handler (with the AXI reader)', ['idle', 'read the victim', 'hand over', 'wait', 'fetch', 'fill',
                                                  'install', 'fail'], 3, [
    ('next', 3), ('ld', 1), ('data_re', 1), ('copied_op', 2), ('buf_src', 2), ('got_op', 2), ('err_op', 2),
    ('ar_op', 2), ('wr_wb', 1), ('tag_we', 1), ('tag_fail', 1), ('data_we', 1), ('guard_ld', 1), ('fail_owner', 1),
    ('wake', 1), ('prio_set', 1), ('err_set', 1),
], [], named=True)
_m = MACHINES[('cache', 'miss handler (with the AXI reader)')]['rows']
for n, s in [
    ('idle', dict(next=0)),
    ('idle: a dirty victim', dict(ld=1, next=1)),
    ('idle: an allocation, the victim clean', dict(ld=1, buf_src=2, next=6)),
    ('idle: a fetch', dict(ld=1, ar_op=1, next=4)),
    ('read the victim', dict(data_re=1, copied_op=2, next=2)),
    ('hand over: copied, the writer busy', dict(buf_src=1, copied_op=1, next=2)),
    ('hand over: copied and handed', dict(buf_src=1, copied_op=1, wr_wb=1, next=3)),
    ('hand over: the writer busy', dict(next=2)),
    ('hand over: handed', dict(wr_wb=1, next=3)),
    ('hand over: handed, the allocation\'s line in', dict(wr_wb=1, buf_src=2, next=3)),
    ('wait', dict(next=3)),
    ('wait: B ok, fetch', dict(ar_op=1, next=4)),
    ('wait: B ok, install the allocation', dict(next=6)),
    ('wait: B an error, fetch', dict(err_set=1, ar_op=1, next=4)),
    ('wait: B an error, install', dict(err_set=1, next=6)),
    ('fetch: arvalid', dict(next=4)),
    ('fetch: AR taken', dict(ar_op=2, got_op=1, err_op=2, next=5)),
    ('fill: a beat', dict(buf_src=3, got_op=2, next=5)),
    ('fill: a beat with an error', dict(buf_src=3, got_op=2, err_op=1, next=5)),
    ('fill: the last beat, all ok', dict(buf_src=3, got_op=2, next=6)),
    ('fill: the last beat, an error', dict(buf_src=3, got_op=2, next=7)),
    ('fill: no beat', dict(next=5)),
    ('install', dict(tag_we=1, data_we=1, guard_ld=1, wake=1, prio_set=1, next=0)),
    ('fail', dict(tag_we=1, tag_fail=1, guard_ld=1, fail_owner=1, wake=1, prio_set=1, next=0)),
]:
    _m.append((n, s))

# ---- the cache: the AXI writer, its B side and the update slot
M('cache', 'AXI writer, B and the update slot', ['idle', 'busy (the address taken or not, beats 0 to 8)'],
  1 + 1 + 4, [
      ('busy_op', 2), ('aw_op', 2), ('beat_op', 2), ('buf_src', 2), ('addr_src', 1), ('kind_push', 1), ('kind', 1),
      ('kind_pop', 1), ('slot_reply', 1), ('slot_err', 1), ('slot_clr', 1), ('wake_slot', 1), ('mh_b', 1),
      ('mh_b_err', 1), ('awvalid', 1), ('wvalid', 1), ('wlast', 1),
  ], [
      ('idle', {}),
      ('an update\'s line taken', dict(busy_op=1, aw_op=2, beat_op=1, buf_src=1, addr_src=0, kind_push=1, kind=0)),
      ('a write-back taken', dict(busy_op=1, aw_op=2, beat_op=1, buf_src=2, addr_src=1, kind_push=1, kind=1)),
      ('busy: AW and a beat offered', dict(awvalid=1, wvalid=1)),
      ('busy: AW taken', dict(aw_op=1, wvalid=1)),
      ('busy: a beat taken', dict(beat_op=2, awvalid=1, wvalid=1)),
      ('busy: AW and a beat taken', dict(aw_op=1, beat_op=2, wvalid=1)),
      ('busy: the last beat offered', dict(wvalid=1, wlast=1)),
      ('busy: done', dict(busy_op=2)),
      ('B: an update\'s, ok', dict(kind_pop=1, slot_reply=1, slot_clr=1, wake_slot=1)),
      ('B: an update\'s, an error', dict(kind_pop=1, slot_reply=1, slot_err=1, slot_clr=1, wake_slot=1)),
      ('B: a write-back\'s, ok', dict(kind_pop=1, mh_b=1)),
      ('B: a write-back\'s, an error', dict(kind_pop=1, mh_b=1, mh_b_err=1)),
  ], note='AW, W and B run beside each other: the rows are the writer\'s, and B\'s, separately')

# ---- the cache: the front end (six request registers, the pick)
M('cache', 'front end', ['a source: empty', 'waiting', 'waiting for the miss handler', 'waiting for the slot',
                         'parked (an update in the slot)', 'behind a write (fill\'s read)'],
  4 * 4 + 3 + 3 + 3 + 4, [
      ('ld', 6), ('ready', 6), ('rr_ld', 1), ('prio_clr', 1),
  ], [
      ('nothing', {}), ('a request taken at a port', dict(ld=1)), ('a pick by round robin', dict(rr_ld=1, prio_clr=1)),
      ('a pick of the miss\'s source', dict(prio_clr=1)),
  ], note='the readies are each register\'s valid, and the reply to its port; the six sources are alike: one row a '
          'source a cycle, a request taken, freed or set waiting by the walker\'s and the miss handler\'s signals above')

# ---- the Updater: the run (updater_ctl.h's switch (q.state))
U_ST = ['idle', 'stopped', 'check', 'root read', 'root write', 'next', 'half read', 'fill', 'flush', 'flush read',
        'flush write', 'flush update', 'look-up', 'level', 'load', 'take', 'new write', 'new update']
UI = {s: i for i, s in enumerate(U_ST)}
M('updater', 'the run', U_ST, 5 + 2 + 1, [
    ('next', 5), ('level_op', 2), ('side_op', 2), ('look_ld', 1), ('src_ld', 2), ('pm_ok_op', 2), ('pm_src_ld', 1),
    ('new_page_ld', 1), ('fill_row_op', 2), ('held_op', 2), ('held_kind', 2), ('held_page_ld', 1),
    ('held_at_sel', 2), ('have_op', 3), ('dirty_op', 2), ('rd_go', 1), ('rd_addr', 2), ('rd_half', 2), ('rd_two', 1),
    ('rd_xform', 2), ('rd_catch', 1), ('wr_go', 1), ('wr_addr', 2), ('wr_half', 2), ('wr_two', 1), ('wr_src0', 2),
    ('wr_src1', 2), ('lk_go', 1), ('lk_op', 1), ('lk_root', 1), ('lk_page', 1), ('lk_level', 2), ('lk_entry', 1),
    ('pm_patch', 1), ('pm_fill', 1), ('take', 1), ('fp_pop', 1), ('pc_dec', 1), ('busy_clr', 1), ('done', 1),
    ('stop', 1), ('err', 3), ('stat', 2), ('credit', 1),
], [], named=True)
_u = MACHINES[('updater', 'the run')]['rows']


def urow(name, st, **s):
    s.setdefault('next', UI[st])
    s['next'] = UI[s['next']] if isinstance(s['next'], str) else s['next']
    _u.append((st + ': ' + name, s))


STOP = lambda e: dict(stop=1, err=e, next='stopped')
# err codes: 1 beyond, 2 lpddr, 3 out_of_pages, 4 not_owned; stat: 1 data, 2 pages, 3 memory
urow('nothing', 'idle'); urow('nothing', 'stopped')
urow('root 0', 'check', next='next')
urow('root 1, depth > 0: a page of holes written', 'check', wr_go=1, wr_addr=0, wr_two=1, wr_src0=2, wr_src1=2,
     side_op=1, next='root write')
urow('root 1, depth 0: a page of zeros written', 'check', wr_go=1, wr_addr=0, wr_two=1, wr_src0=1, wr_src1=1,
     side_op=1, next='root write')
urow('root 2, depth > 0: the source read, owned bits cleared', 'check', rd_go=1, rd_addr=0, rd_two=1, rd_xform=1,
     next='root read')
urow('root 2, depth 0: the source read', 'check', rd_go=1, rd_addr=0, rd_two=1, next='root read')
for st, nx in (('root read', 'root write'), ('half read', 'next'), ('flush read', 'flush'), ('load', 'take')):
    urow('waiting for the burst', st, stat=3 if st == 'load' else 0)
    urow('an error', st, **STOP(2))
urow('the root written', 'root read', wr_go=1, wr_addr=0, wr_two=1, side_op=1, next='root write')
urow('waiting for B', 'root write')
urow('an error', 'root write', **STOP(2))
urow('the Tracer\'s copy\'s root written', 'root write', wr_go=1, wr_addr=1, wr_two=1, side_op=2)
urow('on', 'root write', next='next')
urow('no value, WORD_COUNT not 0: waiting for data', 'next', stat=1)
urow('no value, a page held, or another page: flush', 'next', next='flush')
urow('no value, none held: done', 'next', busy_clr=1, done=1, next='idle')
urow('beyond', 'next', **STOP(1))
urow('none held, depth 0: the root held, owned', 'next', held_page_ld=1, pm_ok_op=2, held_op=1, held_kind=0,
     held_at_sel=1, have_op=1, dirty_op=1)
urow('none held: the image\'s look-up', 'next', held_page_ld=1, pm_ok_op=2, lk_go=1, lk_op=0, lk_root=0, lk_page=0,
     side_op=1, next='look-up')
urow('the half not had, covered by the run: patched', 'next', have_op=3, pm_patch=1, dirty_op=2, take=1)
urow('the half not had, a hole: filled', 'next', fill_row_op=1, next='fill')
urow('the half not had: read', 'next', rd_go=1, rd_addr=1, rd_half=1, next='half read')
urow('the value patched', 'next', pm_patch=1, dirty_op=2, take=1)
urow('the half had', 'half read', have_op=3, next='next')
urow('a row filled', 'fill', pm_fill=1, fill_row_op=2)
urow('the half\'s last row filled', 'fill', pm_fill=1, fill_row_op=2, have_op=3, next='next')
urow('owned, both halves dirty: written', 'flush', wr_go=1, wr_addr=2, wr_two=1, next='flush write')
urow('owned, one half dirty: written', 'flush', wr_go=1, wr_addr=2, wr_half=1, next='flush write')
urow('owned, nothing dirty: released', 'flush', held_op=2, next='next')
urow('new, halves missing: read', 'flush', rd_go=1, rd_addr=1, rd_half=2, rd_two=1, next='flush read')
urow('new: written whole', 'flush', wr_go=1, wr_addr=2, wr_two=1, wr_src0=3, wr_src1=3, next='flush write')
urow('the halves had', 'flush read', have_op=2, next='flush')
urow('waiting for B', 'flush write', stat=3)
urow('an error', 'flush write', **STOP(2))
urow('owned: released', 'flush write', held_op=2, next='next')
urow('new: its update', 'flush write', lk_go=1, lk_op=1, lk_root=0, lk_page=1, lk_level=1, lk_entry=0,
     next='flush update')
for st in ('flush update', 'new update', 'look-up'):
    urow('waiting for the reply', st, stat=3)
    urow('lpddr_error', st, **STOP(2))
urow('ok: released', 'flush update', held_op=2, next='next')
urow('not_owned', 'flush update', **STOP(4))
urow('the image\'s in, with tracer_copy: the copy\'s look-up', 'look-up', look_ld=1, src_ld=1, lk_go=1, lk_op=0,
     lk_root=1, lk_page=1, side_op=2)
urow('both in: out of pages', 'look-up', look_ld=1, src_ld=1, **STOP(3))
urow('both in: owned, held', 'look-up', look_ld=1, src_ld=1, held_op=1, held_kind=0, held_at_sel=2, have_op=1,
     dirty_op=1, next='next')
urow('both in: a fault, its walk', 'look-up', look_ld=1, src_ld=1, level_op=1, side_op=2, next='level')
urow('past the data level: the image\'s page held', 'level', held_op=1, held_kind=0, held_at_sel=2, have_op=1,
     dirty_op=1, next='next')
urow('nothing at this (level, file): the next, the image', 'level', side_op=1)
urow('nothing at this (level, file): the next, the Tracer\'s copy a level down', 'level', side_op=2, level_op=2)
urow('the image\'s data page, or a source already loaded, or none: take', 'level', next='take')
urow('an index page read, owned bits cleared, the next entry caught', 'level', rd_go=1, rd_addr=2, rd_two=1,
     rd_xform=1, rd_catch=1, pm_ok_op=2, pm_src_ld=1, next='load')
urow('a data page read', 'level', rd_go=1, rd_addr=2, rd_two=1, pm_ok_op=2, pm_src_ld=1, next='load')
urow('loaded', 'load', pm_ok_op=1, next='take')
urow('the queue empty: waiting', 'take', stat=2)
urow('the image\'s data page: taken, held', 'take', fp_pop=1, pc_dec=1, held_op=1, held_kind=1, held_at_sel=3,
     have_op=4, dirty_op=1, next='next')
for src, nm in ((0, 'the source as loaded'), (2, 'holes'), (1, 'zeros')):
    urow('taken, written whole: ' + nm, 'take', fp_pop=1, pc_dec=1, wr_go=1, wr_addr=3, wr_two=1, wr_src0=src,
         wr_src1=src, new_page_ld=1, next='new write')
urow('waiting for B', 'new write', stat=3)
urow('an error', 'new write', **STOP(2))
urow('its update', 'new write', lk_go=1, lk_op=1, lk_root=1, lk_page=1, lk_level=2, lk_entry=1, next='new update')
urow('not_owned', 'new update', **STOP(4))
urow('ok: the next, the image', 'new update', src_ld=2, side_op=1, next='level')
urow('ok: the next, the Tracer\'s copy a level down', 'new update', src_ld=2, side_op=2, level_op=2, next='level')

# ---- the Updater: the decoder and the input queue
M('updater', 'decoder', ['(no named states: the bit count, a run open or not, the output register full or not)'],
  8 + 1 + 1, [('dec', 1), ('need', 2), ('hdr', 1), ('val', 1), ('lru_hit', 1), ('lru_miss', 1), ('refill', 1),
              ('clear', 1), ('ready', 1)], [])
_d = MACHINES[('updater', 'decoder')]['rows']
_d.append(('nothing', {}))
_d.append(('a refill alone', dict(refill=1)))
_d.append(('START with continue 0: cleared', dict(clear=1)))
for need, nm in ((0, 'raw'), (1, 'a hit'), (2, 'a 34-bit miss'), (3, 'a 66-bit miss')):
    for kind in ('hdr', 'val'):
        for rf in (0, 1):
            s = dict(dec=1, need=need, **{kind: 1})
            if need == 1: s['lru_hit'] = 1
            if need >= 2: s['lru_miss'] = 1
            if rf: s['refill'] = 1
            _d.append(('%s, %s%s' % (nm, 'a header word' if kind == 'hdr' else 'a value', ', and a refill' if rf else ''), s))

# ---- the Updater: the AXI channels and the look-up port (the parts of edge outside the switch)
M('updater', 'AXI channels and the look-up port', ['AR: idle, offered, the read in progress',
                                                    'AW and W: idle, offered, beats pending, B awaited',
                                                    'look-up: idle, offered, the reply awaited'],
  1 + 1 + 1 + 1 + 1 + 1 + 1 + 1, [
      ('ar_clr', 1), ('beat_in', 1), ('rd_last', 1), ('rd_err', 1), ('catch', 1), ('aw_clr', 1), ('w_take', 1),
      ('w_fetch', 1), ('w_hold', 1), ('b_in', 1), ('b_err', 1), ('lk_taken', 1), ('rp_ld', 1),
      ('w_valid', 1), ('w_last', 1),
  ], [
      ('nothing', {}), ('AR taken', dict(ar_clr=1)), ('an R beat', dict(beat_in=1)), ('an R beat caught', dict(beat_in=1, catch=1)),
      ('an R beat, an error', dict(beat_in=1, rd_err=1)), ('the last R beat', dict(beat_in=1, rd_last=1)),
      ('AW taken', dict(aw_clr=1)), ('a beat taken, the next row read', dict(w_take=1, w_fetch=1, w_valid=1)),
      ('the last beat taken', dict(w_take=1, w_valid=1, w_last=1)), ('W held: wready 0', dict(w_hold=1)), ('B', dict(b_in=1)),
      ('B, an error', dict(b_in=1, b_err=1)), ('the request taken', dict(lk_taken=1)), ('the reply', dict(rp_ld=1)),
  ], note='R, W, B and the look-up port move independently: rows a channel, not a table of their products')

# ---- the arbiter
M('arbiter', 'the AR and AW turns, the write piece, the W queue, the lists', [
    'a slot: empty or cutting', 'a list: 0 to 4 bursts', 'the write piece: none or in turn (1 to 32 beats left)',
    'the W queue: 0, 1, 2'], 4 + 4 + 4 * 3 + 4 * 3 + 1 + 6 + 2 + 4, [
    ('ar_turn', 1), ('aw_turn', 1), ('ar_served', 2), ('aw_served', 2), ('slot_ld', 8), ('list_push', 8),
    ('r_beat', 1), ('r_last_piece', 1), ('b_piece', 1), ('b_last', 1), ('wq_push', 1), ('wq_pop', 1),
    ('wcur_done', 1), ('readies', 12), ('rvalid', 4), ('bvalid', 4),
], [
    ('nothing', {}), ('an AR turn', dict(ar_turn=1)), ('an AW turn', dict(aw_turn=1)),
    ('a burst taken on AR', dict(list_push=1)), ('a burst taken on AW', dict(list_push=1)),
    ('an R beat', dict(r_beat=1)), ('a piece\'s last R beat', dict(r_beat=1, r_last_piece=1)),
    ('a B, not the last piece\'s', dict(b_piece=1)), ('a B, the burst\'s last', dict(b_piece=1, b_last=1)),
    ('a W beat in', dict(wq_push=1)), ('a W beat out', dict(wq_pop=1)), ('the piece\'s last beat in', dict(wq_push=1, wcur_done=1)),
], note='no state machine: per-client slots, lists and counters, each changed by its own handshake')


def machine_width(key):
    return sum(MACHINES[key]['signals'].values())


def words(rows, with_next=True):
    return set(frozenset((k, v) for k, v in s.items() if v and (with_next or k != 'next')) for _, s in rows)


def control_ge(unit, high):
    ge = 0.0
    for (u, name), m in MACHINES.items():
        if u != unit:
            continue
        nrows = len(m['rows'])
        or_bits = 0
        for _, s in m['rows']:
            for sig, v in s.items():
                if v:
                    or_bits += bin(int(v)).count('1')
        mult = 6 if (unit == 'cache' and name == 'front end') else 1   # six sources alike
        mult = 4 if (unit == 'arbiter') else mult                       # four clients alike
        ge += mult * (nrows * CTL_ROW_GE[high] + or_bits * CTL_OR_GE)
        # the flag registers' set and clear: ~3 GE a flag bit
    flags = sum(r['bits'] * reg_count(r, unit, high) for r in REGS[unit] if r['kind'] == 'flag'
                and reg_included(r, unit, high))
    return ge + 3.0 * flags


# ============================================================================================ the memories, bits
MEMS = OrderedDict()
MEMS['cache'] = [('data memory: 4,096 x 1,024, one read and one write port, 8 lane enables', 4096 * 1024),
                 ('tag memory: 512 x 216, one read and one write port', 512 * 216)]
MEMS['updater'] = [('page memory: 256 x 128, one port, two half enables', 256 * 128),
                   ('free page queue: 256 x 32', 256 * 32)]
MEMS['arbiter'] = []
MEMS['core'] = [('IMEM: 32 KiB, the core\'s read port and a NoC write port', 32 * 1024 * 8),
                ('DMEM: 32 KiB, the core\'s port and a NoC write port', 32 * 1024 * 8),
                ('boot ROM: 256 bytes', 256 * 8)]

# ============================================================================================ the core, bottom-up
# The tile's core (spec/core.md): RV32I + C + Zmmul + Zicsr + Zicond, machine mode, four stages (fetch, decode,
# execute, writeback), single issue, no branch prediction, the multiply in two pipelined stages (the four 17 x 17
# products of the operands' halves in execute, their sum in writeback), 32 GPRs, the CSRs of core.md's table. Its
# insides are not modelled gate by gate in meshsim; this is an estimate by parts with the same costs, set beside
# what is published for cores of its kind (CORE_PUBLISHED).
CORE_REGS = [
    R('GPRs x1-x31', 31 * 32, 1, 1),
    R('fetch: the PC 31, outstanding and stale bits 3', 34),
    R('fetch: the buffer, 16 bytes and their valid bits', 128 + 8),
    R('decode: instruction 32, PC 31, valid, compressed', 65),
    R('execute: operands 64, immediate 32, PC 31, rd 5, control 30', 162),
    R('writeback: result 32, rd 5, control 10, load control 5', 52),
    R('writeback: the four 34-bit partial products', 136),
    R('data port request: address 32, data 32, byte enables 4, valid, write, the response target', 71, 1, 1, 'out'),
    R('coprocessor port request: instruction 32, sources 64, valid', 97, 1, 1, 'out'),
    R('CSRs: mstatus 2, mie 1, mtvec 30, mscratch 32, mepc 31, mcause 6, mtval 32', 134),
    R('CSRs: mcycle 64, minstret 64', 128),
    R('trap taken, WFI, the status outputs', 7, kind='flag'),
]
CORE_OPS = [
    O('GPR read ports: 2 x 32:1 of 32 bits', 'mux:32', 32, 2),
    O('GPR write: an enable a register (HIGH: a hold mux a bit)', 'mux:2', 992, 1, 'high'),
    O('ALU: adder/subtractor 32 (SLT and branches by its carry)', 'fa', 32),
    O('ALU: subtract\'s inversion', 'xor', 32),
    O('ALU: the shifter, left, right and arithmetic', 'mux:2', 32 * 5 * 3 // 2),
    O('ALU: AND, OR, XOR', 'and2', 32 * 3),
    O('ALU: Zicond\'s zero test and its gating', 'zd', 32),
    O('ALU: Zicond gating', 'and2', 32),
    O('ALU: the result, 8:1', 'mux:8', 32),
    O('the branch target: PC + immediate (execute)', 'fa', 32),
    O('the jump target in decode: PC + immediate', 'fa', 32),
    O('PC + 2 or + 4 (fetch, the link value)', 'ha', 31, 2),
    O('forwarding: two operands, 3:1', 'mux:3', 32, 2),
    O('the immediate, 5 formats and RVC\'s', 'mux:6', 32),
    O('fetch: alignment of a halfword and a word across two', 'mux:3', 32),
    O('fetch: the buffer\'s control', 'ge', 300),
    O('decode: RV32I, Zicsr, Zmmul, Zicond, the custom opcodes; illegal instructions', 'ge', 1800),
    O('decode: RVC\'s expander', 'ge', 1100),
    O('multiply: four 17 x 17 signed products, partial product bits', 'and2', 289, 4),
    O('multiply: their reduction and carry-propagate (about n^2 full adders a product)', 'fa', 289, 4),
    O('multiply: the four products\' sum in writeback (two carry-save rows and a 64-bit adder)', 'fa', 160),
    O('load: the byte or halfword, extended; store: placed in its lanes', 'mux:4', 56),
    O('load and store: the byte enables, the misalignment trap', 'ge', 40),
    O('CSRs: the read, 13:1 of 32 bits', 'mux:13', 32),
    O('CSRs: CSRRW/S/C\'s write value', 'ge', 130),
    O('CSRs: mcycle and minstret + 1', 'ha', 64, 2),
    O('CSRs: mepc, mcause, mtval at a trap', 'mux:2', 69),
    O('traps, MRET, the NMI, WFI and the wake', 'ge', 400),
    O('the pipeline\'s stalls, the load-use and multiply waits, the redirect', 'ge', 1000),
    O('the data decoder\'s windows and the fetch address check', 'ge', 400),
]
CORE_PUBLISHED = [
    ('Ibex "small": RV32IMC, 2 stages, a 3-cycle multiplier', '26.06 kGE (Yosys), about 24 kGE commercial'),
    ('Ibex "maxperf": RV32IMC, a 1-cycle multiplier, branch target ALU, a writeback stage', '35.64 kGE (Yosys), '
     'about 33 kGE commercial'),
    ('the source', 'lowRISC Ibex README, its configuration table (latch-based register file; M includes a divider)'),
]
# The specification's area for the core (24; 25): about 0.15 mm2 for the core, its memories and its registers,
# "0.14 mm2 for the 64 KiB of IMEM and DMEM, 0.01 mm2 of core logic" (25), at 12 nm, rough.


def core_flops(high):
    return sum(r['bits'] * r['count'] for r in CORE_REGS if high or 'coprocessor' not in r['name'])


def core_ge(high):
    ff = core_flops(high) * COST['ff']
    ops = sum(op_ge(o) for o in CORE_OPS if op_included(o, high))
    mux_hold = 0 if not high else sum(r['bits'] for r in CORE_REGS if r['kind'] not in ('out', 'flag')
                                      and 'GPR' not in r['name']) * COST['mux']
    logic = (ops + mux_hold) * BUFFERING[high]
    return ff, logic


# ============================================================================================ the totals
def unit_ge(unit, high):
    ff = flops(unit, high)
    ffge = ff * COST['ff']
    mux_bits, icg = reg_mux_bits(unit, high)
    regmux = mux_bits * COST['mux'] + (0 if high else icg * COST['icg'])
    ops = ops_ge(unit, high)
    ctl = control_ge(unit, high)
    logic = (regmux + ops + ctl) * BUFFERING[high]
    return dict(ff=ff, ffge=ffge, regmux=regmux, ops=ops, ctl=ctl, logic=logic)


def top_ops(unit, high, n=5):
    items = [(op_ge(o), o['name']) for o in OPS[unit] if op_included(o, high)]
    mux_bits, _ = reg_mux_bits(unit, high)
    items.append((mux_bits * COST['mux'], 'the registers\' input multiplexers' + (' and holds' if high else '')))
    items.append((control_ge(unit, high), 'the next-state and output logic'))
    return sorted(items, reverse=True)[:n]


def summary():
    out = OrderedDict()
    for unit in ('cache', 'updater', 'arbiter'):
        lo, hi = unit_ge(unit, False), unit_ge(unit, True)
        named = sum(len(m['states']) for (u, n), m in MACHINES.items() if u == unit and m['named'])
        named_ff = sum(m['state_ff'] for (u, n), m in MACHINES.items() if u == unit and m['named'])
        st_ff = sum(m['state_ff'] for (u, n), m in MACHINES.items() if u == unit)
        width = sum(machine_width(k) for k in MACHINES if k[0] == unit)
        nrows = sum(len(MACHINES[k]['rows']) for k in MACHINES if k[0] == unit)
        ndist = sum(len(words(MACHINES[k]['rows'])) for k in MACHINES if k[0] == unit)
        out[unit] = dict(named_states=named, named_ff=named_ff, state_ff=st_ff, ctl_width=width, ctl_rows=nrows, ctl_words=ndist,
                         ff_coded=flops_as_coded(unit), ff_low=lo['ff'], ff_spec=SPEC_FF.get(unit),
                         ffge_low=lo['ffge'], ffge_high=hi['ffge'], logic_low=lo['logic'], logic_high=hi['logic'],
                         mem_bits=sum(b for _, b in MEMS[unit]), lo=lo, hi=hi)
    cff_lo, clog_lo = core_ge(False)
    cff_hi, clog_hi = core_ge(True)
    out['core'] = dict(ff_low=core_flops(False), ff_coded=core_flops(True), ffge_low=cff_lo, ffge_high=cff_hi,
                       logic_low=clog_lo, logic_high=clog_hi, mem_bits=sum(b for _, b in MEMS['core']))
    return out


# ============================================================================================ the report's tables
def fmt(x):
    return '{:,}'.format(int(round(x)))


def kge(x):
    return '%.1f' % (x / 1000.0)


def tables():
    S = summary()
    L = []
    p = L.append
    p('# COUNTS.md: what count.py counts (generated; do not edit)\n')
    p('Costs (GE, NAND2 = 1): ' + ', '.join('%s %.2f' % kv for kv in COST.items()) + '.\n')
    p('## The summary\n')
    p('| | named states (their flip-flops) | all state flip-flops | control word, bits | control rows / distinct words | flip-flops as '
      'coded (LOW) | spec\'s | flip-flops, kGE LOW-HIGH | logic, kGE LOW-HIGH | memory, bits |')
    p('|---|---|---|---|---|---|---|---|---|---|')
    for u in ('cache', 'updater', 'arbiter'):
        s = S[u]
        p('| %s | %d (%d) | %d | %d | %d / %d | %s (%s) | %s | %s-%s | %s-%s | %s |' % (
            u, s['named_states'], s['named_ff'], s['state_ff'], s['ctl_width'], s['ctl_rows'], s['ctl_words'], fmt(s['ff_coded']),
            fmt(s['ff_low']), fmt(s['ff_spec']), kge(s['ffge_low']), kge(s['ffge_high']), kge(s['logic_low']),
            kge(s['logic_high']), fmt(s['mem_bits'])))
    s = S['core']
    p('| core | | | | | %s (%s) | | %s-%s | %s-%s | %s |' % (fmt(s['ff_coded']), fmt(s['ff_low']), kge(s['ffge_low']),
                                                            kge(s['ffge_high']), kge(s['logic_low']),
                                                            kge(s['logic_high']), fmt(s['mem_bits'])))
    p('')
    p('## LOW and HIGH\n')
    p('| switch | HIGH | LOW |')
    p('|---|---|---|')
    for k, (h, l) in ASSUME.items():
        p('| %s | %s | %s |' % (k, h, l))
    p('')
    for u in ('cache', 'updater', 'arbiter'):
        s = S[u]
        p('## %s\n' % u)
        p('### Registers\n')
        p('| register | bits | count | sources | kind | note |')
        p('|---|---|---|---|---|---|')
        for r in REGS[u]:
            p('| %s | %s | %d | %d | %s | %s |' % (r['name'], fmt(r['bits']), r['count'], r['sources'], r['kind'], r['note']))
        p('')
        p('As coded: **%s** flip-flops (the specification: %s). LOW: %s.\n' % (fmt(s['ff_coded']), fmt(s['ff_spec']),
                                                                              fmt(s['ff_low'])))
        p('Not counted:\n')
        for n, why in NOT_HW[u]:
            p('- %s: %s' % (n, why))
        p('')
        p('### Datapath operators\n')
        p('| operator | kind | bits | count | when | GE |')
        p('|---|---|---|---|---|---|')
        for o in OPS[u]:
            p('| %s | %s | %s | %d | %s | %s |' % (o['name'], o['kind'], fmt(o['bits']), o['count'], o['when'], fmt(op_ge(o))))
        p('')
        p('### Control\n')
        for (uu, name), m in MACHINES.items():
            if uu != u:
                continue
            p('**%s.** States: %s. State flip-flops: %d. Control word: %d bits, %d rows, %d distinct words (%d '
              'without the next state). %s\n' % (name, ', '.join(m['states']), m['state_ff'], machine_width((uu, name)),
                                                  len(m['rows']), len(words(m['rows'])),
                                                  len(words(m['rows'], False)), m['note']))
            p('Signals: ' + ', '.join('%s %d' % kv for kv in m['signals'].items()) + '.\n')
            p('<details><summary>its rows</summary>\n')
            for n, sg in m['rows']:
                p('- %s: `%s`' % (n, ' '.join('%s=%s' % kv for kv in sg.items() if kv[1]) or 'nothing'))
            p('\n</details>\n')
        p('### Gates\n')
        p('| part | LOW, GE | HIGH, GE |')
        p('|---|---|---|')
        for part, key in (('flip-flops', 'ffge'), ('registers\' input muxes and holds (and LOW\'s ICGs)', 'regmux'),
                          ('datapath operators', 'ops'), ('next-state and output logic', 'ctl'),
                          ('logic, with HIGH\'s buffering', 'logic')):
            p('| %s | %s | %s |' % (part, fmt(s['lo'][key]), fmt(s['hi'][key])))
        p('')
        p('Largest, HIGH: ' + '; '.join('%s %s GE' % (n, fmt(g)) for g, n in top_ops(u, True)) + '.\n')
        p('Largest, LOW: ' + '; '.join('%s %s GE' % (n, fmt(g)) for g, n in top_ops(u, False)) + '.\n')
        p('Memories: ' + ('; '.join('%s: %s bits' % (n, fmt(b)) for n, b in MEMS[u]) or 'none') + '.\n')
    p('## core\n')
    p('| register | bits |')
    p('|---|---|')
    for r in CORE_REGS:
        p('| %s | %s |' % (r['name'], fmt(r['bits'] * r['count'])))
    p('')
    p('| operator | kind | bits | count | when | GE |')
    p('|---|---|---|---|---|---|')
    for o in CORE_OPS:
        p('| %s | %s | %s | %d | %s | %s |' % (o['name'], o['kind'], fmt(o['bits']), o['count'], o['when'], fmt(op_ge(o))))
    p('')
    s = S['core']
    p('Core: %s-%s flip-flops; flip-flops %s-%s kGE; logic %s-%s kGE; in all %s-%s kGE. Memories: %s bits.\n' % (
        fmt(s['ff_low']), fmt(s['ff_coded']), kge(s['ffge_low']), kge(s['ffge_high']), kge(s['logic_low']),
        kge(s['logic_high']), kge(s['ffge_low'] + s['logic_low']), kge(s['ffge_high'] + s['logic_high']),
        fmt(s['mem_bits'])))
    p('Published, for cores of its kind:\n')
    for n, v in CORE_PUBLISHED:
        p('- %s: %s' % (n, v))
    p('')
    return '\n'.join(L), S


if __name__ == '__main__':
    text, S = tables()
    with open(os.path.join(HERE, 'COUNTS.md'), 'w') as f:
        f.write(text + '\n')
    js = {u: {k: v for k, v in s.items() if k not in ('lo', 'hi')} for u, s in S.items()}
    with open(os.path.join(HERE, 'counts.json'), 'w') as f:
        json.dump(js, f, indent=1)
    if '-q' not in sys.argv:
        print(text.split('## LOW and HIGH')[0])
