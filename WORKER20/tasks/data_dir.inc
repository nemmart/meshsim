# Persistent runner data (Quest PIPELINE.md, "Persistent data on the runner"): ~questrunner/data (owner), writable once the
# unit's ReadWritePaths lists it; else the git-ignored data/ in the queue checkout. Never /tmp (wiped at boot).
DATA=$HOME/data
{ mkdir -p "$DATA" && touch "$DATA/.writable" && rm -f "$DATA/.writable"; } 2>/dev/null || DATA=$REPO/data
mkdir -p "$DATA" || { echo "no writable persistent data directory ($HOME/data, $REPO/data)"; exit 4; }
