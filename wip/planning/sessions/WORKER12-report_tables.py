#!/usr/bin/env python3
"""Markdown tables for REPORT.md from data/ (count_report.py's output). Usage: python3 report_tables.py > tables.md"""
import csv, json, os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
L = lambda f: list(csv.DictReader(open(os.path.join(D, f))))
J = lambda f: json.load(open(os.path.join(D, f)))
RUNS = [('cyclone-29', 'Cyclone 2^29'), ('evm-29', 'EVM Zone 2^29'), ('cyclone-22', 'Cyclone 22'), ('evm-23', 'EVM Zone trace-23')]
n = lambda x: f'{int(x):,}'

print('### 1-2. Source instructions: executions, rows per execution (from the tables), 2^29 runs\n')
c = L('cyclone-29-opcodes.csv'); e = {r['opcode']: r for r in L('evm-29-opcodes.csv')}
print('| opcode | class | traced/exec | untraced/exec | Cyclone execs | Cyclone untraced | EVM Zone execs | EVM Zone untraced |')
print('|---|---|---|---|---|---|---|---|')
for r in c:
    x = e[r['opcode']]
    print(f"| {r['opcode']} | {r['class']} | {r['traced_per_exec']} | {r['untraced_per_exec']} | {n(r['executions'])} | {n(r['untraced_rows'])} | {n(x['executions'])} | {n(x['untraced_rows'])} |")

print('\n### 1. CSR instructions by CSR and register pattern (executed keys), Cyclone 2^29 (EVM Zone executes none)\n')
print('| key | instruction | traced/exec | executions |'); print('|---|---|---|---|')
for r in L('cyclone-29-templates.csv'):
    if r['class'] == 'csr' and int(r['executions']): print(f"| {r['decode_index_or_key']} | {r['label']} | {r['traced_per_exec']} | {n(r['executions'])} |")

print('\n### 3. Totals\n')
print('| run | traced rows | source executions | untraced rows | untraced / traced | untraced share of all rows |'); print('|---|---|---|---|---|---|')
for k, nm in RUNS:
    s = J(f'{k}-summary.json')
    print(f"| {nm} | {n(s['traced_rows'])} | {n(s['executions'])} | {n(s['untraced_rows'])} | {100 * s['untraced_per_traced']:.3f}% | {100 * s['untraced_share_of_all']:.3f}% |")
for k, nm in RUNS[:2]:
    s = J(f'{k}-summary.json')
    print(f'\nTop untraced contributors, {nm}:\n\n| opcode | executions | untraced rows | share |\n|---|---|---|---|')
    for t in s['top_untraced_by_opcode']: print(f"| {t['opcode']} | {n(t['executions'])} | {n(t['untraced_rows'])} | {100 * t['share']:.2f}% |")

print('\n### 4. Untraced rows per window of 2^16 traced rows\n')
print('| run | windows | with untraced | mean | p50 | p90 | p99 | p99.9 | max | max / 2^16 |'); print('|---|---|---|---|---|---|---|---|---|---|')
for k, nm in RUNS:
    s = J(f'{k}-summary.json'); u = s['untraced_per_window']
    print(f"| {nm} | {n(s['windows'])} | {n(s['windows_with_untraced'])} | {u['mean']:,.1f} | {n(u['p50'])} | {n(u['p90'])} | {n(u['p99'])} | {n(u['p999'])} | {n(u['max'])} | {u['max'] / 65536:,.2f} |")
for k, nm in RUNS[:2]:
    s = J(f'{k}-summary.json')
    print(f'\nHeaviest windows, {nm}:\n\n| window | first row | untraced rows | DIVQ | DIVR | inline execs |\n|---|---|---|---|---|---|')
    for t in s['top_windows'][:6]: print(f"| {t['window']} | {n(t['first_row'])} | {n(t['untraced_rows'])} | {t['divq']} | {t['divr']} | {t['inline_execs']} |")

print('\n### 5. The controller-fed set, 2^29 runs\n')
for k, nm in RUNS[:2]:
    s = J(f'{k}-summary.json'); cf = s['controller_fed']
    print(f"{nm}: {n(cf['inline_execs'])} inline executions ({n(cf['inline_traced_rows'])} traced rows), {n(cf['csr_execs'])} CSR executions "
          f"({n(cf['csr_traced_rows'])} traced rows, in {n(cf['windows_with_csr'])} windows); per window at most {cf['max_inline_per_window']} inlines, {cf['max_csr_per_window']} CSR.\n")
    print('| template | label | executions | windows with | mean / window | max / window (window) | traced rows | untraced rows |'); print('|---|---|---|---|---|---|---|---|')
    for r in L(f'{k}-fed.csv'):
        if int(r['executions']):
            print(f"| {r['template']} | {r['label']} | {n(r['executions'])} | {n(r['windows_with'])} | {r['mean_per_window']} | {r['max_per_window']} ({r['max_window']}) | {n(r['traced_rows'])} | {n(r['untraced_rows'])} |")
    print()

print('### 6. The inverse: Fermat as built against addition chains (untraced rows per execution)\n')
inv = L('inverse.csv')
print('| inline | variant | squarings | multiplies | rows / step | prologue | epilogue | untraced rows | vs Fermat |'); print('|---|---|---|---|---|---|---|---|---|')
for r in inv: print(f"| {r['inline']} | {r['variant']} | {r['squarings']} | {r['multiplies']} | {r['step_rows']} | {r['prologue']} | {r['epilogue']} | {n(r['untraced_rows'])} | {float(r['vs_fermat']):.3f} |")
best = {}
for r in inv:
    if not r['variant'].startswith('Fermat'): best[r['inline']] = min(best.get(r['inline'], 1e18), int(r['untraced_rows']))
fer = {r['inline']: int(r['untraced_rows']) for r in inv if r['variant'].startswith('Fermat')}
print('\nPer run, with the best chain above for each inline (other untraced rows unchanged):\n')
print('| run | DIVQ | DIVR | untraced, Fermat | untraced, chain | untraced / traced, Fermat -> chain | max window, Fermat -> chain |'); print('|---|---|---|---|---|---|---|')
for k, nm in RUNS:
    s = J(f'{k}-summary.json'); q, r_ = s['divq_execs'], s['divr_execs']
    delta = q * (fer['DIVQ'] - best['DIVQ']) + r_ * (fer['DIVR'] - best['DIVR'])
    wmax = max(int(w['untraced_rows']) - int(w['divq']) * (fer['DIVQ'] - best['DIVQ']) - int(w['divr']) * (fer['DIVR'] - best['DIVR']) for w in L(f'{k}-windows.csv'))
    print(f"| {nm} | {q} | {r_} | {n(s['untraced_rows'])} | {n(s['untraced_rows'] - delta)} | {100 * s['untraced_per_traced']:.2f}% -> {100 * (s['untraced_rows'] - delta) / s['traced_rows']:.2f}% | {n(s['untraced_per_window']['max'])} -> {n(wmax)} |")
