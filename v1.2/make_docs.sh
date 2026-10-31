#!/bin/sh
# Rebuild every generated document: the figure set (diagrams/svg, diagrams/png), the register reference's examples
# and Reference.pdf, and the introduction's figures and Intro.pdf. See BUILDING.md for what each step needs.
set -e
cd "$(dirname "$0")"
echo "== diagrams";        sh diagrams/src/build.sh
echo "== Reference.pdf";   sh sw/regdoc/src/build_pdf.sh
echo "== Intro.pdf";       sh intro/src/build.sh 2>&1 | grep -v "^WARNING: Using fontTools" || true
echo "== done"
