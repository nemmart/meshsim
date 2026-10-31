set -e
T=$1; OUT=$2; EXTRA=$3
B=$T/build
g++ -std=c++20 -O1 -DNDEBUG -fno-rtti -fno-strict-aliasing -w $EXTRA -DXBYAK_NO_EXCEPTION -DXBYAK64 \
  -I$T/src -I$B/_deps/fmt-src/include -I$B/_deps/json-src/include -I$B/_deps/xbyak-src -isystem $T/third-party \
  /home/claude/expdiff/expdump.cpp $B/libtracer.a $B/_deps/fmt-build/libfmt.a -lelf -no-pie -o $OUT
