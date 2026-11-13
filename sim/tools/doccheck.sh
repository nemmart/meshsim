#!/bin/bash
# tools/doccheck.sh [-v]: the documents' checks (V135-DOCS-PROPOSAL section 6), on the files of the frozen copy
# (tools/freeze/frozen.sh --untracked: sim/'s files less ref/, oracle/ and the rest the copy leaves out), so a path to
# something the copy leaves out counts as missing. Four checks, each failure one line "KIND file:line: detail":
#   MISSING      a path a document cites doesn't exist: a link's target, or a word in code (`...` or a fenced block)
#                that is a path (it has a folder and a known extension, or its first folder is one of the tree's), or
#                a document's bare name (README.md, PORTS.md: found anywhere in the tree)
#   OUTSIDE      a document cites a sim/, ref/, oracle/ or wip/ path (paths are relative to the version's root)
#   NO-README    a folder down to two levels below the root has no README.md and isn't named in its parent's
#                ("name/" or `name`); deeper, the nearest README above speaks for it; planning/ needs none
#   DOC-MISSING  a source file's comment names a document (*.md) that isn't in the tree (by its name: documents keep their
#                names when they move, rule 7)
# The history (planning/ and spec/history/, but their own README.md) is kept whole and not checked for paths. A path in
# a command beginning with "cd DIR" is also tried from DIR (and skipped when DIR is not a plain path). Exemptions, by
# document and cited path: tools/doccheck-exempt.txt (kinds: other-repo, historical, output, cd). Generated
# paths (build/, results/, fw/build/) and other versions' (v1.x/, ../) aren't checked. Exit 1 on any failure; -v also
# lists every path that was checked.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
cd "$(dirname "$0")/.."
V=0; [ "${1:-}" = -v ] && V=1
LIST=$(mktemp); trap 'rm -f "$LIST" "$LIST".*' EXIT
tools/freeze/frozen.sh --untracked > "$LIST"
# every folder of the tree (for "is it a folder" and the README check)
awk -F/ '{ p = ""; for (i = 1; i < NF; i++) { p = p (i > 1 ? "/" : "") $i; print p } }' "$LIST" | sort -u > "$LIST.dirs"
HIST='^(planning|spec/history)/'
EXT='md|h|hpp|cpp|c|S|s|sh|mk|py|ld|inc|txt|csv|log|json|gz|tgz|hex|bin|sym|java|patch|sha256|out|fp|dig|trace|tb|ref|cfg|html|svg|tsv'

# ---- the exemptions: DOC<TAB>cited path<TAB>kind<TAB>why (tools/doccheck-exempt.txt), by document and path, not line
EX=tools/doccheck-exempt.txt
grep -vE '^[[:space:]]*(#|$)' "$EX" 2>/dev/null > "$LIST.ex" || true
awk -F'\t' 'NF < 4 || $3 !~ /^(other-repo|historical|output|cd)$/ { print "doccheck: " FILENAME ": bad exemption: " $0; bad = 1 } END { exit bad }' "$LIST.ex" || exit 2

