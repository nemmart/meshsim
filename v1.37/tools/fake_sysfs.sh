#!/bin/bash
# fake_sysfs.sh DIR LAYOUT: a /sys tree with a known CPU layout, for testing machine.sh and the C++ detection.
#   hybrid   the owner's runner (i5-14400F): 6 P cores with hyperthreads (CPUs 0-11, siblings 0-1, 2-3, ..), 4 E (12-15)
#   server   two sockets of 4 cores with hyperthreads, Linux numbering: CPUs 0-7 the first threads, 8-15 their siblings
#   plain    4 cores, no hyperthreads
set -eu
D=$1; L=$2
rm -rf "$D"; mkdir -p "$D/devices/system/cpu"
sib() { mkdir -p "$D/devices/system/cpu/cpu$1/topology"; echo "$2" > "$D/devices/system/cpu/cpu$1/topology/thread_siblings_list";
        echo "$3" > "$D/devices/system/cpu/cpu$1/topology/physical_package_id"; }
case $L in
  hybrid) echo 0-15 > "$D/devices/system/cpu/online"
          for c in 0 2 4 6 8 10; do sib $c "$c-$((c+1))" 0; sib $((c+1)) "$c-$((c+1))" 0; done
          for c in 12 13 14 15; do sib $c "$c" 0; done
          mkdir -p "$D/devices/cpu_core" "$D/devices/cpu_atom"; echo 0-11 > "$D/devices/cpu_core/cpus"; echo 12-15 > "$D/devices/cpu_atom/cpus" ;;
  server) echo 0-15 > "$D/devices/system/cpu/online"
          for c in 0 1 2 3 4 5 6 7; do p=$((c / 4)); sib $c "$c,$((c+8))" $p; sib $((c+8)) "$c,$((c+8))" $p; done ;;
  plain)  echo 0-3 > "$D/devices/system/cpu/online"; for c in 0 1 2 3; do sib $c "$c" 0; done ;;
esac
