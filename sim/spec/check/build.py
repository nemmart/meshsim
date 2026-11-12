#!/usr/bin/env python3
"""Build v1.35's spec from v1.3's by moving whole lines (stage 2 of the restructuring; MAP.md is the plan).

Run from the repository's root:  python3 sim/spec/check/build.py
Reads v1.3/ only; writes the destinations under sim/spec/ and sim/spec/check/EDITS.md and map.tsv. Source text is
copied by line range, never retyped; an edit changes one source line by exact substring replacement (each old
substring must occur), and EDITS.md lists every edit with the whole old and new line, and every added line.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import section13 as S13

V13, OUT = 'v1.3', 'sim/spec'
SRC = {'ms': 'spec/machine-spec.md', 'ro': 'spec/replayer-opcodes.md', 'sch': 'spec/schedule.md'}
WHOLE = ['spec/CHANGES-to-v1.25.md', 'spec/CHANGES-v1.2.md', 'spec/CHANGES-v1.25.md', 'spec/CHANGES-v1.25-to-v1.26.1.md',
         'spec/CHANGES-v1.26.md', 'spec/CHANGES-v1.26.1.md', 'spec/CHANGES-v1.26.1-to-v1.3.md', 'spec/CHANGES-v1.3.md',
         'planning/DECISIONS-v1.2.md', 'planning/DECISIONS-v1.25.md', 'planning/DECISIONS-v1.26.md',
         'planning/DECISIONS-v1.3.md']


def read(rel):
    with open(os.path.join(V13, rel), encoding='utf-8') as f:
        t = f.read()
    L = t.split('\n')
    if L[-1] == '':
        L.pop()
    return L


LINES = {k: read(v) for k, v in SRC.items()}

# ------------------------------------------------------------------------------------------------- edits
# (src, line) -> list of (old substring, new substring, kind). kind: sim | path | version | heading
E = {}


def edit(src, line, old, new, kind):
    E.setdefault((src, line), []).append((old, new, kind))


P = {'sw/mesh.h': 'fw/include/mesh.h', 'sw/': 'fw/', 'tables/': 'data/tables/',
     'spec/replayer-opcodes.md': 'spec/jolt/replayer-opcodes.md', 'wip/gold/': 'pipeline/gold/',
     'wip/planning/upstream/': 'v1.3/planning/upstream/', 'planning/upstream/': 'v1.3/planning/upstream/',
     'planning/DECISIONS-': 'spec/history/DECISIONS-', 'planning/sessions/': 'v1.3/planning/sessions/',
     'CHANGES-v1.1.md': 'v1.3/CHANGES-v1.1.md', 'CHANGES-v1.2.md': 'spec/history/CHANGES-v1.2.md',
     'CHANGES-v1.25.md': 'spec/history/CHANGES-v1.25.md', 'diagrams/': 'v1.3/diagrams/', 'results/': 'v1.3/results/',
     'RESULTS-full-size.md': 'v1.3/RESULTS-full-size.md', 'sort/': 'fw/sort/',
     'tools/replayer-opcodes/': 'v1.3/tools/replayer-opcodes/',
     'WORKER11/REPORT.md': 'v1.3/planning/sessions/WORKER11-REPORT.md',
     'WORKER20/REPORT.md': 'v1.3/planning/sessions/WORKER20-REPORT.md'}
FULLNAME = {'CHANGES-v1.1.md', 'CHANGES-v1.2.md', 'CHANGES-v1.25.md', 'RESULTS-full-size.md', 'WORKER11/REPORT.md',
            'WORKER20/REPORT.md', 'sw/mesh.h', 'spec/replayer-opcodes.md'}
PATH_LINES = {'ms': [12, 13, 16, 18, 1219, 1287, 1291, 1315, 1345, 1417, 1510, 1514, 1516, 1556, 1558, 1562, 1591, 1605,
                     1614, 1629, 1656, 1669, 1779, 1782, 1786, 1938, 1980, 2027, 2137, 2171, 2175, 2247, 2264, 2341, 2690,
                     2860, 2950, 2961, 3464, 3468, 3479, 3488, 610, 879, 939, 992, 2471, 2476, 2478],
              'ro': [3, 5, 12, 13, 99, 1303]}


def map_token(t):
    for k in sorted(P, key=len, reverse=True):
        if k in FULLNAME:
            if t == k or t.startswith(k + ':'):
                return P[k] + t[len(k):]
        elif t.startswith(k):
            return P[k] + t[len(k):]
    return None


for src, lines in PATH_LINES.items():
    for ln in lines:
        for tok in sorted(set(re.findall(r'`([^`\s]+)`', LINES[src][ln - 1]))):
            if src == 'ms' and ln in (6, 7, 8, 9, 10, 11):
                continue
            new = map_token(tok)
            if new and new != tok:
                edit(src, ln, '`%s`' % tok, '`%s`' % new, 'path')

edit('ms', 1, ', v1.3', ', v1.35', 'version')
edit('ms', 15, '`sw/mesh.h` and `sw/REGISTERS.md`', '`fw/include/mesh.h` and `fw/REGISTERS.md`', 'path')
edit('ms', 15, 'generated from the register tables of section 8. Both must match section 8; **they are still at v1.1** until regenerated.',
     'generated from the register table. Both must match section 8.', 'sim')
edit('ms', 16, ' (being rewritten for v1.2)', '', 'sim')
edit('ms', 17, "- `meshsim/`: the cycle-level simulator, which models every unit described here and is where the measured numbers come from. **meshsim is still at v1.1**; its port to this document follows.",
     "- `src/`: meshsim, the cycle-level simulator, which models every unit described here. v1.35's is in C++, checked against v1.3's, in Java, where the measured numbers come from (Markers, below).", 'sim')
edit('ms', 20, "**Every measurement in this document was taken on meshsim v1.1 or earlier**; one that v1.2's changes may invalidate carries **v1.1 measurement; to be re-measured in v1.2** (or v1.0) in its label, and is kept as the best number known until the simulator is updated.",
     "**Every number marked [meas: meshsim] was measured on meshsim's Java implementation, v1.0 to v1.3**, and a test or driver a label names is a Java one (`spec/CHANGES-v1.3-to-v1.35.md` names what replaces each in v1.35); one labelled *v1.0 measurement; to be re-measured in v1.2* (or *in v1.1*) has not been re-measured since, and is kept as the best number known (item 24).", 'sim')
edit('ms', 28, 'for Tony to confirm.)',
     "for Tony to confirm.) **Where this document says meshsim stops, errors or flags,** v1.35's meshsim does one of three things: **undefined behaviour is fatal** in every build, the run ending with a message naming the access (`src/base/fatal.h`); **the silent traps** (4.13) **stop the core**; and **every other invariant of section 9 is counted** in every build and, in the debug and suite builds, also reported by name while the run goes on (`src/check.h`), so a test fails on a violation it doesn't expect.", 'sim')
edit('ms', 753, 'and S1Bench measures the margin.', "and its S1 measurement checks the margin (`measure_s1`, `tests/mach.h`).", 'sim')
edit('ms', 2439, "**In meshsim v1.2, C&C 1 stays a behavioural Java model** (as `CnC.java` in v1.1), driving its peripheral's registers from the harness; likewise C&C 2 and C&C 3 until their firmware exists.",
     "**In meshsim, C&C 1 is a behavioural model by default** (`src/units/cnc.h`), driving its peripheral's registers from the harness, and so are C&C 2 and C&C 3; with `cc_cores` (`tests/mach.h`) all three are cores, booted through the host port.", 'sim')
edit('ms', 2542, '- **In meshsim v1.2,** C&C 1 is a behavioural model driving these registers from the harness, and so are C&C 2 and C&C 3 until their firmware exists (6.2).',
     '- **In meshsim,** C&C 1 is by default a behavioural model driving these registers from the harness, and so are C&C 2 and C&C 3; as an option all three are cores (6.2).', 'sim')
edit('ms', 2821, " and the replayer's", '', 'heading')
edit('ms', 2942, 'meshsim stops with an error naming the access, unless the invariant says it reports.',
     "v1.35's meshsim checks each in one of three ways (the preamble): undefined behaviour is fatal in every build, the silent traps (23) stop the core, and the rest are counted in every build and reported by name in the debug and suite builds.", 'sim')
edit('ms', 2951, "meshsim's `Config.strictReplies`", "meshsim's `strict_replies` (`tests/mach.h`)", 'sim')
edit('ms', 2957, "meshsim's `Config.checkReadPortOverlap`, on by default", "meshsim's `check_read_port_overlap` (`tests/mach.h`), on by default", 'sim')
edit('ro', 3, 'the machine specification, v1.25, linked from `machine-spec.md` 4.19',
     'the machine specification, v1.35, linked from `spec/jolt/replayer.md` 4.19', 'version')


def edited(src, ln):
    l = LINES[src][ln - 1]
    for old, new, _ in E.get((src, ln), []):
        if l.count(old) < 1:
            sys.exit(f'edit {src}:{ln}: old text not found: {old[:60]!r}')
        l = l.replace(old, new)
    return l


# ------------------------------------------------------------------------------------------------- added text
def sec13_table(unit):
    order = ['open', 'decided, part open', 'proposed', 'decided, part proposed', 'proposed, part deferred', 'unclear',
             'deferred', 'decided, part deferred', 'decided, not yet done', 'later', 'decided, part later']
    rows = sorted(((n, v) for n, v in S13.I.items() if v[1] == unit), key=lambda x: (order.index(x[1][0]), x[0]))
    out = ['| item | what is to be settled | section | status |', '|---|---|---|---|']
    out += [f'| {n} | {v[3][0].upper() + v[3][1:]} | {v[2]} | {v[0]} |' for n, v in rows]
    return out


def where(n):
    if n not in S13.I:
        return 'closed'
    return {'P': 'here', 'R': '`spec/jolt/replayer.md`', 'T': '`spec/jolt/tracer.md`'}[S13.I[n][1]]


NEW13 = ['## 13. Open items', '',
         "What is open, proposed or deferred in v1.35's design, each with the section it affects; possible later additions are marked *later*, and *unclear* marks an item the record doesn't settle, awaiting the owner. The replayer's and the Tracer's items are in `spec/jolt/replayer.md` and `spec/jolt/tracer.md`. Numbers are never reused. Every item's history (its text as first written and each version's change of status) is `spec/history/machine-spec-v1.3-section-13.md`; the decisions are in `spec/history/`. The logic budget (section 11) is unreviewed; v1.2 rescaled it for the 256-bit link word and the 32 KB controller memories (item 127).",
         ''] + sec13_table('P') + ['', "**The index:** where each item lives; *closed* means settled, its record in the history file.", '',
         '; '.join(f'{n} {where(n)}' for n in range(1, 159)) + '.', '']
OPEN = lambda u: ['', "## Open items (section 13's numbering)", '', "Settled items are listed in `spec/machine-spec.md` section 13's index; their record is `spec/history/machine-spec-v1.3-section-13.md`.", ''] + sec13_table(u)


def stub(head, dest, sub):
    return [head, '', f'*Moved in v1.35 to `{dest}`{sub}.*', '']


A = {  # name -> added lines
    'companions': ["- `spec/OVERVIEW.md` and `spec/GUIDELINES.md`: what is being built and where it stands, and the rules the design follows; read them first.",
                   "- `spec/jolt/`: the Jolt-specific units, the replayer (4.19) and the Tracer (4.21), with what belongs only to them and the opcode appendix (`spec/jolt/replayer-opcodes.md`).",
                   "- `spec/CHANGES-v1.3-to-v1.35.md`: v1.35 against v1.3; nothing changes for the RTL.",
                   "- `spec/history/`: what changed in each version (the `CHANGES-*` files, each version's \"in brief\"), the decisions logs behind them, and section 13's full record. Where a log and this document differ, this document is the authority."],
    's419': stub('### 4.19 The replayer (peripheral type 4)', 'spec/jolt/replayer.md', ', with 4.19.1–4.19.15'),
    's421': stub('### 4.21 The Tracer (peripheral type 3)', 'spec/jolt/tracer.md', ', with 4.21.1–4.21.10'),
    's811': ["The replayer's window (peripheral 1 on tiles 0–63): moved in v1.35 to `spec/jolt/replayer.md`.", ''],
    's816': stub("### 8.16 The Tracer's window (C&C 3's peripheral)", 'spec/jolt/tracer.md', ''),
    'inv16': ["    - the replayer's and the Tracer's: in `spec/jolt/replayer.md` and `spec/jolt/tracer.md`."],
    'p10': ['| replayer (4.19), Tracer (4.21) | in `spec/jolt/replayer.md` and `spec/jolt/tracer.md` |'],
    'new13': NEW13,
    'rep_head': ['# The replayer: machine specification, v1.35', '',
                 "*Part of the machine specification (`spec/machine-spec.md`), moved here whole in v1.35 with its section numbers: 4.19, the replayer's part of 8.11, its invariant (section 9, 16) and its parameters (section 10). The opcode appendix is `spec/jolt/replayer-opcodes.md`.*", ''],
    'rep_811': ['', "From 8.11, the peripheral windows:", ''],
    'rep_inv': ['', "From section 9, invariant 16 (the peripherals' and the replayer's invariants):", ''],
    'rep_p10': ['', 'From section 10, the parameter summary:', '', '| parameter | default |', '|---|---|'],
    'rep_open': OPEN('R'),
    'tr_head': ['# The Tracer: machine specification, v1.35', '',
                "*Part of the machine specification (`spec/machine-spec.md`), moved here whole in v1.35 with its section numbers: 4.21, its window (8.16), its invariant (section 9, 16) and its parameters (section 10).*", ''],
    'tr_inv': ['', "From section 9, invariant 16 (the peripherals' and the replayer's invariants):", ''],
    'tr_p10': ['', 'From section 10, the parameter summary:', '', '| parameter | default |', '|---|---|'],
    'tr_open': OPEN('T'),
    'hp_head': ["# v1.3's machine specification: the preamble's history", '',
                "*Moved whole from `v1.3/spec/machine-spec.md` in v1.35: the companion bullets for the history documents (lines 6–11) and each version's \"in brief\" (lines 30–36, 40–70), unchanged; their paths are v1.3's.*", ''],
    'h13_head': ["# v1.3's machine specification: section 13 as it stood", '',
                 "*Moved whole from `v1.3/spec/machine-spec.md` (lines 3094–3458) in v1.35, unchanged; its paths and line references are v1.3's. Today's open items are `spec/machine-spec.md` section 13.*", ''],
}

# ------------------------------------------------------------------------------------------------- layouts
# a destination is a list of ('s', src, first, last) and ('a', name)
D = {
    'machine-spec.md': [('s', 'ms', 1, 5), ('a', 'companions'), ('s', 'ms', 12, 29), ('s', 'ms', 38, 39), ('s', 'ms', 72, 1284),
                        ('a', 's419'), ('s', 'ms', 1957, 1975), ('a', 's421'), ('s', 'ms', 2359, 2841), ('a', 's811'),
                        ('s', 'ms', 2857, 2901), ('a', 's816'), ('s', 'ms', 2937, 2960), ('a', 'inv16'), ('s', 'ms', 2963, 3033),
                        ('a', 'p10'), ('s', 'ms', 3036, 3093), ('a', 'new13'), ('s', 'ms', 3459, 3488)],
    'jolt/replayer.md': [('a', 'rep_head'), ('s', 'ms', 1285, 1956), ('a', 'rep_811'), ('s', 'ms', 2842, 2856), ('a', 'rep_inv'),
                         ('s', 'ms', 2961, 2961), ('a', 'rep_p10'), ('s', 'ms', 3034, 3034), ('a', 'rep_open')],
    'jolt/tracer.md': [('a', 'tr_head'), ('s', 'ms', 1976, 2358), ('s', 'ms', 2902, 2936), ('a', 'tr_inv'), ('s', 'ms', 2962, 2962),
                       ('a', 'tr_p10'), ('s', 'ms', 3035, 3035), ('a', 'tr_open')],
    'jolt/replayer-opcodes.md': [('s', 'ro', 1, len(LINES['ro']))],
    'schedule.md': [('s', 'sch', 1, len(LINES['sch']))],
    'history/machine-spec-v1.3-preamble.md': [('a', 'hp_head'), ('s', 'ms', 6, 11), ('s', 'ms', 30, 37), ('s', 'ms', 40, 71)],
    'history/machine-spec-v1.3-section-13.md': [('a', 'h13_head'), ('s', 'ms', 3094, 3458)],
}

A['changes'] = ['# v1.3 to v1.35', '',
    "*What v1.35 changes against v1.3. The hardware is v1.3's; the simulator is v1.3's rewritten in C++ (`src/`); the spec is v1.3's, restructured.*", '',
    '## For the RTL', '',
    "**Nothing changes.** v1.35's hardware is v1.3's, unit for unit: every normative statement, register, number and parameter of v1.3's spec stands. The guide from v1.26.1 is still `spec/history/CHANGES-v1.26.1-to-v1.3.md`.", '',
    "## Where v1.3's spec went", '',
    "Every section keeps its number; a section that moved leaves a one-line stub under its number in `spec/machine-spec.md`.", '',
    '| v1.3 | v1.35 |', '|---|---|',
    "| `machine-spec.md`, all but what follows | `spec/machine-spec.md`, the platform |",
    "| 4.19, the replayer; 8.11's replayer window; its invariant (9, 16) and parameters (10) | `spec/jolt/replayer.md` |",
    "| 4.21, the Tracer; 8.16, its window; its invariant (9, 16) and parameters (10) | `spec/jolt/tracer.md` |",
    "| `replayer-opcodes.md`, the appendix | `spec/jolt/replayer-opcodes.md` |",
    "| `schedule.md` | `spec/schedule.md` |",
    "| the preamble's \"in brief\" blocks and its companions for the history documents | `spec/history/machine-spec-v1.3-preamble.md` |",
    "| section 13 as it stood | `spec/history/machine-spec-v1.3-section-13.md`; the new section 13 lists what is open today |",
    "| the `CHANGES-*` files; `v1.3/planning/DECISIONS-v1.2.md` to `-v1.3.md` | `spec/history/`, names unchanged |", '',
    "Every statement about the simulator and every path was made true of v1.35; nothing else in the text changed.", '',
    "## The simulator's deliberate differences from v1.3's", '',
    "v1.35's meshsim matches v1.3's results exactly, and its cycle counts exactly except in these cases, each approved and recorded (the decision in brackets):", '',
    "- **A counter store and a decrement in the same cycle** (D27). The traffic director judges a due decrement without the core's counter store of the same cycle, which reaches the register block a cycle later; v1.3's simulator did the same through its fixed step order. No reference program reaches the case.",
    "- **A router FIFO's overflow is counted at t + 1** (D28), with its `ERRORS` bit. v1.3's counted it whenever the FIFO was next touched, a variable number of cycles late. No reference program overflows one.",
    "- **A second READY pulse while the first is still visible** (D29). v1.35 keeps the spec's sticky bit and counts the case (`ready_repulses`); v1.3's read 0 at C&C 1 until the new pulse landed. Likewise an overflow that feeds `ERRORS` reaches the register block as an event, counted at t + 1. No reference program reaches either.",
    "- **C&C 1's peripheral applies a store one cycle later** (D33, D44): at t + 1, as every peripheral slot does, where v1.3's applied it within the store's cycle. C&C 1's own work (the watchdog's first count, a UART byte's start) runs a cycle later; one suite's printed figure differs by one cycle and is masked in its comparison.",
    "- **The request bus's credit latency is a setting** (D34): 0 is exact as v1.3's (every sender in the bus's domain); 3, the default, is the spec's, each port's leaf in its tile. They differ only when a node's 8-beat buffer fills, which is counted; the reference runs never reach it.", '',
    "## Where v1.35's checks differ from the rule", '',
    "The machine specification's preamble gives the rule: undefined behaviour is fatal, the silent traps stop the core, and every other invariant is counted and, in the debug and suite builds, reported. As of 2026-11-12 these cases differ:", '',
    "- **Counted, not fatal:** a packet of size 11 (3.1, undefined); a stream with a misaligned source or destination, or with *d* = 126 from a mesh tile (4.2), which are also not reported.",
    "- **Counted but not reported:** a late rotation (invariant 3) and a `HIST` count past 17 bits (invariant 6). **Reported but not counted** in the release build: uncounted traffic other than mode 0 (invariant 22).",
    "- **Fatal, where the spec says ignored and flagged:** a run started while the replayer runs (4.19.9), and `PULSE_DMA` while a run is running (8.6); invariant 26 and 4.17.2 make both undefined.",
    "- **Not checked:** a buffer both read and written in one run (invariant 2); `DMA_HDR` or `DMA_ADDR` changed mid-transfer (4.17.9); the 1, 3 and 1 requests outstanding, the tag windows and reads during a group run (invariant 17); one source per destination for modes 1–3 into big SRAM (invariant 18); the overtaking read (invariant 24), which only the harness's S1 measurement covers; a Tracer transfer asking for more than its credits cover (4.21).", '',
    "## The drivers the spec's measurements name", '',
    "The `[meas: …]` labels name v1.3's Java tests and drivers (Markers). What replaces each in v1.35:", '',
    '| v1.3 (Java) | v1.35 |', '|---|---|',
    '| NetTest (fan-in, `skewtable`) | `tests/suite_net.cpp` |',
    '| MemTest | `tests/suite_mem.cpp` |',
    '| Stage B | `tests/stageb.cpp` |',
    '| AllToAllTest (`measure`) | not yet ported (2026-11-12) |',
    '| ReadPortGatherTest | not yet ported (2026-11-12) |',
    '| SearchBench | not yet ported (2026-11-12) |',
    '| CondTest | not yet ported (2026-11-12) |',
    '| S1Bench | not yet ported (2026-11-12); `measure_s1` (`tests/mach.h`) is the harness side of its measurement |',
    "| SplitRun | not carried: its driver doesn't build in v1.3 |"]
A['hist_readme'] = ['# spec/history', '',
    "What v1.35's restructuring took out of v1.3's spec, moved whole and unchanged: each version's `CHANGES-*` guide, the four decisions logs the spec cites (`DECISIONS-v1.2.md` to `DECISIONS-v1.3.md`), the preamble's \"in brief\" blocks and history companions (`machine-spec-v1.3-preamble.md`), and section 13 as it stood (`machine-spec-v1.3-section-13.md`).", '',
    "These are history: their paths and line numbers are v1.3's, and nothing here is kept true of v1.35. Where a log and `spec/machine-spec.md` differ, the spec is the authority. Carried text, here and in the spec, keeps its worker names and session codes: the restructuring moves text and doesn't rewrite it.", '',
    "`EDITS.md` lists every line the restructuring changed or added, with the old and new text of each edit."]
A['jolt_readme'] = ['# spec/jolt', '',
    "The Jolt-specific units, plugged into the platform's peripheral slot (4.17): `replayer.md` (4.19, with its part of 8.11, its invariant and parameters), `tracer.md` (4.21, 8.16, its invariant and parameters) and the replayer's opcode appendix, `replayer-opcodes.md`. Section numbers are the machine specification's; `spec/machine-spec.md` keeps a stub under each, and its section 13 indexes the items listed here."]
A['check_readme'] = ['# spec/check', '',
    "The tools that restructured v1.3's spec as v1.35's and prove it a move. They read `v1.3/`, so this folder is left out of the frozen copy; the record of every edit is `spec/history/EDITS.md`.", '',
    '- `MAP.md`: the plan (where each unit went, the new section 13, the edits), reviewed before the move; `map.tsv`, its line ranges.',
    '- `build.py`: builds the destinations from `v1.3/` by line range, applies the edits, writes `map.tsv`, `edits.json` and `spec/history/EDITS.md`.',
    '- `check_move.py`: the proof (run from the repository root): every source line in exactly one destination, unchanged and in order, or an edit; every destination line a source line, an edit or added; every section number a heading; every path present.',
    "- `inventory.py`, `section13.py`: the source's units, and section 13's statuses with their evidence.",
    "- `MECHANISMS.md`: what v1.35's meshsim does for each check the spec promises; `FINDINGS.md`: what looks wrong, for the owner."]
D['CHANGES-v1.3-to-v1.35.md'] = [('a', 'changes')]
D['history/README.md'] = [('a', 'hist_readme')]
D['jolt/README.md'] = [('a', 'jolt_readme')]
D['check/README.md'] = [('a', 'check_readme')]


def write_edits_md(edits, added):
    from collections import Counter
    c = Counter(e[4] for e in edits)
    L = ['# The edits: every line the restructuring changed or added', '',
         f"*Written by `build.py`. {len(edits)} edited lines ({', '.join(f'{k} {v}' for k, v in sorted(c.items()))}) and {len(added)} added lines. Kinds: **sim**, a statement about the simulator made true of v1.35; **path**, a path made true of v1.35's tree; **version**, the two version headers; **heading**, 8.11's heading, which would otherwise be false. Section 13's new text is among the added lines. Line numbers: the destination's (under `sim/spec/`) and the source's.*", '',
         '## Edited lines', '']
    for dest, dl, src, sl, k, old, new in edits:
        L += [f'### `{dest}` {dl} ← `{src}` {sl} ({k})', '', '- old: ' + old, '- new: ' + new, '']
    L += ['## Added lines', '', '| destination | line | text |', '|---|---|---|']
    L += [f"| `{d}` | {n} | {t.replace('|', '\\\\|')} |" for d, n, t in added]
    with open(os.path.join(OUT, 'history/EDITS.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')


def build():
    tsv, edits, added = [], [], []
    for dest, segs in D.items():
        out = []
        for seg in segs:
            if seg[0] == 's':
                _, src, a, b = seg
                tsv.append(f'{V13}/{SRC[src]}\t{a}\t{b}\t{dest}')
                for ln in range(a, b + 1):
                    new = edited(src, ln)
                    if new != LINES[src][ln - 1]:
                        kinds = '+'.join(sorted({k for _, _, k in E[(src, ln)]}))
                        edits.append((dest, len(out) + 1, f'{V13}/{SRC[src]}', ln, kinds, LINES[src][ln - 1], new))
                    out.append(new)
            else:
                for l in A[seg[1]]:
                    out.append(l)
                    if l.strip():
                        added.append((dest, len(out), l))
        os.makedirs(os.path.dirname(os.path.join(OUT, dest)) or OUT, exist_ok=True)
        with open(os.path.join(OUT, dest), 'w', encoding='utf-8') as f:
            f.write('\n'.join(out) + '\n')
    for rel in WHOLE:
        dest = 'history/' + os.path.basename(rel)
        tsv.append(f'{V13}/{rel}\t1\t{len(read(rel))}\t{dest}')
        os.makedirs(os.path.join(OUT, 'history'), exist_ok=True)
        with open(os.path.join(V13, rel), 'rb') as f, open(os.path.join(OUT, dest), 'wb') as g:
            g.write(f.read())
    unused = set(E) - {(s, l) for _, _, s2, l, *_ in edits for s in [k for k, v in SRC.items() if f'{V13}/{v}' == s2]}
    with open(os.path.join(OUT, 'check/map.tsv'), 'w', encoding='utf-8') as f:
        f.write('# source\tfirst\tlast\tdestination (under sim/spec/); machine-readable copy of MAP.md, written by build.py\n')
        f.write('\n'.join(tsv) + '\n')
    return edits, added


if __name__ == '__main__':
    edits, added = build()
    import json
    with open(os.path.join(OUT, 'check/edits.json'), 'w', encoding='utf-8') as f:
        json.dump({'edits': edits, 'added': added}, f, ensure_ascii=False, indent=0)
    write_edits_md(edits, added)
    from collections import Counter
    print('edits', len(edits), dict(Counter(e[4] for e in edits)), 'added lines', len(added))
