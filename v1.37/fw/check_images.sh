#!/bin/bash
# fw/check_images.sh [PART...]: the committed firmware images against the hash lists committed beside them, for a
# machine without the RISC-V toolchain, where fw/build.sh --check can't run. Parts (none named: all):
#   images   fw/images/, the images fw/build.sh builds (the boot ROM, the tile programs, the sort's tile image, the
#            16-record runs' keys, the controllers', the replay controller and the tracer tile's program), against
#            fw/images/MANIFEST
#   core     tests/core-programs/elf/, the core's own programs, against tests/core-programs/SHA256SUMS
#   runs     the runs' keys, with FWIMAGE=path/to/fwimage (tools/fwimage; `make` builds it): the committed
#            sort.keys is the generator's output. Without FWIMAGE the part is not checked, and says so.
# In each folder every listed file must be there with its sha256, and no file may be there that its list doesn't name.
# Exit 1 on any difference, missing file or missing hash.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs: the output is the same in any locale
FW=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$FW/.." && pwd)
PARTS=${*:-images core runs}
for p in $PARTS; do case $p in images|core|runs) ;; *) echo "usage: fw/check_images.sh [images] [core] [runs]"; exit 2;; esac; done
bad=0; ok=0

hashes() {   # hashes NAME LIST DIR FOLDERS...: LIST's files in DIR, and no file under DIR's FOLDERS that LIST doesn't name
  local name=$1 list=$2 dir=$3 want f; shift 3
  [ -f "$list" ] || { echo "MISSING: $name's hash list"; bad=1; return; }
  while read -r want f; do
    case $want in '#'*|'') continue ;; esac
    if [ ! -f "$dir/$f" ]; then echo "MISSING: $name/$f"; bad=1
    elif [ "$(sha256sum < "$dir/$f" | cut -d' ' -f1)" != "$want" ]; then echo "DIFFERS: $name/$f against its listed sha256"; bad=1
    else ok=$((ok + 1)); fi
  done < "$list"
  for f in $(cd "$dir" && find "$@" -type f ! -name MANIFEST | sed 's|^\./||' | sort); do
    [ -n "$(awk -v n="$f" '$1 ~ /^[0-9a-f]{64}$/ && $2 == n { print $1 }' "$list")" ] || { echo "NOT LISTED: $name/$f has no hash in its list"; bad=1; }
  done
}

for p in $PARTS; do
  case $p in
    images) hashes fw/images "$FW/images/MANIFEST" "$FW/images" . ;;
    core)   hashes tests/core-programs/elf "$SIM/tests/core-programs/SHA256SUMS" "$SIM/tests/core-programs/elf" . ;;
    runs)
      if [ -z "${FWIMAGE:-}" ]; then echo "fw/check_images.sh: runs: not checked (FWIMAGE names no fwimage: make, then FWIMAGE=build/clang-release/fwimage)"; continue; fi
      T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
      "$FWIMAGE" keys 16 "$T/sort.keys"
      if cmp -s "$T/sort.keys" "$FW/images/sort/sort.keys"; then ok=$((ok + 1)); else echo "DIFFERS: fw/images/sort/sort.keys against the generator's keys"; bad=1; fi
      ;;
  esac
done
[ $bad = 0 ] || { echo "fw/check_images.sh ($PARTS): FAILED"; exit 1; }
echo "fw/check_images.sh ($PARTS): $ok committed files equal to their hash lists and to what is made here"
