#!/bin/sh
# readme_examples.sh: README.md holds the two examples word for word. This checks that it still does: every line of
# examples/*.cpp must be a line of README.md.
cd "$(dirname "$0")"
for f in examples/*.cpp; do
    if grep -Fxv -f README.md "$f" | grep -q .; then
        echo "README.md does not hold $f as it is:"; grep -Fxv -f README.md "$f" | head -5; exit 1
    fi
done
echo "README.md holds the examples as they are"
