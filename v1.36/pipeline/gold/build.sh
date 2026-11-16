#!/bin/sh
# Builds gold with plain javac into build/ (Java 21) and runs the tests.
#   ./build.sh                    build, then the inline emit test against test/inline-emit-digests.txt and the
#                                 inline vector test (upstream's inline test vectors, test/inline-vectors.jsonl.gz)
#   JOLT_CPP=<jolt-cpp checkout> ./build.sh
#                                 the same, but row for row against <jolt-cpp>/src/tracer/inlines/*.inc
set -e
cd "$(dirname "$0")"
rm -rf build
mkdir -p build
javac -d build -Xlint:all -Werror $(find src test -name '*.java')
echo "built build/ ($(find build -name '*.class' | wc -l) classes)"
if [ -n "$JOLT_CPP" ]; then
  java -cp build gold.InlineEmitTest "$JOLT_CPP/src/tracer/inlines"
else
  java -cp build gold.InlineEmitTest
fi
java -cp build gold.InlineVectorTest test/inline-vectors.jsonl.gz
