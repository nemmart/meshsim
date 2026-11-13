#!/bin/bash
# W19 step 4: everything that needs Java or sim/ref into sim/oracle/. Run from the repo top.
set -eu
cd sim
mkdir -p oracle/regen oracle/bench
git mv tests/ref oracle/ref
for f in golden golden-m2 golden-m3 fp-m1 g1-replay; do
  git mv tests/$f/regen.sh oracle/regen/$f.sh
  sed -i "s#HERE=\$(cd \"\$(dirname \"\$0\")\" && pwd)#HERE=\$(cd \"\$(dirname \"\$0\")/../../tests/$f\" \&\& pwd)#" oracle/regen/$f.sh
  grep -q "tests/$f\" && pwd)" oracle/regen/$f.sh || { echo "HERE not rewritten in $f"; exit 1; }
done
git mv bench/w14/TracerPlay.java oracle/bench/TracerPlay.java
# the conformance target -> oracle/conformance.sh
python3 - <<'PY'
p='Makefile'; s=open(p).read()
a=s.index('# The conformance tool'); b=s.index('clean:\n')
block=s[a:b]
keep='''# The conformance tool (the C++ side of PORTS.md 10): the matrix against ref is oracle/conformance.sh (W19: it needs
# a JDK and sim/ref).
$(B)/portconf: $(B)/tests/portconf_main.o $(LIB_OBJ)
\t$(CXX) $(FLAGS) $^ -o $@ $(WRAP)

'''
s=s[:a]+keep+s[b:]
s=s.replace('#   make conformance the port-conformance matrix: ref\'s side (tests/ref/portconf.sh, needs a JDK) against the C++ ports\n',
            '#   (the port-conformance matrix against ref: oracle/conformance.sh, needs a JDK and sim/ref)\n')
s=s.replace('.PHONY: all debug run check conformance clean','.PHONY: all debug run check clean')
assert 'conformance:' not in s and 'make conformance' not in s, 'conformance left'
open(p,'w').write(s)
PY
cat > oracle/conformance.sh <<'CS'
#!/bin/bash
# oracle/conformance.sh (formerly sim/Makefile's `make conformance`; moved by W19): the port-conformance matrix, 10^5
# cycles per configuration, three seeds: ref's observations (oracle/ref/portconf.sh, a JDK and sim/ref) against the
# framework's (clang release), file by file.
set -eu
cd "$(dirname "$0")/.."
make --no-print-directory CC_NAME=clang MODE=release build/clang-release/portconf
./oracle/ref/portconf.sh 100000 build/portconf
n=0
for s in build/portconf/*.script; do
  case $s in *rlink*) continue;; esac
  ./build/clang-release/portconf "$s" | cmp -s - "${s%.script}.obs" || { echo "DIFFERS from ref: $s"; exit 1; }
  n=$((n + 1))
done
echo "conformance: $n scripts of 10^5 cycles equal to ref, line for line (the reserved link is M2's)"
CS
chmod +x oracle/conformance.sh
# repoint every mention outside sim/ref and the planning records
files=$(grep -rlI -e 'tests/ref' -e 'regen\.sh' -e 'TracerPlay\.java' -e 'make conformance' . | grep -v '^\./ref/\|^\./planning/' || true)
for f in $files; do
  sed -i -E \
    -e 's#tests/ref/#oracle/ref/#g; s#tests/ref\b#oracle/ref#g' \
    -e 's#(sim/)?tests/(golden|golden-m2|golden-m3|fp-m1|g1-replay)/regen\.sh#\1oracle/regen/\2.sh#g' \
    -e 's#(sim/)?bench/w14/TracerPlay\.java#\1oracle/bench/TracerPlay.java#g' \
    -e 's#make conformance#oracle/conformance.sh#g' "$f"
done
# bare mentions of a fixture's regen.sh
bare() { fx=$1; shift; for f in "$@"; do [ -f "$f" ] && perl -pi -e "s#(?<![/\\w])regen\\.sh#oracle/regen/$fx.sh#g" "$f"; done; true; }
bare golden tests/golden/build_images.sh tests/golden/RUNS tests/TRACE-FORMAT.md
bare golden-m2 tests/golden-m2/README.md tests/golden-m2/RUNS tests/golden-m2/images/MANIFEST.md tests/AUDIT-M2.md
bare golden-m3 tests/golden-m3/README.md tests/AUDIT-M3.md
echo "step4: moved; $(echo $files | wc -w) files repointed"
grep -rnI 'tests/ref' . | grep -v '^\./ref/\|^\./planning/' || echo "no tests/ref left"
