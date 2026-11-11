#!/bin/bash
# oracle/conformance.sh (formerly sim/Makefile's `oracle/conformance.sh`; moved by W19): the port-conformance matrix, 10^5
# cycles per configuration, three seeds: ref's observations (oracle/ref/portconf.sh, a JDK and sim/ref) against the
# framework's (clang release), file by file.
set -eu
cd "$(dirname "$0")/.."
make --no-print-directory CC_NAME=clang MODE=release build/clang-release/portconf
./oracle/ref/portconf.sh 100000 build/portconf
n=0
for s in build/portconf/*.script; do
  case $s in *rlink*) continue;; esac
  ./build/clang-release/portconf "$s" | cmp -s - "${s%.script}.obs" || { echo "DIFFERS from ref: $s"; exit 1; }
  n=$((n + 1))
done
echo "conformance: $n scripts of 10^5 cycles equal to ref, line for line (the reserved link is M2's)"
