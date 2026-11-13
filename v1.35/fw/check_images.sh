#!/bin/bash
# fw/check_images.sh [--built DIR] [PART...]: the committed firmware images of fw/build.sh's parts (boot tests m1 m3 cc
# recipes replay tracer sort; none named: all) against the hash lists committed beside them, for a machine without the
# RISC-V toolchain, where fw/build.sh --check can't run. Each image is found where fw/build.sh --check looks for it
# (fw/boot, the fixture folders tests/golden/images, tests/golden-m2/images and tests/m3-images, else fw/images) and its
# sha256 must equal the one its folder lists (fw/images/MANIFEST; the fixture folders' MANIFEST, MANIFEST.md and
# REQ_MANIFEST; COPIES.sha256 for the boot ROM). Exit 1 on any difference, missing image or missing hash.
#   --built DIR   after a real build (fw/build.sh --check leaves its images in fw/build/out): also check that the
#                 images named here for the parts are exactly the images the build made, so the names below stay true.
# The images' names follow fw/build.sh's parts: change one with the other.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
FW=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$FW/.." && pwd)
BUILT=; [ "${1:-}" = --built ] && { BUILT=$2; shift 2; }
PARTS=${*:-boot tests m1 m3 cc recipes replay tracer sort}

names() {   # names PART: the images fw/build.sh makes for it, relative to its output folder
  local n s
  [ "$1" = boot ] && { echo bootrom.hex; return; }
  case $1 in
    tests)   for s in "$FW"/tests/*.c; do n=$(basename "$s" .c); case $n in rcp_*) ;; *) echo "$n";; esac; done ;;
    m1)      for s in "$FW"/m1/*.S; do basename "$s" .S; done ;;
    m3)      echo cc_ping; echo cc_req; echo tile_req ;;
    cc)      echo cc1; echo cc23; echo cc_rcp; echo cc_bad ;;
    recipes) for s in "$FW"/recipes/rcp_*.c; do basename "$s" .c; done ;;
    replay)  echo replay_ctl; echo log8k/replay_ctl; echo oldmap/replay_ctl ;;
    tracer)  echo tracer_ctl ;;
    sort)    echo sort ;;
  esac | while read -r n; do printf '%s\n' "$n.code.bin" "$n.data.bin" "$n.sym"; done
}

listed() {   # listed FOLDER NAME: the sha256 FOLDER's hash list gives NAME (empty if none)
  if [ "$1" = fw/boot ]; then awk -v p="fw/boot/$2" '$2 == p { print $1 }' "$SIM/COPIES.sha256"; return; fi
  local f
  for f in MANIFEST MANIFEST.md REQ_MANIFEST; do
    [ -f "$SIM/$1/$f" ] && awk -v n="$2" '$1 ~ /^[0-9a-f]{64}$/ && $2 == n { print $1 }' "$SIM/$1/$f"
  done | head -1
}

for p in $PARTS; do case $p in boot|tests|m1|m3|cc|recipes|replay|tracer|sort) ;;
  *) echo "fw/check_images.sh: no part '$p' (boot tests m1 m3 cc recipes replay tracer sort)"; exit 2;; esac; done
bad=0; ok=0; all=
for p in $PARTS; do
  for f in $(names "$p"); do
    all="$all $f"; where=; f2=$f
    case $f in
      bootrom.hex) where=fw/boot ;;
      *) for d in tests/golden/images tests/golden-m2/images tests/m3-images; do
           [ -f "$SIM/$d/$f" ] && { where=$d; break; }; done
         [ -n "$where" ] || where=fw/images ;;
    esac
    want=$(listed "$where" "$f2")
    if [ ! -f "$SIM/$where/$f2" ]; then echo "MISSING: $where/$f2"; bad=1
    elif [ -z "$want" ]; then echo "NOT LISTED: $where/$f2 has no hash in $where's list"; bad=1
    elif [ "$(sha256sum < "$SIM/$where/$f2" | cut -d' ' -f1)" != "$want" ]; then echo "DIFFERS: $where/$f2 against its listed sha256"; bad=1
    else ok=$((ok + 1)); fi
  done
done
if [ -n "$BUILT" ]; then
  a=$(printf '%s\n' $all | sort); b=$(cd "$BUILT" && find . -type f | sed 's|^\./||' | sort)
  [ "$a" = "$b" ] || { echo "the images named for '$PARTS' differ from those built in $BUILT:"; diff <(echo "$a") <(echo "$b") || true; bad=1; }
fi
[ $bad = 0 ] || { echo "fw/check_images.sh ($PARTS): FAILED"; exit 1; }
echo "fw/check_images.sh ($PARTS): $ok committed images equal to their hash lists${BUILT:+, and the same names as the build made}"
