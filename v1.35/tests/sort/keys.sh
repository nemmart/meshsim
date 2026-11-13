#!/bin/bash
# keys.sh N SET... -- the sort's key sets for a step or the kit (owner's Q4): build/keys-N/<set>.keys,
# prebuilt and checked by hash (tests/sort/ref/KEYS.sha256, pipeline/keys/EXPECTED.sha256) by default; made with
# pipeline/keys' KeyGen only when a set is missing and a JDK is present (the testing runs need no Java, D40).
# Run from sim/. Prints the sets' directory; exits 1 if a set is missing or differs from its recorded hash.
set -u
N=${1:?usage: keys.sh N SET...}; shift
D=build/keys-$N; mkdir -p "$D"
for s in "$@"; do
  f=$D/$s.keys
  if [ ! -f "$f" ]; then
    if command -v javac > /dev/null && command -v java > /dev/null; then
      K=$(mktemp -d); javac -nowarn -d "$K" pipeline/keys/KeyGen.java && (cd build && java -Xmx2g -cp "$K" KeyGen keys-$N --n "$N" --only "$s" > /dev/null); rm -rf "$K"
    fi
    [ -f "$f" ] || { echo "keys.sh: $f is missing (put the prebuilt set there, or install a JDK to make it with pipeline/keys)" >&2; exit 1; }
  fi
  want=$(cat tests/sort/ref/KEYS.sha256 pipeline/keys/EXPECTED.sha256 2>/dev/null | awk -v n="keys-$N/$s.keys" '$2 == n {print $1; exit}')
  if [ -n "$want" ] && [ "$(sha256sum < "$f" | cut -c1-64)" != "$want" ]; then echo "keys.sh: $f differs from its recorded sha256" >&2; exit 1; fi
done
echo "$D"
