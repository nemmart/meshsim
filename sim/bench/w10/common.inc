# Shared by W10's task bodies. run TAG CPUSET cmd...: one run under taskset (and perf stat when PERF=1: cycles,
# instructions, L1d load misses, LLC load misses, summed per core type); appends "TAG <bench line> [perf: ...]" to
# $RES/raw.txt. field NAME: a key=value field of stdin's lines.
run() {
  local tag=$1 cpus=$2; shift 2
  command -v perf > /dev/null || PERF=0
  if [ "${PERF:-0}" = 1 ]; then
    taskset -c "$cpus" perf stat -x, -e cycles,instructions,L1-dcache-load-misses,LLC-load-misses -o "$W/perf.csv" -- "$@" \
      > "$W/out.txt" 2> "$W/err.txt"
  else
    taskset -c "$cpus" "$@" > "$W/out.txt" 2> "$W/err.txt"
  fi
  local rc=$?
  local line; line=$(grep -E '^(threads|chip) ' "$W/out.txt" | tail -1)
  if [ $rc -ne 0 ] || [ -z "$line" ]; then echo "RUNFAIL $tag rc=$rc: $*"; head -5 "$W/err.txt"; FAILS=$((FAILS + 1)); return 1; fi
  local pc=""
  [ "${PERF:-0}" = 1 ] && pc=" perf: $(awk -F, '$1 ~ /^[0-9]+$/ { n=split($3,a,"/"); ev=(n>1)?a[2]:a[1]; k=(a[1]=="cpu_atom")?"atom":"core";
      v[k "_" ev]+=$1 } END { for (x in v) printf "%s=%s ", x, v[x] }' "$W/perf.csv")"
  echo "$tag $line$pc" | tee -a "$RES/raw.txt"
}
field() { grep -o "$1=[^ ]*" | cut -d= -f2; }
# med TAG FIELD: the median of FIELD over raw.txt's lines starting "TAG "
med() { grep "^$1 " "$RES/raw.txt" | field "$2" | sort -g | awk '{ a[NR]=$1 } END { if (NR) print a[int((NR+1)/2)]; else print "-" }'; }
