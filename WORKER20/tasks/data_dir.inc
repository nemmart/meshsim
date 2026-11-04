# Persistent runner data (Quest PIPELINE.md, "Persistent data on the runner"): ~/data if this unit may write there, else
# the git-ignored data/ in the queue checkout (inside ReadWritePaths under ProtectHome=read-only). Never /tmp (wiped at boot).
DATA=$HOME/data
{ mkdir -p "$DATA" && touch "$DATA/.writable" && rm -f "$DATA/.writable"; } 2>/dev/null || DATA=$REPO/data
mkdir -p "$DATA" || { echo "no writable persistent data directory ($HOME/data, $REPO/data)"; exit 4; }
