#!/bin/bash
# lib/classes.sh MESHSIM DIR: compiles gold and jumpmap (MESHSIM/wip/) and v1.26.1's ConvertDelivery (with meshsim's src/
# and the replay harnesses, as v1.26.1's acceptance/make_kit.sh) into DIR/{gold,jumpmap,meshsim}, for Java 21.
set -eu
. "$(dirname "$0")/../pins.sh"
M=$1; C=$2; S=$M/$MESHSIM_REL
rm -rf "$C"; mkdir -p "$C"/{gold,jumpmap,meshsim}
javac --release 21 -nowarn -d "$C/gold" $(find "$M/wip/gold/src" -name '*.java')
javac --release 21 -nowarn -d "$C/jumpmap" -cp "$C/gold" $(find "$M/wip/jumpmap/src" -name '*.java')
javac --release 21 -nowarn -d "$C/meshsim" "$S"/src/*.java 2>&1 | grep -v '^Note:' || true
javac --release 21 -nowarn -d "$C/meshsim" -cp "$C/meshsim" "$S"/test/T.java "$S"/test/Replay*.java \
  "$S"/test/StageB.java "$S"/test/SyntheticDelivery.java "$S"/test/ConvertDelivery.java 2>&1 | grep -v '^Note:' || true
for c in gold/gold/Main.class jumpmap/jumpmap/Main.class meshsim/ConvertDelivery.class; do [ -f "$C/$c" ] || { echo "classes: no $c"; exit 1; }; done
