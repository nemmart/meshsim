#!/usr/bin/env bash
# make_release.sh -- WORKER25's PROPOSAL for v1.26.1/ (not run by WORKER25: freezing is the owner's and integrator's).
# v1.26.1/ = v1.26/ plus exactly this branch's changes in wip/ (DECISIONS-v1.26 section 3), plus DECISIONS-v1.26.md as
# it now stands (items 13-17). Everything wip/ gained since v1.26 froze that belongs to v1.3 stays out, as v1.26 left it
# out: gold/, tracer/, DECISIONS-v1.3.md, INTEGRATOR-HANDOVER.md, WORKER17's session files and WORKER24's Tracer figures
# (diagrams/README.md, src/common.py, fig9_*: v1.26's diagrams/ is kept unchanged).
#   WORKER25/tasks/make_release.sh   (from the repo root, on main after v1.26.1 is merged)
set -eu
[ -d v1.26 ] && [ ! -e v1.26.1 ] || { echo "needs v1.26/ and no v1.26.1/"; exit 1; }
cp -a v1.26 v1.26.1
git rm -q --cached -r v1.26.1 2>/dev/null || true
rm -f v1.26.1/spec/CHANGES-v1.25-to-v1.26.md          # renamed and extended: CHANGES-v1.25-to-v1.26.1.md
for f in \
  meshsim/acceptance/make_task.sh \
  meshsim/acceptance/run_shard.sh \
  meshsim/src/MeshHeader.java \
  meshsim/src/RegDoc.java \
  meshsim/src/Replayer.java \
  meshsim/sw/mesh.h \
  meshsim/sw/replay/replay_ctl.c \
  meshsim/sw/replay/replay_layout.h \
  meshsim/test/ConvertDelivery.java \
  meshsim/test/PeripheralSlotTest.java \
  meshsim/test/ReplayDriver.java \
  meshsim/test/ReplayWindow.java \
  meshsim/test/ReplayerUnitTests.java \
  meshsim/test/StageB.java \
  meshsim/test/SyntheticDelivery.java \
  spec/CHANGES-to-v1.25.md \
  spec/CHANGES-v1.25-to-v1.26.1.md \
  spec/CHANGES-v1.26.1.md \
  spec/CHANGES-v1.26.md \
  spec/machine-spec.md \
  sw/RECIPES.md \
  sw/REGISTERS.md \
  sw/mesh-v1.2-draft.h \
  sw/mesh.h \
  sw/recipes/RECIPES.tmpl.md \
  sw/recipes/rcp_replay.c \
  planning/DECISIONS-v1.26.md; do
  mkdir -p "v1.26.1/$(dirname "$f")"; cp -a "wip/$f" "v1.26.1/$f"
done
# check: every file of v1.26.1/ outside diagrams/ and planning/sessions/ equals wip/'s
bad=0
(cd v1.26.1 && find . -type f ! -path './diagrams/*' ! -path './planning/sessions/*') | while read -r f; do
  cmp -s "v1.26.1/$f" "wip/$f" || { echo "DIFFERS from wip: $f"; bad=1; }
done
git add v1.26.1
echo "v1.26.1/ staged: $(git diff --cached --name-only | wc -l) files. Then: the top README's row; v1.26/ deleted (DECISIONS-v1.26 s3)."
