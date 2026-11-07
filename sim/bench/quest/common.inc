# Shared by W6's task bodies (sourced). Needs W (scratch), RES (results dir), K (kit's sim/bench dir).
# run TAG CPUSET cmd...: one run under taskset and perf stat; appends "TAG <bench line> perf: ..." to $RES/raw.txt.
EV=${EV:-cycles,instructions,cache-misses,L1-dcache-load-misses}
run() {
  local tag=$1 cpus=$2; shift 2
  taskset -c "$cpus" perf stat -x, -e $EV -o "$W/perf.csv" -- "$@" > "$W/out.txt" 2> "$W/err.txt"
  local rc=$?
  local line; line=$(grep -E '^(mesh|fp128) ' "$W/out.txt" | tail -1)
  if [ $rc -ne 0 ] || [ -z "$line" ]; then
    echo "RUNFAIL $tag rc=$rc: $*"; head -5 "$W/err.txt"; FAILS=$((FAILS + 1)); return 1
  fi
  local pc; pc=$(awk -F, '
    $3 ~ /cycles/ && $3 !~ /ref/ { split($3, a, "/"); k = (a[1] == "cpu_atom") ? "atom" : "core"; c[k] = $1 }
    $3 ~ /instructions/ { split($3, a, "/"); k = (a[1] == "cpu_atom") ? "atom" : "core"; i[k] = $1 }
    $3 ~ /cache-misses/ { split($3, a, "/"); k = (a[1] == "cpu_atom") ? "atom" : "core"; m[k] = $1 }
    $3 ~ /L1-dcache-load-misses/ { split($3, a, "/"); k = (a[1] == "cpu_atom") ? "atom" : "core"; l[k] = $1 }
    $3 ~ /LLC-loads/ { split($3, a, "/"); k = (a[1] == "cpu_atom") ? "atom" : "core"; ll[k] = $1 }
    $3 ~ /LLC-load-misses/ { split($3, a, "/"); k = (a[1] == "cpu_atom") ? "atom" : "core"; lm[k] = $1 }
    END { printf "core_cycles=%s core_instr=%s core_cmiss=%s core_l1miss=%s atom_cycles=%s atom_instr=%s atom_cmiss=%s",
          c["core"], i["core"], m["core"], l["core"], c["atom"], i["atom"], m["atom"]
          if (("core" in ll) || ("core" in lm)) printf " core_llc_loads=%s core_llc_misses=%s", ll["core"], lm["core"] }' "$W/perf.csv")
  echo "$tag $line perf: $pc" | tee -a "$RES/raw.txt"
}
# field NAME < lines: the value of NAME=...
field() { grep -o "$1=[^ ]*" | cut -d= -f2; }
