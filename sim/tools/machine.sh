#!/bin/bash
# machine.sh: what this machine is, and which CPUs to use (D21: nothing machine-specific is baked in).
# Replaces the machine.inc. Works on hybrid chips (Intel P/E cores: /sys/devices/cpu_core, cpu_atom) and on
# ordinary ones (every core a "P" core), one socket or several; only CPUs this process may run on are used.
#
# Run it:
#   machine.sh record [FILE]   the machine: CPU model, sockets, cores and threads, core types with CPUs and clocks,
#                              caches, memory, kernel, governor, compilers, Java; printed, and written to FILE if given
#   machine.sh cpus KIND [N]   a CPU list (comma-separated), KIND one of:
#                                p      every P-core CPU (hyperthreads included)
#                                p1     one CPU per P core (the first sibling of each)
#                                e      every E-core CPU (empty on a non-hybrid chip)
#                                all    every CPU this process may use
#                                place  the default placement order: p1, then the P cores' other siblings, then e, then
#                                       the rest (the framework's own default, src/topology.cpp, is the same)
#                              with N, only the first N
# Or source it (a Quest task): `. machine.sh [P_LIST [E_LIST]]` sets ALL_CPUS, P_CPUS, P_ONE, E_CPUS, PLACE and defines
# expand, take, count and machine_record, as machine.inc did. Overrides, in order: the arguments, then the environment
# (SIM_P_CPUS, SIM_E_CPUS, SIM_ALLOWED_CPUS; SIM_SYSFS: another /sys, for tests), then detection.

expand() { local out= p a b i; IFS=, read -ra parts <<< "$1"; for p in "${parts[@]}"; do
  if [[ $p == *-* ]]; then a=${p%-*}; b=${p#*-}; for ((i=a; i<=b; i++)); do out+=${out:+,}$i; done; elif [ -n "$p" ]; then out+=${out:+,}$p; fi; done; echo "$out"; }
count() { [ -z "$1" ] && echo 0 || echo "$1" | tr ',' '\n' | wc -l; }
# take N LIST: the first N of LIST
take() { echo "$2" | tr ',' '\n' | head -n "$1" | paste -sd, -; }
# keep LIST ALLOWED: the members of LIST that are in ALLOWED, in LIST's order
keep() { local out= c; for c in ${1//,/ }; do [[ ",$2," == *",$c,"* ]] && out+=${out:+,}$c; done; echo "$out"; }
khz() { cat $SYS/devices/system/cpu/cpu$1/cpufreq/cpuinfo_$2_freq 2>/dev/null || echo "?"; }

# positional overrides only when sourced (run as a command, the arguments are the command's)
if [ "${BASH_SOURCE[0]}" = "$0" ]; then _p_arg=; _e_arg=; else _p_arg=${1:-}; _e_arg=${2:-}; fi
SYS=${SIM_SYSFS:-/sys}   # another root, for tests (tools/fake_sysfs.sh)
_online=$(expand "$(cat $SYS/devices/system/cpu/online)")
_allowed=${SIM_ALLOWED_CPUS:-$(sed -n 's/^Cpus_allowed_list:\s*//p' /proc/self/status)}
ALL_CPUS=$(keep "$_online" "$(expand "$_allowed")")
if [ -n "$_p_arg" ]; then P_CPUS=$(expand "$_p_arg"); elif [ -n "${SIM_P_CPUS:-}" ]; then P_CPUS=$(expand "$SIM_P_CPUS")
elif [ -r $SYS/devices/cpu_core/cpus ]; then P_CPUS=$(keep "$(expand "$(cat $SYS/devices/cpu_core/cpus)")" "$ALL_CPUS")
else P_CPUS=$ALL_CPUS; fi
if [ -n "$_e_arg" ]; then E_CPUS=$(expand "$_e_arg"); elif [ -n "${SIM_E_CPUS:-}" ]; then E_CPUS=$(expand "$SIM_E_CPUS")
elif [ -r $SYS/devices/cpu_atom/cpus ]; then E_CPUS=$(keep "$(expand "$(cat $SYS/devices/cpu_atom/cpus)")" "$ALL_CPUS")
else E_CPUS=; fi
# one per P core: the first of its siblings that we may use
P_ONE=; _seen=,
for c in ${P_CPUS//,/ }; do
  sib=$(expand "$(cat $SYS/devices/system/cpu/cpu$c/topology/thread_siblings_list 2>/dev/null || echo $c)")
  first=$(keep "$sib" "$P_CPUS" | cut -d, -f1)
  [[ $_seen == *",$first,"* ]] && continue
  _seen+="$first,"; P_ONE+=${P_ONE:+,}$first
done
PLACE=$P_ONE
for c in ${P_CPUS//,/ } ${E_CPUS//,/ } ${ALL_CPUS//,/ }; do [[ ",$PLACE," == *",$c,"* ]] || PLACE+=${PLACE:+,}$c; done

machine_record() {   # [FILE]: print the record, and write it to FILE if given
  local _rec; _rec=$(mktemp)
  { echo "model:     $(lscpu | sed -n 's/^Model name: *//p' | head -1)"
    echo "sockets:   $(lscpu | sed -n 's/^Socket(s): *//p'); CPUs usable $ALL_CPUS ($(count "$ALL_CPUS") of $(count "$_online") online)"
    local p1=${P_CPUS%%,*} e1=${E_CPUS%%,*}
    echo "P cores:   $(count "$P_ONE") cores, CPUs $P_CPUS ($(count "$P_CPUS") threads), max $(khz "$p1" max) kHz, min $(khz "$p1" min) kHz"
    [ -n "$E_CPUS" ] && echo "E cores:   $(count "$E_CPUS") CPUs $E_CPUS, max $(khz "$e1" max) kHz, min $(khz "$e1" min) kHz"
    echo "placement: $PLACE"
    echo "governor:  $(cat $SYS/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo ?)"
    echo "caches:    $(lscpu | grep -E '^L[123]' | sed 's/  */ /g' | tr '\n' ';')"
    echo "memory:    $(grep MemTotal /proc/meminfo | sed 's/  */ /g')"
    echo "kernel:    $(uname -sr)"
    echo "gcc:       $(g++ --version 2>/dev/null | head -1)"
    echo "clang:     $(clang++ --version 2>/dev/null | head -1)"
    echo "java:      $(java -version 2>&1 | head -1)"
  } > "$_rec"
  cat "$_rec"
  if [ -n "${1:-}" ]; then cp "$_rec" "$1"; fi
  rm -f "$_rec"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  case "${1:-}" in
    record) machine_record "${2:-}" ;;
    cpus)
      case "${2:-}" in p) l=$P_CPUS ;; p1) l=$P_ONE ;; e) l=$E_CPUS ;; all) l=$ALL_CPUS ;; place) l=$PLACE ;;
        *) echo "machine.sh cpus p|p1|e|all|place [N]" >&2; exit 2 ;; esac
      if [ -n "${3:-}" ]; then take "$3" "$l"; else echo "$l"; fi ;;
    *) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
  esac
fi
