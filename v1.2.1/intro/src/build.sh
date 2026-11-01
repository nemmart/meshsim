#!/bin/sh
# Build the introduction: the figures (svg/, rendered to png/), then Intro.pdf (one level above intro/).
# Needs python3 (standard library only, for the figures), cairosvg (for png/), pandoc and WeasyPrint (pip install cairosvg weasyprint).
set -e
cd "$(dirname "$0")"
for s in fig*.py; do python3 "$s"; done
mkdir -p ../png
python3 -c "import glob,os,cairosvg; [cairosvg.svg2png(url=f, write_to=os.path.join('../png', os.path.basename(f)[:-4]+'.png'), scale=2.0) for f in glob.glob('../svg/*.svg')]"
pandoc Intro.md --standalone --from markdown --to html5 --css intro.css --embed-resources \
       --metadata pagetitle="How the mesh sorter sorts" -o /tmp/intro-build.html 2>/dev/null \
  || pandoc Intro.md --standalone --from markdown --to html5 --css intro.css --self-contained \
       --metadata pagetitle="How the mesh sorter sorts" -o /tmp/intro-build.html
weasyprint /tmp/intro-build.html ../../Intro.pdf
echo "wrote $(cd ../.. && pwd)/Intro.pdf"
