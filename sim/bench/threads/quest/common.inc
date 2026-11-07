# Shared by W4's task bodies. run TAG CPUSET cmd...: one run under taskset (and perf stat when PERF=1); appends
# "TAG <bench line> [perf: ...]" to $RES/raw.txt.
run() {
  local tag=$1 cpus=$2; shift 2
  if [ "${PERF:-0}" = 1 ]; then
    taskset -c "$cpus" perf stat -x, -e cycles,instructions,cache-misses -o "$W/perf.csv" -- "$@" > "$W/out.txt" 2> "$W/err.txt"
  else
    taskset -c "$cpus" "$@" > "$W/out.txt" 2> "$W/err.txt"
  fi
  local rc=$?
  local line; line=$(grep -E '^threads ' "$W/out.txt" | tail -1)
  if [ $rc -ne 0 ] || [ -z "$line" ]; then echo "RUNFAIL $tag rc=$rc: $*"; head -5 "$W/err.txt"; FAILS=$((FAILS + 1)); return 1; fi
  local pc=""
  [ "${PERF:-0}" = 1 ] && pc=" perf: $(awk -F, '$3 ~ /cycles/ && $3 !~ /ref/ { split($3,a,"/"); k=(a[1]=="cpu_atom")?"atom":"core"; c[k]+=$1 }
      $3 ~ /instructions/ { split($3,a,"/"); k=(a[1]=="cpu_atom")?"atom":"core"; i[k]+=$1 }
      $3 ~ /cache-misses/ { split($3,a,"/"); k=(a[1]=="cpu_atom")?"atom":"core"; m[k]+=$1 }
      END { printf "core_cycles=%s core_instr=%s core_cmiss=%s atom_cycles=%s atom_instr=%s", c["core"], i["core"], m["core"], c["atom"], i["atom"] }' "$W/perf.csv")"
  echo "$tag $line$pc" | tee -a "$RES/raw.txt"
}
field() { grep -o "$1=[^ ]*" | cut -d= -f2; }
