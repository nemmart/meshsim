#!/bin/bash
# header_diff.sh GENERATED DRAFT (D40): v1.3's tools/header_diff.py in shell over the host gcc, its
# output byte for byte (checked by tests/test_header_diff.sh against the Python's output, tests/regdoc/header_diff.out).
# Compares the generated mesh.h with the draft header (fw/mesh-v1.2-draft.h): every macro both define (numeric value,
# or for function-like macros the value at sample arguments), every macro only one defines, and every struct member's
# offset. Exit status 0 even when they differ: the differences are a report, not a failure; RegDocTest asserts the
# ones that must agree. Needs gcc (the host's: its predefined macros and stdint.h are the baseline, as the Python's).
#
# The Python's semantics, kept: names sorted by code point (LC_ALL=C); a macro's value compiled and printed with
# "%llx" ("UNCOMPARABLE" if either header's program fails); the struct members found by the same patterns (the body
# up to the first "} name;", PERIPHERAL_STD_MEMBERS expanded, comments removed, `MESH_* | uint32_t` declarators and
# union members).
set -u
# code-point order for the name lists (the Python's sorted()); gcc's own messages keep the caller's locale
sort() { LC_ALL=C command sort "$@"; }
comm() { LC_ALL=C command comm "$@"; }
[ $# = 2 ] || { echo "usage: header_diff.sh GENERATED DRAFT"; exit 2; }
GEN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); DRAFT=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

# ---- the macros: name TAB params (empty: object-like), the host's own and the stdint ones left out
gcc -E -dM -x c /dev/null | sed -n 's/^#define \([A-Za-z0-9_]*\).*/\1/p' | sort -u > "$T/base"
macros() {   # HEADER OUT
  gcc -E -dM -x c -include "$1" /dev/null | awk -v base="$T/base" '
    BEGIN { while ((getline l < base) > 0) skip[l] = 1 }
    /^#define / {
      s = substr($0, 9); match(s, /^[A-Za-z0-9_]+/); if (RSTART != 1) next
      n = substr(s, 1, RLENGTH); rest = substr(s, RLENGTH + 1); p = ""
      if (substr(rest, 1, 1) == "(") { e = index(rest, ")"); if (e > 0) p = substr(rest, 1, e) }
      if (n in skip) next
      if (n ~ /^(__|_STDINT|INT|UINT|WCHAR|SIG_|PTRDIFF|SIZE_MAX|WINT)/) next
      print n "\t" p
    }' | sort -t "$(printf '\t')" -k1,1 -u > "$2"
}
macros "$GEN" "$T/G"; macros "$DRAFT" "$T/D"
SKIP="MESH_H MESH_V12_DRAFT_H MESH_RO MESH_WO MESH_RW PERIPHERAL_STD_MEMBERS tile peripheral replayer cc1 tables COMMAND REPLY MESH_H_VERSION INTPTR_MAX INTPTR_MIN UINTPTR_MAX"
printf '%s\n' $SKIP | sort -u > "$T/skip"
cut -f1 "$T/G" > "$T/Gn"; cut -f1 "$T/D" > "$T/Dn"
comm -12 "$T/Gn" "$T/Dn" | comm -23 - "$T/skip" > "$T/both"

