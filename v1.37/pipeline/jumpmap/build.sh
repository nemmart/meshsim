#!/bin/sh
# Builds gold (if needed) and the jump-map tool into jumpmap/build/ (plain javac, Java 21).
set -e
cd "$(dirname "$0")"
[ -d ../gold/build/gold ] || (cd ../gold && ./build.sh > /dev/null)
rm -rf build
mkdir -p build
javac -d build -cp ../gold/build -Xlint:all -Werror $(find src -name '*.java')
echo "built jumpmap/build ($(find build -name '*.class' | wc -l) classes)"
