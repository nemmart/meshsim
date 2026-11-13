#!/bin/bash
# oracle/regs_crosscheck.sh (WORKER19, P1): v1.35's register generator (tools/regs/gen_regs) against v1.3's own
# (sim/ref's Registers.java and RegDoc.java, run now): mesh.h and REGISTERS.md byte for byte after their opening
# comment and paragraph (v1.35's describe v1.35: P8), and both equal to the committed fw/include/mesh.h and
# fw/REGISTERS.md. Needs sim/ref, a JDK and a C++ compiler.
set -eu
SIM=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
javac -nowarn -d "$T/cls" "$SIM"/ref/meshsim/src/*.java 2>&1 | grep -v '^Note' || true
(cd "$T" && java -cp cls Registers v13-mesh.h > /dev/null && java -cp cls RegDoc v13-REGISTERS.md > /dev/null)
${CXX:-g++} -std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -O1 "$SIM/tools/regs/gen_regs.cpp" -o "$T/gen_regs"
"$T/gen_regs" mesh.h "$T/mesh.h" > /dev/null; "$T/gen_regs" registers "$T/REGISTERS.md" > /dev/null
bad=0
body() { case $1 in *mesh.h) sed -n '/^#ifndef MESH_H$/,$p' "$1";; *) sed -n '/^## 1\. /,$p' "$1";; esac; }   # after the banner
for p in "v13-mesh.h mesh.h fw/include/mesh.h" "v13-REGISTERS.md REGISTERS.md fw/REGISTERS.md"; do
  set -- $p
  cmp -s <(body "$T/$1") <(body "$T/$2") || { echo "DIFFERS: gen_regs's $2 against v1.3's generator"; bad=1; }
  cmp -s <(body "$T/$1") <(body "$SIM/$3") || { echo "DIFFERS: $3 against v1.3's generator"; bad=1; }
  cmp -s "$T/$2" "$SIM/$3" || { echo "DIFFERS: $3 against gen_regs's $2"; bad=1; }
done
[ $bad = 0 ] || { echo "regs_crosscheck: FAILED"; exit 1; }
echo "regs_crosscheck: mesh.h and REGISTERS.md from gen_regs equal to v1.3's generators' after the banners, and to the committed files"
