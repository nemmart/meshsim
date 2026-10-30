#!/bin/sh
# Build and run check_div against the reference tracer (jolt-asic-planning tracer-standalone 4f36b3d).
#   TRACER_SRC  the tracer's src/ directory (default: ../../../../jolt-asic-planning/tracer/src)
#   TB          its CMake build directory with libtracer.a (default /tmp/tb); build it with
#     cmake -S <jolt-asic-planning>/tracer -B /tmp/tb -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
#     make -C /tmp/tb -j8 tracer
#   $1          random cases per template (default 1000000)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TR=${TRACER_SRC:-$HERE/../../../../jolt-asic-planning/tracer/src}
TB=${TB:-/tmp/tb}
python3 "$HERE/../gen.py" > /dev/null
FLAGS="-std=c++20 -O2 -DNDEBUG -fno-rtti -I$TR -I$TB/_deps/xbyak-src -I$TB/_deps/fmt-src/include \
       -I$TB/_deps/json-src/include -DXBYAK_NO_EXCEPTION -DXBYAK64"
LIBS="-Wl,--allow-multiple-definition $TB/libtracer.a $TB/_deps/fmt-build/libfmt.a -lelf -no-pie -lpthread"
g++ $FLAGS "$HERE/check_div.cpp" -o /tmp/check_div $LIBS
/tmp/check_div "${1:-1000000}" 2>/dev/null | tee "$HERE/../results/check_div.txt"
