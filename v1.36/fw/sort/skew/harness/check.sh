#!/bin/sh
# The planner's regression test: build the host harness and the RV32 planner image, run every set in both formats,
# and compare with the expected results (results.txt: 128-bit values, results-fmt32.txt: 32-bit values).
set -e
cd "$(dirname "$0")"
./build.sh
./harness > /tmp/skew-results.txt
./harness --fmt 32 > /tmp/skew-results-fmt32.txt
diff results.txt /tmp/skew-results.txt && diff results-fmt32.txt /tmp/skew-results-fmt32.txt && echo "planner harness: both formats match the expected results"
