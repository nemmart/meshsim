#!/bin/bash
# fw/check_images.sh [rtl]: the RTL's firmware vendored in fw/rtl, against its hash list, fw/rtl/MANIFEST: every source
# and image it lists must be there with its sha256, and fw/rtl's folders src, boot, tile and sort must hold no file it
# doesn't list (fw/rtl/README.md; fw/rtl/refresh.sh writes the list). These images are built by Snarky One's toolchain,
# never here. Exit 1 on any difference, missing file or missing hash. (`rtl` is the one part there is; v1.35's parts,
# meshsim's own firmware, are not in v1.36.)
set -eu
export LC_ALL=C   # byte order for sort, ls and globs: the output is the same in any locale
FW=$(cd "$(dirname "$0")" && pwd)
[ $# = 0 ] || [ "$*" = rtl ] || { echo "usage: fw/check_images.sh [rtl]"; exit 2; }
bad=0; ok=0
m="$FW/rtl/MANIFEST"
[ -f "$m" ] || { echo "MISSING: fw/rtl/MANIFEST"; echo "fw/check_images.sh: FAILED"; exit 1; }
while read -r want f; do
  case $want in '#'*|'') continue ;; esac
  if [ ! -f "$FW/rtl/$f" ]; then echo "MISSING: fw/rtl/$f"; bad=1
  elif [ "$(sha256sum < "$FW/rtl/$f" | cut -d' ' -f1)" != "$want" ]; then echo "DIFFERS: fw/rtl/$f against its listed sha256"; bad=1
  else ok=$((ok + 1)); fi
done < "$m"
for f in $(cd "$FW/rtl" && find src boot tile sort -type f 2> /dev/null | sort); do
  [ -n "$(awk -v n="$f" '$1 ~ /^[0-9a-f]{64}$/ && $2 == n { print $1 }' "$m")" ] ||
    { echo "NOT LISTED: fw/rtl/$f has no hash in fw/rtl/MANIFEST"; bad=1; }
done
[ $bad = 0 ] || { echo "fw/check_images.sh: FAILED"; exit 1; }
echo "fw/check_images.sh: $ok files of fw/rtl equal to their hash list"