# the expression for a name: NAME, or NAME(3, 5, 2, 1, 7)[:k] for a function-like macro (the generated header's params)
expr_of() {
  local p; p=$(awk -F '\t' -v n="$1" '$1 == n { print $2; exit }' "$T/G")
  [ -z "$p" ] && p=$(awk -F '\t' -v n="$1" '$1 == n { print $2; exit }' "$T/D")
  if [ -z "$p" ]; then printf '%s' "$1"; return; fi
  local inner=${p#(}; inner=${inner%)}; local k=0 a
  IFS=, read -r -a parts <<< "$inner"
  for a in "${parts[@]}"; do [ -n "${a//[[:space:]]/}" ] && k=$((k + 1)); done
  local s="" i; local args=(3 5 2 1 7)
  for ((i = 0; i < k && i < 5; i++)); do s+="${s:+, }${args[i]}"; done
  printf '%s(%s)' "$1" "$s"
}
values() {   # HEADER OUT NAMES...: "NAME HEX" lines for the names whose program compiles (all at once, else one by one)
  local h=$1 out=$2; shift 2
  prog() { printf '#include <stdio.h>\n#include "%s"\nint main(void){\n' "$h"
           local n; for n in "$@"; do printf '  printf("%s %%llx\\n", (unsigned long long)(%s));\n' "$n" "$(expr_of "$n")"; done
           printf 'return 0;}\n'; }
  prog "$@" > "$T/v.c"
  if gcc -w -o "$T/v" "$T/v.c" 2> /dev/null; then "$T/v" > "$out"; return; fi
  : > "$out"
  local n; for n in "$@"; do prog "$n" > "$T/v.c"; gcc -w -o "$T/v" "$T/v.c" 2> /dev/null && "$T/v" >> "$out"; done
}
mapfile -t BOTH < "$T/both"
values "$GEN" "$T/vg" "${BOTH[@]}"; values "$DRAFT" "$T/vd" "${BOTH[@]}"
awk -v vg="$T/vg" -v vd="$T/vd" '
  BEGIN { while ((getline l < vg) > 0) { split(l, f, " "); g[f[1]] = f[2] }
          while ((getline l < vd) > 0) { split(l, f, " "); d[f[1]] = f[2] } }
  { n = $1; nb++
    if ((n in g) && (n in d)) { ok++; if (g[n] != d[n]) diff[++nd] = sprintf("  DIFFERS %-22s generated 0x%s, draft 0x%s", n, g[n], d[n]) }
    else print "UNCOMPARABLE " n }
  END { printf "macros in both: %d, compared: %d, differing: %d\n", nb, ok, nd; for (i = 1; i <= nd; i++) print diff[i] }' "$T/both"
comm -23 "$T/Dn" "$T/Gn" | comm -23 - "$T/skip" | sed 's/^/  ONLY IN DRAFT /'
comm -13 "$T/Dn" "$T/Gn" | comm -23 - "$T/skip" | sed 's/^/  ONLY GENERATED /'

# ---- the struct members: STRUCT TAB MEMBER, sorted, unique
members() {   # HEADER OUT
  awk 'BEGIN { RS = "\001" } {
    t = $0
    std = ""; split("PARAMS STATUS ERRORS PULSE_RUN PULSE_DMA EVENTS EVENTS_PEEK EVENTS_MASK DMA_HDR DMA_ADDR", w, " ")
    for (i = 1; i <= 10; i++) std = std (i > 1 ? " " : "") "MESH_X " w[i] ";"
    while ((s = index(t, "typedef struct {")) > 0) {
      t = substr(t, s + 16)
      if (!match(t, /\} [A-Za-z0-9_]+;/)) break
      body = substr(t, 1, RSTART - 1); name = substr(t, RSTART + 2, RLENGTH - 3); t = substr(t, RSTART + RLENGTH)
      out = ""
      while ((p = index(body, "PERIPHERAL_STD_MEMBERS;")) > 0) { out = out substr(body, 1, p - 1) std; body = substr(body, p + 23) }
      body = out body
      out = ""
      while ((p = index(body, "/*")) > 0) {
        rest = substr(body, p + 2); q = index(rest, "*/"); if (q == 0) break
        out = out substr(body, 1, p - 1); body = substr(rest, q + 2)
      }
      body = out body
      b = body
      while (match(b, /(MESH_[A-Za-z0-9_]+|uint32_t)[ \t\n\r\f\v]+[^;]+;/)) {
        m = substr(b, RSTART, RLENGTH - 1); b = substr(b, RSTART + RLENGTH)
        sub(/^(MESH_[A-Za-z0-9_]+|uint32_t)[ \t\n\r\f\v]+/, "", m)
        nv = split(m, vs, ",")
        for (j = 1; j <= nv; j++) {
          v = vs[j]; gsub(/^[ \t\n\r\f\v]+|[ \t\n\r\f\v]+$/, "", v); sub(/\[.*/, "", v)
          if (v != "" && substr(v, 1, 1) != "_") print name "\t" v
        }
      }
      b = body
      while (match(b, /union \{[^}]*\}/)) {
        u = substr(b, RSTART, RLENGTH); b = substr(b, RSTART + RLENGTH)
        while (match(u, /MESH_[A-Za-z0-9_]+ [A-Za-z0-9_]+;/)) {
          x = substr(u, RSTART, RLENGTH - 1); u = substr(u, RSTART + RLENGTH); sub(/^MESH_[A-Za-z0-9_]+ /, "", x)
          print name "\t" x
        }
      }
    }
  }' "$1" | sort -u > "$2"
}
members "$GEN" "$T/mg"; members "$DRAFT" "$T/md"
comm -12 "$T/mg" "$T/md" > "$T/mboth"
for h in "$GEN" "$DRAFT"; do
  { printf '#include <stdio.h>\n#include <stddef.h>\n#include "%s"\nint main(void){\n' "$h"
    awk -F '\t' '{ printf "  printf(\"%s.%s %%zx\\n\", offsetof(%s, %s));\n", $1, $2, $1, $2 }' "$T/mboth"
    printf 'return 0;}\n'; } > "$T/o.c"
  if ! gcc -w -o "$T/o" "$T/o.c" 2> "$T/o.err"; then head -c 2000 "$T/o.err"; echo; exit 1; fi
  "$T/o" > "$T/o.$([ "$h" = "$GEN" ] && echo g || echo d)"
done
awk -v od="$T/o.d" '
  BEGIN { while ((getline l < od) > 0) { split(l, f, " "); if (!(f[1] in d)) nd0++; d[f[1]] = f[2] } }
  { if (!($1 in g)) { order[++n] = $1 } g[$1] = $2 }
  END { for (i = 1; i <= n; i++) if (g[order[i]] != d[order[i]]) bad[++nb] = "  OFFSET DIFFERS " order[i] " " g[order[i]] " " d[order[i]]
        printf "struct members in both: %d, differing offsets: %d\n", n, nb; for (i = 1; i <= nb; i++) print bad[i] }' "$T/o.g"
comm -13 "$T/mg" "$T/md" | sed 's/^\(.*\)\t\(.*\)$/  MEMBER ONLY IN DRAFT \1 \2/'
comm -23 "$T/mg" "$T/md" | sed 's/^\(.*\)\t\(.*\)$/  MEMBER ONLY GENERATED \1 \2/'
exit 0
