#!/bin/bash
# fetch.sh [DIR]: jolt-asic-planning at the pinned commit (pins.sh) into DIR (default $OUT/src/jolt-asic-planning), checked.
# A private clone takes the token from $GH_TOKEN, passed to each git command as an HTTP header (git -c): it is never
# written to a file. JAP_COMMIT=<hash> overrides the pin (the fallback, 8d546e3).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/pins.sh"
OUT=${OUT:-$HOME/meshsim-inputs}
D=${1:-$OUT/src/jolt-asic-planning}
g() {  # git, with the token (if any) as a header for this command only
  if [ -n "${GH_TOKEN:-}" ]; then
    git -c "http.https://github.com/.extraheader=AUTHORIZATION: basic $(printf 'x-access-token:%s' "$GH_TOKEN" | base64 -w0)" "$@"
  else git "$@"; fi
}
if [ ! -d "$D/.git" ]; then
  mkdir -p "$(dirname "$D")"
  g clone -q --filter=blob:none --no-checkout "$JAP_URL" "$D"
fi
g -C "$D" cat-file -e "$JAP_COMMIT^{commit}" 2>/dev/null || g -C "$D" fetch -q origin
g -C "$D" -c advice.detachedHead=false checkout -q --force "$JAP_COMMIT"
[ "$(git -C "$D" rev-parse HEAD)" = "$JAP_COMMIT" ] || { echo "fetch: $D is not at $JAP_COMMIT"; exit 2; }
[ -z "$(git -C "$D" status --porcelain -- tracer)" ] || { echo "fetch: $D/tracer has local changes"; exit 2; }
echo "jolt-asic-planning $JAP_COMMIT at $D"
