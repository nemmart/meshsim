#!/usr/bin/env python3
"""ACT4 (W5a): every architectural-test ELF through gold, jolt-cpp (row_dump) and upstream (upstream_rows), with each
one's verdict and whether their rows agree; upstream's own jolt-emu verdicts come from Quest task 095.

    run_act4.py <elfs dir> <jolt-emu-results.txt> <skip.txt> <out.tsv>
        [--gold=<gold build dir>] [--row-dump=<path>] [--upstream-rows=<path>]

<elfs dir> is the unpacked act4-elfs.tar.xz (task 095). Each test runs with program.json beside this script (jolt-emu's
defaults, common/src/constants.rs at 17a9459). A verdict is the test's tohost word after the run
(tests/arch-tests/jolt/rvmodel_macros.h): 1 PASS; (gp << 1) | 1 FAIL in case gp; 0 none. Gold reads it from memory
(run --tohost); for jolt-cpp and upstream it is the last store to tohost in their rows (WORKER18's record: opcode in
word 1 bits 7:0, address word 7, value written word 9).
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
STORES = {"SD", "VirtualStoreB", "VirtualStoreH", "VirtualStoreW"}


def opcode_names():
    # jolt-cpp's enum order, as gold's Op.java (isa/riscv.inc at 821e1bd)
    src = open(os.path.join(HERE, "..", "..", "src", "gold", "Op.java")).read()
    names = {}
    for line in src.splitlines():
        line = line.strip()
        if line.startswith("public static final int ") and "=" in line and "NUM_OPCODES" not in line:
            k, v = line[len("public static final int "):].rstrip(";").split(" = ")
            if v.isdigit():
                names[int(v)] = k
    return names


OPS = opcode_names()


def tohost_address(elf):
    nm = shutil.which("riscv-none-elf-nm") or "nm"
    out = subprocess.run([nm, elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "tohost":
            return int(parts[0], 16)
    return None


def verdict_from_rows(rows_file, tohost):
    value = 0
    with open(rows_file, "rb") as f:
        while True:
            rec = f.read(80)
            if len(rec) < 80:
                break
            w = struct.unpack("<10Q", rec)
            if OPS.get(w[1] & 0xff) in STORES and w[7] == tohost & ~7:
                value = w[9]
    return value


def describe(v):
    return "PASS" if v == 1 else "NONE" if v == 0 else f"FAIL({v >> 1})"


def run(cmd, timeout=600):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return -1, "timeout"


def first_error(text):
    for line in text.splitlines():
        if "ERROR" in line or "panicked" in line or "ABORT" in line or "Exception" in line:
            return line.strip()[:160]
    return text.strip().splitlines()[-1][:160] if text.strip() else ""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--"))
    elfs_dir, emu_results, skip_file, out = args
    gold = opts.get("gold", os.path.join(HERE, "..", "..", "build"))
    row_dump = opts.get("row-dump", "row_dump")
    upstream = opts.get("upstream-rows", "upstream_rows")
    emu = dict(line.split() for line in open(emu_results) if line.strip())
    skipped = {l.strip() for l in open(skip_file) if l.strip() and not l.startswith("#")}
    elfs = sorted(os.path.join(r, f) for r, _, fs in os.walk(elfs_dir) for f in fs
                  if f.endswith(".elf") and not f.endswith(".sig.elf"))
    empty = tempfile.mkdtemp()
    rows = []
    for elf in elfs:
        name = os.path.basename(elf)[:-4]
        d = tempfile.mkdtemp()
        shutil.copy(elf, os.path.join(d, "program.elf"))
        shutil.copy(os.path.join(HERE, "program.json"), d)
        th = tohost_address(elf)
        rc, text = run(["java", "-cp", gold, "gold.Main", "run", d, empty, "--tohost"])
        g_hash = text.split("run hash ")[1].split(",")[0] if "run hash " in text else "-"
        g = describe(int(text.split("tohost ")[1].split(":")[0], 16)) if "tohost 0x" in text else "ABORT"
        g_note = "" if "run hash" in text else first_error(text)
        res = {}
        for tool, exe in (("cpp", row_dump), ("up", upstream)):
            prefix = os.path.join(d, tool)
            rc, text = run([exe, d, empty, prefix, "--rows"])
            if rc == 0 and os.path.exists(prefix + ".json"):
                j = json.load(open(prefix + ".json"))
                res[tool] = (describe(verdict_from_rows(prefix + ".rows.bin", th)), j["run_hash"], j["rows"], "")
            else:
                res[tool] = ("ABORT", "-", "-", first_error(text))
        same = "yes" if g_hash != "-" and g_hash == res["cpp"][1] == res["up"][1] else \
               ("gold=cpp" if g_hash != "-" and g_hash == res["cpp"][1] else "no")
        rows.append([name, "skip" if name in skipped else "run", emu.get(name, "?"), g, res["cpp"][0], res["up"][0],
                     str(res["cpp"][2]), same, g_note or res["cpp"][3] or res["up"][3]])
        print("\t".join(rows[-1]), flush=True)
        shutil.rmtree(d)
    with open(out, "w") as f:
        f.write("test\tupstream_skip\tjolt_emu_exit\tgold\tjolt_cpp\tupstream_trace\trows\trows_agree\tnote\n")
        for r in rows:
            f.write("\t".join(r) + "\n")


if __name__ == "__main__":
    main()
