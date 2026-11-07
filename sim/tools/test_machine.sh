#!/bin/bash
# test_machine.sh: machine.sh's placement on fake /sys trees, against the lists test_topology.cpp checks (WORKER4, D21).
set -u
T=$(dirname "$0"); F=0
check() { local got; got=$(SIM_SYSFS=/tmp/sim-fs-$$-$1 SIM_ALLOWED_CPUS=$2 "$T/machine.sh" cpus "$3")
  [ "$got" = "$4" ] && echo "ok   machine.sh $1 $3 ($2)" || { echo "FAIL machine.sh $1 $3 ($2): $got, want $4"; F=1; }; }
for L in hybrid server plain; do "$T/fake_sysfs.sh" /tmp/sim-fs-$$-$L $L; done
check hybrid 0-15 place 0,2,4,6,8,10,1,3,5,7,9,11,12,13,14,15
check hybrid 0-15 p1 0,2,4,6,8,10
check hybrid 0-15 e 12,13,14,15
check hybrid 2-9,13 place 2,4,6,8,3,5,7,9,13
check server 0-15 place 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
check server 0-15 p1 0,1,2,3,4,5,6,7
check plain 0-15 place 0,1,2,3
got=$(SIM_SYSFS=/tmp/sim-fs-$$-hybrid SIM_ALLOWED_CPUS=0-15 "$T/machine.sh" cpus place 3)
[ "$got" = 0,2,4 ] && echo "ok   machine.sh cpus place 3" || { echo "FAIL machine.sh cpus place 3: $got"; F=1; }
rm -rf /tmp/sim-fs-$$-*
exit $F
