# machine.inc (WORKER5's Quest tasks; D21: nothing machine-specific baked in; to be replaced by W4's sim/tools/machine.sh).
# Sourced by a task. Defines:
#   machine_record FILE   writes what the machine is: CPU model, sockets, cores and threads, core types (P/E) with their
#                         CPUs and max/min clocks, caches, memory, kernel, compilers and Java; and prints it
#   expand LIST           "0-3,8" -> "0,1,2,3,8"
# and sets, from the arguments "$1" (P CPUs) and "$2" (E CPUs) when given, else detected:
#   P_CPUS    the performance cores' CPUs (hybrid: /sys/devices/cpu_core/cpus; otherwise every online CPU)
#   P_ONE     one CPU per P core (the first of each thread_siblings_list)
#   E_CPUS    the efficiency cores' CPUs (/sys/devices/cpu_atom/cpus), or empty
#   ALL_CPUS  every online CPU
expand() { local out= p a b; IFS=, read -ra parts <<< "$1"; for p in "${parts[@]}"; do
  if [[ $p == *-* ]]; then a=${p%-*}; b=${p#*-}; for ((i=a; i<=b; i++)); do out+=${out:+,}$i; done; elif [ -n "$p" ]; then out+=${out:+,}$p; fi; done; echo "$out"; }
ALL_CPUS=$(expand "$(cat /sys/devices/system/cpu/online)")
if [ -n "${1:-}" ]; then P_CPUS=$(expand "$1"); elif [ -r /sys/devices/cpu_core/cpus ]; then P_CPUS=$(expand "$(cat /sys/devices/cpu_core/cpus)"); else P_CPUS=$ALL_CPUS; fi
if [ -n "${2:-}" ]; then E_CPUS=$(expand "$2"); elif [ -r /sys/devices/cpu_atom/cpus ]; then E_CPUS=$(expand "$(cat /sys/devices/cpu_atom/cpus)"); else E_CPUS=; fi
P_ONE=; for c in ${P_CPUS//,/ }; do f=$(cut -d, -f1 /sys/devices/system/cpu/cpu$c/topology/thread_siblings_list | cut -d- -f1); [ "$f" = "$c" ] && P_ONE+=${P_ONE:+,}$c; done
count() { [ -z "$1" ] && echo 0 || echo "$1" | tr ',' '\n' | wc -l; }
khz() { cat /sys/devices/system/cpu/cpu$1/cpufreq/cpuinfo_$2_freq 2>/dev/null || echo "?"; }
machine_record() {
  { echo "model:     $(lscpu | sed -n 's/^Model name: *//p' | head -1)"
    echo "sockets:   $(lscpu | sed -n 's/^Socket(s): *//p'), CPUs online $ALL_CPUS ($(count "$ALL_CPUS"))"
    local p1=${P_CPUS%%,*} e1=${E_CPUS%%,*}
    echo "P cores:   $(count "$P_ONE") cores, CPUs $P_CPUS ($(count "$P_CPUS") threads), max $(khz $p1 max) kHz, min $(khz $p1 min) kHz"
    [ -n "$E_CPUS" ] && echo "E cores:   $(count "$E_CPUS") cores, CPUs $E_CPUS, max $(khz $e1 max) kHz, min $(khz $e1 min) kHz"
    echo "governor:  $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo ?)"
    echo "caches:    $(lscpu | grep -E '^L[123]' | sed 's/  */ /g' | tr '\n' ';')"
    echo "memory:    $(grep MemTotal /proc/meminfo)"
    echo "kernel:    $(uname -sr)"
    echo "gcc:       $(g++ --version 2>/dev/null | head -1)"
    echo "clang:     $(clang++ --version 2>/dev/null | head -1)"
    echo "java:      $(java -version 2>&1 | head -1)"
  } > "$1"
  cat "$1"
}
