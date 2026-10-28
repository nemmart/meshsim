#!/bin/sh
# Redraw the worked examples' diagrams into ../svg/ (and render ../png/), then build Reference.pdf (at the release's top level)
# from sw/REGISTERS.md (pandoc -> HTML -> WeasyPrint).
# Usage: ./build_pdf.sh [in.md] [out.pdf]. Needs pandoc, python3 with weasyprint and cairosvg (README.md).
set -e
export PYTHONDONTWRITEBYTECODE=1
HERE=$(cd $(dirname $0) && pwd)
IN=${1:-$HERE/../../REGISTERS.md}; OUT=${2:-$HERE/../../../Reference.pdf}
BASE=$(cd $(dirname $IN) && pwd)
for e in "$HERE"/ex[0-9]_*.py; do (cd "$HERE" && python3 "$(basename $e)"); done
mkdir -p "$HERE/../png"
python3 -c "import glob,os,cairosvg; [cairosvg.svg2png(url=f, write_to=os.path.join('$HERE/../png', os.path.basename(f)[:-4]+'.png'), scale=2.0) for f in glob.glob('$HERE/../svg/*.svg')]"
T=$(mktemp -d)
pandoc "$IN" -f gfm -t html5 -s --metadata title="Tile registers" --css "$HERE/style.css" -o $T/base.html
python3 "$HERE/toc.py" $T/base.html $T/toc.html
python3 "$HERE/figs.py" $T/toc.html $T/regs.html "$BASE" $T
python3 -c "import weasyprint,sys; weasyprint.HTML(sys.argv[1], base_url=sys.argv[2] + '/').write_pdf(sys.argv[3])" $T/regs.html "$BASE" "$OUT"
rm -rf $T
echo "wrote $OUT"
