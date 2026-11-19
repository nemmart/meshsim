# The gold and jumpmap sources lib/classes.sh builds (WORKER19, P1), committed here in place of v1.3's git archive of
# the meshsim repository at pins.sh's GOLD_SRC and GOLD_V13_SRC: the files are byte-identical to `git archive PIN
# wip/gold/src wip/jumpmap/src` (SNAPSHOTS.sha256), and the classes built from them equal the git-archive build's.
#   d97ecde/gold/src, d97ecde/jumpmap/src   GOLD_SRC d97ecdebfe8a8fc5f7f53ac5cc9d517454f9dfb9: the v1.2 segment files
#                                           and jumpmap's outputs (gold's last commit 7ef9039, jumpmap's 4788e0e)
#   eeb64b5/gold/src                        GOLD_V13_SRC eeb64b5c10efc50965f255d208fa5d6715a42dfe: the v1.3 file
# Sourced by lib/classes.sh and build.sh: the last commits that git log gave at the pins (VERSION, GOLD_COMMIT).
GOLD_LAST=7ef9039eb1a51c0ea9c335d7251285c7f2099889
JUMPMAP_LAST=4788e0ee14ceb30268b66c2c2d3db17f252356ee
GOLD_V13_LAST=eeb64b5c10efc50965f255d208fa5d6715a42dfe