# ---- the documents: the paths they cite
grep -E '\.md$' "$LIST" | grep -Ev "$HIST" > "$LIST.docs" || true
grep -Ex '(planning|spec/history)/README\.md' "$LIST" >> "$LIST.docs" || true
while IFS= read -r doc; do
  awk -v DOC="$doc" -v EXT="$EXT" '
    function emit(tok, kind,   t) {
      t = tok
      sub(/^[("'"'"'<]+/, "", t); sub(/[)"'"'"'>.,;:!?]+$/, "", t)
      sub(/#.*$/, "", t); sub(/:[0-9]+(-[0-9]+)?$/, "", t); sub(/^\.\//, "", t)
      if (t == "" || t ~ /^(https?|mailto|ftp):/ || t ~ /:\/\//) return
      if (t ~ /[*?<>{}$\[\]|~%@=`\\]/ || t ~ /^-/ || t ~ /^\//) return
      if (t ~ /\.\.\.|^\.+$/) return
      if (kind == "code") {
        if (t !~ /^[A-Za-z0-9_.+-]+(\/[A-Za-z0-9_.+-]*)*$/) return
        if (t !~ /\// && t !~ ("\\.(" EXT ")$")) return
      }
      print FNR "\t" kind "\t" t "\t" cdir
    }
    function words(s, kind,   n, a, i) { n = split(s, a, /[ \t,;()"'"'"'=]+/); for (i = 1; i <= n; i++) if (a[i] != "") emit(a[i], kind) }
    function cd_of(x,   t) {   # a command beginning with cd: its folder, or \001 when it is not a plain path
      if (!match(x, /^[ \t]*(\$ )?cd[ \t]+/)) return ""
      t = substr(x, RSTART + RLENGTH); sub(/[ \t]*(&&|;|\|\||$).*/, "", t)
      return t ~ /^[A-Za-z0-9_.\/-]+$/ ? t : "\001"
    }
    /^[ \t]*(```|~~~)/ { fence = !fence; cdir = ""; next }
    fence { c = cd_of($0); if (c != "") cdir = c; words($0, "code"); next }
    {
      s = $0
      while (match(s, /\]\([^) \t]+\)/)) { emit(substr(s, RSTART + 2, RLENGTH - 3), "link"); s = substr(s, RSTART + RLENGTH) }
      if (match($0, /^[ \t]*\[[^\]]+\]:[ \t]+[^ \t]+/)) { t = $0; sub(/^[ \t]*\[[^\]]+\]:[ \t]+/, "", t); sub(/[ \t].*/, "", t); emit(t, "link") }
      s = $0
      while (match(s, /`[^`]+`/)) { span = substr(s, RSTART + 1, RLENGTH - 2); s = substr(s, RSTART + RLENGTH); cdir = cd_of(span); words(span, "code"); cdir = "" }
    }' "$doc" | while IFS=$'\t' read -r line kind p cdir; do printf '%s\t%s\t%s\t%s\t%s\n' "$doc" "$line" "$kind" "$p" "$cdir"; done
done < "$LIST.docs" > "$LIST.cites"

awk -F'\t' -v FILES="$LIST" -v DIRS="$LIST.dirs" -v V=$V -v EXF="$LIST.ex" '
  function norm(p,   n, a, o, i, k) {   # resolve . and .. ; "" if it climbs out of the root
    n = split(p, a, "/"); k = 0
    for (i = 1; i <= n; i++) { if (a[i] == "" || a[i] == ".") continue; if (a[i] == "..") { if (k == 0) return "\001"; k--; continue } o[++k] = a[i] }
    p = ""; for (i = 1; i <= k; i++) p = p (i > 1 ? "/" : "") o[i]; return p
  }
  function there(p) { return (p in F) || (p in D) }
  BEGIN { while ((getline l < FILES) > 0) { F[l] = 1; b = l; sub(/.*\//, "", b); B[b] = 1 }
          while ((getline l < DIRS) > 0) { D[l] = 1; t = l; sub(/\/.*/, "", t); TOP[t] = 1 }
          while ((getline l < EXF) > 0) { split(l, e, "\t"); EX[e[1] SUBSEP e[2]] = 1 } }
  {
    doc = $1; line = $2; kind = $3; p = $4; cd = $5; dir = doc;
    if ((doc SUBSEP p) in EX) next
    if (cd == "\001") next   # after a cd to a folder that is not a plain path
    if (dir ~ /\//) sub(/\/[^\/]*$/, "", dir); else dir = ""
    q = p; sub(/\/$/, "", q); first = q; sub(/\/.*/, "", first)
    if (first ~ /^(sim|ref|oracle|wip)$/) { print "OUTSIDE " doc ":" line ": " p; bad++; next }
    if (first ~ /^(build|results)$/ || q ~ /^fw\/build(\/|$)/ || first ~ /^v1\.[0-9.]+$/) next
    rel = norm((dir == "" ? "" : dir "/") q); root = norm(q)
    if (rel == "\001") next                                   # ../ out of the version: another version
    if (kind == "code" && q ~ /\// && !(first in TOP) && !there(norm((dir == "" ? "" : dir "/") first)) && q !~ /\.[A-Za-z0-9]+$/) next   # prose like read/write
    if (q !~ /\// && q !~ /\.md$/) next                       # a bare file name, not a document: a name, not a path
    ok = there(rel) || there(root) || (q !~ /\// && (q in B))
    if (!ok && cd != "") ok = there(norm(cd "/" q)) || there(norm((dir == "" ? "" : dir "/") cd "/" q))
    if (V) print "checked " (ok ? "ok " : "NO ") doc ":" line ": " p
    if (!ok) { print "MISSING " doc ":" line ": " p " (" kind ")"; bad++ }
  }
  END { exit bad > 0 }' "$LIST.cites" > "$LIST.out1" || true

# ---- every folder has a README.md: down to two levels below the root, a folder has one or is named in its parent's
# ("name/" or `name`); deeper, the nearest README above speaks for it; folders under planning/ need none
has() { [ "$1" = . ] && grep -qxF README.md "$LIST" || grep -qxF "$1/README.md" "$LIST"; }
{ echo .; cat "$LIST.dirs"; } | while IFS= read -r d; do
  case $d in planning|planning/*) continue;; esac
  has "$d" && continue
  depth=$(awk -F/ '{ print ($0 == "." ? 0 : NF) }' <<< "$d")
  if [ "$depth" -gt 2 ]; then
    a=$(dirname "$d"); ok=0
    while :; do has "$a" && { ok=1; break; }; [ "$a" = . ] && break; a=$(dirname "$a"); done
    [ $ok = 1 ] && continue
  elif [ "$d" != . ]; then
    pd=$(dirname "$d"); [ "$pd" = . ] && pr=README.md || pr=$pd/README.md
    n=$(basename "$d" | sed 's/[][\.*^$]/\\&/g')
    grep -qxF "$pr" "$LIST" && grep -qE "(^|[^A-Za-z0-9_.-])$n(/|\`)" "$pr" && continue
  fi
  echo "NO-README $d/"
done > "$LIST.out2"

# ---- documents named in source files' comments
grep -Ev '\.(md|gz|tgz|bin|sym|hex|log|out|csv|txt|trace|fp|dig|json|ref|sha256|patch|tb)$' "$LIST" | grep -Ev "$HIST" |
while IFS= read -r f; do
  awk -v F="$f" '{ s = $0; c = ""   # the line'"'"'s comment: from //, /*, a leading * or a # at a word'"'"'s start
      if (match(s, /\/\/|\/\*|^[ \t]*\*|(^|[ \t])#/)) c = substr(s, RSTART); else next
      while (match(c, /[A-Za-z0-9_.\/+-]*[A-Za-z0-9_+-]\.md([^A-Za-z0-9_]|$)/)) {
        t = substr(c, RSTART, RLENGTH); sub(/[^A-Za-z0-9_]$/, "", t); printf "%s\t%s\t%s\n", F, FNR, t; c = substr(c, RSTART + RLENGTH) } }' "$f" 2>/dev/null
done > "$LIST.named" || true
awk -F'\t' -v FILES="$LIST" -v EXF="$LIST.ex" '
  BEGIN { while ((getline l < FILES) > 0) { F[l] = 1; b = l; sub(/.*\//, "", b); B[b] = 1 }
          while ((getline l < EXF) > 0) { split(l, e, "\t"); EX[e[1] SUBSEP e[2]] = 1 } }
  { f = $1; p = $3; b = p; sub(/.*\//, "", b)
    if ((f SUBSEP p) in EX) next
    if (p ~ /^(\.\.\/)*v1\.[0-9.]+\// || b in B) next          # in the tree under that name, or another version'"'"'s
    print "DOC-MISSING " f ":" $2 ": " p }' "$LIST.named" > "$LIST.out3"

cat "$LIST.out1" "$LIST.out2" "$LIST.out3" | { if [ $V = 1 ]; then cat; else grep -v '^checked '; fi; }
n1=$(grep -c '^MISSING' "$LIST.out1" || true); n0=$(grep -c '^OUTSIDE' "$LIST.out1" || true)
n2=$(wc -l < "$LIST.out2"); n3=$(wc -l < "$LIST.out3")
echo "doccheck: $(wc -l < "$LIST.docs") documents, $(wc -l < "$LIST.cites") cited paths: $n1 MISSING, $n0 OUTSIDE; $((1 + $(wc -l < "$LIST.dirs"))) folders: $n2 NO-README; $(wc -l < "$LIST.named") documents named in source: $n3 DOC-MISSING"
[ $((n0 + n1 + n2 + n3)) = 0 ]
