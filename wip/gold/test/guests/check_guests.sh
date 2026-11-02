#!/bin/sh
# Runs gold on every hand-assembled guest (make_guests.py) and compares with expected.txt: rows and run hash, or the
# abort. The jolt-cpp and upstream results for the same guests are in WORKER18/NOTES.md ("Test guests").
cd "$(dirname "$0")"
[ -d ../../build ] || ../../build.sh
mkdir -p /tmp/gold-empty-input
for g in $(ls -d */ | tr -d /); do printf "%s: %s\n" $g "$(java -cp ../../build gold.Main run $g /tmp/gold-empty-input)"; done > /tmp/gold-guests.txt
if diff -u expected.txt /tmp/gold-guests.txt; then echo "guests: all as expected"; else echo "GUESTS DIFFER"; exit 1; fi
