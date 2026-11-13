#!/usr/bin/env bash
# run.sh -- the sort's measurements (P4), the shuffled-order check and the whole-sort sweep, on meshsim v1.35
# P6, row D8): v1.3's kit/run.sh on the C++ driver (sort_test) and SortSummary (sort_summary); no Java (D40).
#
#   ./run.sh [-j JOBS] [-m HEAP] [-o OUTDIR] [-n KEYS_PER_TILE] [-g GROUPS] [-f FILTER] [-J JOBFILE] [-s]
#     -j  parallel runs (default 12)                   -m  accepted for v1.3's command lines, unused (no JVM)
#     -o  results directory (default results)          -n  keys per tile (default 65536; 2000 for a smoke test)
#     -g  groups: any of p4,shuffle,sweep (default all three)
#     -f  only the runs whose id contains FILTER (e.g. -f p4/trace)    -J  another job file (default jobs.txt)
# Each run writes OUTDIR/<id>.csv (one row per step and a TOTAL row), <id>.phases.csv (every C&C phase) and <id>.log. A
# run whose CSV exists is skipped, so the script can be stopped and restarted. At the end, sort_summary writes
# OUTDIR/SUMMARY.md and OUTDIR/all.csv (also: ./run.sh -s to summarize only).
# Each log starts with the run's inputs line (the SHA-256 of sort_test, the boot ROM, the image, hops.txt, the keys);
# SUMMARY.md's Inputs section flags runs whose files differ from HASHES or from each other.
# The key sets (keys-N/<set>.keys) come prebuilt (assemble.sh KEYDIR, or copied in); a missing one is made with
# pipeline/keys' KeyGen only if a JDK is present (owner's Q4).
set -e
cd "$(dirname "$0")"
J=12; OUT=results; N=65536; SEL=p4,shuffle,sweep; FILTER=; SUMMARY_ONLY=; JOBS=jobs.txt
while getopts j:m:o:n:g:f:J:s opt; do
  case $opt in j) J=$OPTARG;; m) ;; o) OUT=$OPTARG;; n) N=$OPTARG;; g) SEL=$OPTARG;; f) FILTER=$OPTARG;; J) JOBS=$OPTARG;; s) SUMMARY_ONLY=1;;
    *) sed -n '2,12p' "$0"; exit 1;; esac
done
mkdir -p "$OUT"
if [ -z "$SUMMARY_ONLY" ]; then
  echo "meshsim v1.35 kit: sort_test $(sha256sum sort_test | cut -c1-12)..."
  KEYS=keys-$N
  # 1. the job list, filtered, longest first (jobs.txt is already in that order), without the finished ones
  grep -v '^#' $JOBS | awk -F'\t' -v g=",$SEL," -v f="$FILTER" 'index(g, "," $2 ",") && (f == "" || index($3, f))' > "$OUT/.queue"
  # 2. the key sets the selected runs need: prebuilt; made with KeyGen only where a JDK is present
  NEED=$(cut -f5 "$OUT/.queue" | grep -o '@KEYS@/[a-z0-9-]*\.keys' | sed 's|@KEYS@/||; s|\.keys||' | sort -u | while read s; do [ -f $KEYS/$s.keys ] || echo $s; done | paste -sd, -)
  if [ -n "$NEED" ]; then
    if command -v javac > /dev/null && command -v java > /dev/null; then
      echo "== generating key sets into $KEYS with pipeline/keys' KeyGen (a JDK is present): $NEED"
      K=$(mktemp -d); javac -nowarn -d "$K" pipeline/keys/KeyGen.java && java -Xmx2g -cp "$K" KeyGen $KEYS --n $N --only "$NEED"; rm -rf "$K"
    else
      echo "== missing key sets in $KEYS: $NEED (copy them in, or install a JDK to make them with pipeline/keys)"; exit 1
    fi
  fi
  # 3. the kit's files and the key sets present against HASHES, the reference set: a warning, not a stop
  if ! { grep -v '^#' HASHES | while read -r h f; do [ -e "$f" ] && echo "$h  $f"; done | sha256sum -c --quiet; }; then
    echo "== WARNING: files differ from HASHES (results will not be comparable); continuing: SUMMARY.md will flag the runs"
  else echo "== inputs: $(grep -v '^#' HASHES | while read -r h f; do [ -e "$f" ] && echo x; done | wc -l) files match HASHES"; fi
  TODO=$(awk -F'\t' -v o="$OUT" '{ id = $3; gsub("/", "_", id); if (system("test -f " o "/" id ".csv") != 0) print }' "$OUT/.queue")
  NT=$(printf '%s\n' "$TODO" | grep -c . || true)
  echo "== $NT runs to do ($(grep -c . "$OUT/.queue") selected), $J at a time; logs in $OUT/"
  if [ "$NT" -gt 0 ]; then
    printf '%s\n' "$TODO" | sed "s|@KEYS@|$KEYS|g; s|@NH@|$((N / 2))|g; s|@N@|$N|g" | \
      tr '\n' '\0' | xargs -0 -P "$J" -I{} tools/runjob.sh "$OUT" - "{}"
  fi
fi
./sort_summary "$OUT"
