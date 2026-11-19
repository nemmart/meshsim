#!/bin/sh
# check_copies.sh: every file v1.35 copied from v1.3 (sim/COPIES.sha256: sha256, path in sim/, path in
# sim/ref) still has v1.3's bytes. Checks the sim/ side only; the same hashes are checked against sim/ref from oracle/.
set -eu
cd "$(dirname "$0")/.."
grep -v '^#' COPIES.sha256 | awk '{print $1 "  " $2}' | sha256sum -c --quiet - || { echo "check_copies: FAILED"; exit 1; }
echo "check_copies: $(grep -vc '^#' COPIES.sha256) files equal to v1.3's"
# v1.37: the link, the wire and the frames' gold, copied from ../updater_wip (FROM-UPDATER.sha256), so the folder stands alone
grep -v '^#' FROM-UPDATER.sha256 | awk '{print $1 "  " $2}' | sha256sum -c --quiet - || { echo "check_copies: FROM-UPDATER FAILED"; exit 1; }
echo "check_copies: $(grep -vc '^#' FROM-UPDATER.sha256) files equal to ../updater_wip's at their copy"
