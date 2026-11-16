#!/bin/bash
# tools/freeze/frozen.sh [--untracked]: the files of the frozen copy, one path per line relative to sim/: the tracked
# files (git ls-files; with --untracked also the untracked ones git doesn't ignore) less tools/freeze/exclude.txt's.
# Run from anywhere; works in sim/ and in a copy of it that is its own git checkout or plain folder (then every file).
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
cd "$(dirname "$0")/../.."
if git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
  if [ "${1:-}" = --untracked ]; then git ls-files --cached --others --exclude-standard -- .; else git ls-files -- .; fi
else find . -type f ! -path './build/*' ! -path './results/*' | sed 's|^\./||'; fi | sort -u | while IFS= read -r f; do [ -e "$f" ] && printf '%s\n' "$f"; done |
awk -v EX=tools/freeze/exclude.txt '
  BEGIN { while ((getline l < EX) > 0) { if (l ~ /^#/ || l ~ /^[ \t]*$/) continue; n++; keep[n] = (l ~ /^!/); re[n] = keep[n] ? substr(l, 2) : l } }
  { out = 0; for (i = 1; i <= n; i++) if ($0 ~ re[i]) out = !keep[i]; if (!out) print }'
