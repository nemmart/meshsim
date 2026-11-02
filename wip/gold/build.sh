#!/bin/sh
# Builds gold with plain javac into build/ (Java 21). Usage: ./build.sh
set -e
cd "$(dirname "$0")"
rm -rf build
mkdir -p build
javac -d build -Xlint:all -Werror $(find src -name '*.java')
echo "built build/ ($(find build -name '*.class' | wc -l) classes)"
