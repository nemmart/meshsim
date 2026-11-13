#!/bin/bash
# tools/freeze/copy.sh DST [--dry-run]: the freeze's copy: sim/'s tracked files, less tools/freeze/exclude.txt's
# (ref/, oracle/, src/sketch/, the leftover benches, the workers' session material; reports and the index stay), into
# DST (normally the repository's v1.35/), byte for byte with their modes. A plain filtered copy: nothing is edited or
# added, so the tree the freeze check ran is the tree frozen. DST must not exist yet, and sim/ must have no uncommitted
# changes (the copy is of the freeze commit).
#   --dry-run   nothing is written: the counts, and every file left out with the line of exclude.txt that leaves it out
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
SIM=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -ge 1 ] || { echo "usage: tools/freeze/copy.sh DST [--dry-run]"; exit 2; }
DST=$1; DRY=0; [ "${2:-}" = --dry-run ] && DRY=1
cd "$SIM"
LIST=$(mktemp); trap 'rm -f "$LIST"' EXIT
tools/freeze/frozen.sh > "$LIST"
kept=$(wc -l < "$LIST"); all=$(git ls-files -- . | wc -l)
if [ $DRY = 1 ]; then
  echo "== left out ($((all - kept)) of $all tracked files), by the exclude.txt line that leaves each out:"
  git ls-files -- . | sort | comm -23 - "$LIST" | awk -v EX=tools/freeze/exclude.txt '
    BEGIN { while ((getline l < EX) > 0) { if (l ~ /^#/ || l ~ /^[ \t]*$/) continue; n++; keep[n] = (l ~ /^!/); re[n] = keep[n] ? substr(l, 2) : l } }
    { w = ""; for (i = 1; i <= n; i++) if ($0 ~ re[i]) w = keep[i] ? "" : re[i]; c[w]++; if (!(w in first)) first[w] = $0 }
    END { for (w in c) printf "  %5d  %-55s e.g. %s\n", c[w], w, first[w] }' | sort -k2
  echo "== kept: $kept files, by top-level folder:"; cut -d/ -f1 "$LIST" | sort | uniq -c | sed 's/^/  /'
  [ -e "$DST" ] && echo "note: $DST exists; the real run refuses it"
  [ -z "$(git status --porcelain -- .)" ] || echo "note: sim/ has uncommitted changes; the real run refuses them"
  exit 0
fi
[ ! -e "$DST" ] || { echo "copy.sh: $DST exists"; exit 1; }
[ -z "$(git status --porcelain -- .)" ] || { echo "copy.sh: sim/ has uncommitted changes (commit first: the copy is of the freeze commit)"; git status --short -- . | head; exit 1; }
mkdir -p "$DST"
tar -cf - --no-recursion -T "$LIST" | tar -xf - -C "$DST"
# every file the same bytes, and nothing more
(cd "$DST" && find . \( -type f -o -type l \) | sed 's|^\./||' | sort) | cmp -s - "$LIST" || { echo "copy.sh: $DST's files differ from the list"; exit 1; }
(xargs -d '\n' sha256sum < "$LIST") > "$LIST.a"; (cd "$DST" && xargs -d '\n' sha256sum < "$LIST") > "$LIST.b"
cmp -s "$LIST.a" "$LIST.b" || { echo "copy.sh: some file's bytes differ in $DST"; rm -f "$LIST.a" "$LIST.b"; exit 1; }
rm -f "$LIST.a" "$LIST.b"
echo "copy.sh: $kept files copied to $DST, each byte-identical ($((all - kept)) left out); at commit $(git rev-parse --short HEAD)"
