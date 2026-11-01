#!/bin/sh
# Rebuild the figure set: every fig*.py writes its SVG into ../svg/, then each SVG is rendered to ../png/.
# Needs python3 and cairosvg (pip install cairosvg).
set -e
cd "$(dirname "$0")"
for s in fig*.py; do python3 "$s" > /dev/null; done
mkdir -p ../png
python3 - <<'PY'
import glob, os, cairosvg
for svg in sorted(glob.glob("../svg/*.svg")):
    png = os.path.join("../png", os.path.basename(svg)[:-4] + ".png")
    cairosvg.svg2png(url=svg, write_to=png, scale=2.0)
print("rendered", len(glob.glob("../png/*.png")), "PNGs")
PY
echo "diagrams: $(ls ../svg/*.svg | wc -l) SVGs in svg/, $(ls ../png/*.png | wc -l) PNGs in png/"
