#!/bin/bash
# header_diff_crosscheck.sh (WORKER23; P5, D40): tools/regs/header_diff.sh against v1.3's tools/header_diff.py
# (sim/ref/meshsim/tools), byte for byte: on v1.35's mesh.h and the draft header, and on perturbed pairs reaching every
# kind of line (DIFFERS, ONLY IN DRAFT, OFFSET DIFFERS, MEMBER ONLY ...). Needs python3 and gcc; reads sim/ref only.
set -u
SIM=$(cd "$(dirname "$0")/../.." && pwd)
PY="$SIM/ref/meshsim/tools/header_diff.py"; SH="$SIM/tools/regs/header_diff.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0
pair() {   # NAME GEN DRAFT
  python3 "$PY" "$2" "$3" > "$T/py" 2>&1; bash "$SH" "$2" "$3" > "$T/sh" 2>&1
  if cmp -s "$T/py" "$T/sh"; then echo "header_diff $1: equal ($(wc -l < "$T/py") lines)"; else echo "FAIL header_diff $1 differs"; diff "$T/py" "$T/sh" | head; fail=1; fi
}
pair "mesh.h / draft" "$SIM/fw/include/mesh.h" "$SIM/fw/mesh-v1.2-draft.h"
cp "$SIM/fw/include/mesh.h" "$T/gen.h"; cp "$SIM/fw/mesh-v1.2-draft.h" "$T/draft.h"
sed -i 's/^#define DST_BROADCAST      126u/#define DST_BROADCAST      125u/' "$T/draft.h"
printf '#define ONLY_DRAFT_X 5\n#define ONLY_DRAFT_F(a, b) ((a) + (b))\n#define HDR_DST2(d, e) ((d) * 3 + (e))\n' >> "$T/draft.h"
echo '#define HDR_DST2(d, e) ((d) * 3 + (e) + 1)' >> "$T/gen.h"
pair "values differing" "$T/gen.h" "$T/draft.h"
printf 'typedef struct {\n    MESH_RW a, b[2]; /* c */\n    uint32_t _pad;\n    union { MESH_RO u1; MESH_WO u2; };\n    MESH_RW d;\n} Extra;\n' >> "$T/gen.h"
printf 'typedef struct {\n    MESH_RW a;\n    MESH_RW b[2];\n    union { MESH_RO u1; MESH_WO u3; };\n    MESH_RW d;\n} Extra;\n' >> "$T/draft.h"
pair "members differing" "$T/gen.h" "$T/draft.h"
exit $fail
