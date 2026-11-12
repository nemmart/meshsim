#!/bin/bash
# oracle/acceptance_crosscheck.sh [--calibration] (WORKER25, P7b; inventory E5): v1.35's acceptance kit against v1.3's,
# on the same inputs. Needs sim/ref, a JDK and python3 (v1.3's run_shard.sh summarises in Python); run from a committed
# sim/ (the v1.35 kit is HEAD's: tests/acceptance/make_kit.sh).
#   v1.3's kit: built in scratch as v1.3's make_kit.sh builds it (ref's src and the replay tests in a jar; the replay
#     controllers, the committed images, byte-identical to v1.3's build; the tables; v1.3's run_shard.sh and
#     calibrate.sh unchanged), with SyntheticDelivery patched as D43 says (oracle/synthetic_crosscheck.sh's patch).
#   v1.35's kit: made and built (clang) in scratch.
# Each case runs both run_shard.sh on the same delivery and compares: results.csv with the wall-time column masked,
# VERDICT.txt, SUMMARY.txt's non-timing lines (the wall time, the per-stage rates and the throughput masked; owner), the
# exit codes, and every jvm-K.log. The deliveries: synthetic_delivery's (seed 11; equal to v1.3 patched as D43, W19),
# a planted-fault copy (rows, end state, the manifest's rows; hashes refreshed) and a corrupted copy (CORRUPT kinds).
# --calibration: also both calibrate.sh (128 windows of 65,536 rows: the runner), each run's directory compared.
# --real NAME DELIVERY PROGRAM 'RUN_SHARD_ARGS' (repeatable): also both run_shard.sh on a real (v1.26) delivery, e.g.
#   --real evm-sel /d/sel evm-zone-29 '-f 0 -n all -j 16 -s AB -x 1'
# --small-only / --no-small: the synthetic cases only, or none (a runner task that only wants the real and calibration).
# KEEP=DIR: every case's two output directories copied there before the scratch goes (a Quest task's results).
# KIT135=kit.tgz: v1.35's kit as made elsewhere (tests/acceptance/make_kit.sh), for a tree without git (a Quest task).
# Stages A, B and F, and -q 512 (the 8 KB-log control: the log8k controller, 512-entry log queues).
set -u
CAL=0 SMALL=1; REAL=()
while [ $# -gt 0 ]; do
  case "$1" in
    --calibration) CAL=1;;
    --no-small) SMALL=0;;
    --real) REAL+=("$2|$3|$4|$5"); shift 4;;
    *) echo "usage: acceptance_crosscheck.sh [--calibration] [--no-small] [--real NAME DELIVERY PROGRAM ARGS]..."; exit 2;;
  esac; shift
done
SIM=$(cd "$(dirname "$0")/.." && pwd); M=$SIM/ref/meshsim
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
bad=0
# ---- v1.3's kit
mkdir -p "$T/v13/classes" "$T/v13/tables/replayer" "$T/v13/replay" "$T/v13/replay-8k" "$T/v13/sw" "$T/pk"
f=$T/pk/SyntheticDelivery.java; cp "$M/test/SyntheticDelivery.java" "$f"
sed -i 's/^    final SplittableRandom rnd = new SplittableRandom();$/    SplittableRandom rnd;   \/\/ D42: seeded in run()/' "$f"
sed -i "s/^        gen = new SplittableRandom(seed);\$/        gen = new SplittableRandom(seed); rnd = new SplittableRandom(seed ^ 0xD42L);   \/\/ D42/" "$f"
sed -i 's/Map\.of(/lm(/g' "$f"; sed -i '$d' "$f"; cat >> "$f" <<'JAVA'
    /** D42: the pairs in their source order (v1.3: an unordered map). */
    static Map<String, Object> lm(Object... kv) { Map<String, Object> m = new LinkedHashMap<>(); for (int i = 0; i < kv.length; i += 2) m.put((String) kv[i], kv[i + 1]); return m; }
}
JAVA
[ "$(grep -c '// D42' "$f")" = 2 ] || { echo "acceptance_crosscheck: D43's patch didn't apply"; exit 2; }
javac --release 17 -nowarn -d "$T/v13/classes" "$M"/src/*.java 2>&1 | grep -v '^Note'
javac --release 17 -nowarn -d "$T/v13/classes" -cp "$T/v13/classes" "$M"/test/T.java "$M"/test/Replay*.java "$M"/test/StageB.java "$f" "$M"/test/ConvertDelivery.java 2>&1 | grep -v '^Note'
(cd "$T/v13/classes" && jar cf ../meshsim.jar .); rm -rf "$T/v13/classes"
cp "$M/hops.txt" "$T/v13/"; cp "$M/sw/bootrom.hex" "$T/v13/sw/"
cp "$SIM"/tests/m3-images/replay_ctl.{code.bin,data.bin,sym} "$T/v13/replay/"; cp "$SIM"/fw/images/log8k/replay_ctl.{code.bin,data.bin,sym} "$T/v13/replay-8k/"
cp "$SIM/data/tables/manifest.json" "$T/v13/tables/"; cp "$SIM"/data/tables/replayer/{decode_table,rom,templates,template_index}.bin "$T/v13/tables/replayer/"
cp "$M"/acceptance/run_shard.sh "$M"/acceptance/calibrate.sh "$T/v13/"
# ---- v1.35's kit
if [ -n "${KIT135:-}" ]; then cp "$KIT135" "$T/kit.tgz"; else "$SIM/tests/acceptance/make_kit.sh" "$T/kit.tgz" > /dev/null; fi
tar -xzf "$T/kit.tgz" -C "$T" && mv "$T/w25-kit" "$T/v135" \
  && "$T/v135/build.sh" > "$T/build.log" 2>&1 || { cat "$T/build.log"; echo "acceptance_crosscheck: v1.35's kit did not build"; exit 2; }
# ---- the deliveries
SD=$T/v135/sim/build/clang-suite/synthetic_delivery; TB=$SIM/data/tables/replayer
"$SD" "$T/d" --program syn --windows 9 --rows 4000 --seed 11 --tables "$TB" > /dev/null
cp -r "$T/d" "$T/df"; cp -r "$T/d" "$T/dc"
python3 - "$T" <<'PY'
import json, hashlib, os, sys
t = sys.argv[1]
def flip(p, off): b = bytearray(open(p, 'rb').read()); b[off] ^= 0x5a; open(p, 'wb').write(b)
def refresh(d):
    m = json.load(open(d + '/manifest.json'))
    for p in m['programs']:
        for w in p['window_entries']:
            for f, e in w.get('files', {}).items():
                fp = d + '/' + p['name'] + '/' + f
                if os.path.exists(fp): e['sha256'] = hashlib.sha256(open(fp, 'rb').read()).hexdigest()
    json.dump(m, open(d + '/manifest.json', 'w'), indent=1)
d = t + '/df'; p = d + '/syn/'                       # planted faults, hashes refreshed: FAIL lines with details
flip(p + 'window_00001.rows.bin', 80 * 100 + 19)
flip(p + 'window_00002.end.bin', 32 + 8 * 7)
b = open(p + 'window_00003.rows.bin', 'rb').read(); open(p + 'window_00003.rows.bin', 'wb').write(b[:-80])
refresh(d)
m = json.load(open(d + '/manifest.json'))
for w in m['programs'][0]['window_entries']:
    if w['index'] == 5: w['rows'] += 10
    if w['index'] == 6: w['rows'] -= 10
json.dump(m, open(d + '/manifest.json', 'w'), indent=1)
d = t + '/dc'; p = d + '/syn/'                       # corruptions: CORRUPT lines
m = json.load(open(d + '/manifest.json')); we = m['programs'][0]['window_entries']
os.remove(p + 'window_00001.rlog.bin'); del we[2]['files']['window_00002.wlog.bin']; we[3]['rows_sha256'] = '0' * 64
flip(p + 'window_00004.code.bin', 3)
json.dump(m, open(d + '/manifest.json', 'w'), indent=1)
PY
# ---- the comparison
mask_csv() { awk -F, 'BEGIN{OFS=","} {if (NF>=9) $8="W"; print}' "$1"; }
mask_sum() { sed -E 's/wall [0-9]+ s/wall X s/; s/^(stage [A-Z]: [0-9]+ windows OK, [0-9]+ rows), .*/\1/; /^throughput/d' "$1"; }
cmp_dirs() {   # cmp_dirs NAME A B
  local n=$1 a=$2 b=$3 ok=1
  cmp -s <(mask_csv "$a/results.csv") <(mask_csv "$b/results.csv") || { echo "  $n: results.csv differs"; diff <(mask_csv "$a/results.csv") <(mask_csv "$b/results.csv") | head -6; ok=0; }
  cmp -s "$a/VERDICT.txt" "$b/VERDICT.txt" || { echo "  $n: VERDICT.txt differs"; diff "$a/VERDICT.txt" "$b/VERDICT.txt" | head -6; ok=0; }
  cmp -s <(mask_sum "$a/SUMMARY.txt") <(mask_sum "$b/SUMMARY.txt") || { echo "  $n: SUMMARY.txt differs (non-timing lines)"; diff <(mask_sum "$a/SUMMARY.txt") <(mask_sum "$b/SUMMARY.txt") | head -6; ok=0; }
  for g in "$a"/jvm-*.log.gz; do cmp -s <(zcat "$g") <(zcat "$b/$(basename "$g")") || { echo "  $n: $(basename "$g") differs"; diff <(zcat "$g") <(zcat "$b/$(basename "$g")") | head -4; ok=0; }; done
  [ "$(ls "$a" | grep -c jvm)" = "$(ls "$b" | grep -c jvm)" ] || { echo "  $n: jvm logs differ in number"; ok=0; }
  return $((1 - ok))
}
run_case() {   # run_case NAME DELIVERY run_shard-args...
  local n=$1 d=$2; shift 2
  (cd "$T/v13" && ./run_shard.sh -d "$d" "$@" "$T/o13/$n") > "$T/o13-$n.out" 2>&1; local r13=$?
  (cd "$T/v135" && ./run_shard.sh -d "$d" "$@" "$T/o135/$n") > "$T/o135-$n.out" 2>&1; local r135=$?
  if cmp_dirs "$n" "$T/o13/$n" "$T/o135/$n" && [ $r13 = $r135 ]; then echo "SAME   $n (exit $r13, $(wc -l < "$T/o135/$n/results.csv") lines; $(tail -1 "$T/o135/$n/VERDICT.txt"))"
  else echo "DIFFER $n (exit $r13 v1.3, $r135 v1.35)"; bad=1; fi
}
MS=$(sha256sum "$T/d/manifest.json" | cut -d' ' -f1)
if [ $SMALL = 1 ]; then
run_case a-j1        "$T/d"  -p syn -f 0 -n 9 -j 1 -s A -m "$MS"
run_case a-j3        "$T/d"  -p syn -f 0 -n 9 -j 3 -s A
run_case a-range     "$T/d"  -p syn -f 2 -n 5 -j 2 -s A
run_case a-all       "$T/d"  -p syn -f 0 -n all -j 4 -s A
run_case a-past-end  "$T/d"  -p syn -f 5 -n 8 -j 2 -s A
run_case a-faults    "$T/df" -p syn -f 0 -n 9 -j 3 -s A
run_case a-corrupt   "$T/dc" -p syn -f 0 -n 9 -j 2 -s A
run_case ab-j1       "$T/d"  -p syn -f 0 -n 9 -j 1 -s AB -m "$MS"
run_case ab-j3       "$T/d"  -p syn -f 0 -n 9 -j 3 -s AB
run_case abf         "$T/d"  -p syn -f 0 -n 4 -j 2 -s ABF
run_case ab-x3       "$T/d"  -p syn -f 0 -n 9 -j 2 -s AB -x 3
run_case b-all       "$T/d"  -p syn -f 0 -n all -j 3 -s B
run_case ab-faults   "$T/df" -p syn -f 0 -n 9 -j 3 -s AB
run_case ab-corrupt  "$T/dc" -p syn -f 0 -n 9 -j 2 -s AB
run_case abf-q512    "$T/d"  -p syn -f 0 -n 9 -j 3 -s AB -x 4 -q 512
fi
for r in "${REAL[@]}"; do
  IFS='|' read -r n d p a <<< "$r"
  mf=$d/$p.manifest.json; [ -f "$mf" ] || mf=$d/manifest.json
  # shellcheck disable=SC2086
  run_case "$n" "$d" -p "$p" -M "$mf" $a
done
if [ $CAL = 1 ]; then
  (cd "$T/v13" && ./calibrate.sh "$T/c13" "$T") > "$T/c13.out" 2>&1
  (cd "$T/v135" && ./calibrate.sh "$T/c135" "$T") > "$T/c135.out" 2>&1
  for j in 1 4 8 16; do cmp_dirs "calibration j$j" "$T/c13/j$j" "$T/c135/j$j" && echo "SAME   calibration j$j ($(tail -1 "$T/c135/j$j/VERDICT.txt"))" || { echo "DIFFER calibration j$j"; bad=1; }; done
fi
if [ -n "${KEEP:-}" ]; then mkdir -p "$KEEP"; for x in o13 o135 c13 c135; do [ -d "$T/$x" ] && cp -r "$T/$x" "$KEEP/"; done; cp "$T"/*.out "$KEEP/" 2>/dev/null; fi
[ $bad = 0 ] && echo "acceptance_crosscheck: v1.35's kit equals v1.3's on every case" || echo "acceptance_crosscheck: DIFFERENCES"
exit $bad
