# regdoc: REGISTERS.md's worked-example diagrams and its PDF

The files for `../REGISTERS.md`'s worked examples and its PDF. They live here, beside the document, rather than in `../../diagrams/`, the figure set.

- `svg/`: the five worked-example diagrams, `ex1_local_copy.svg` … `ex5_bucket_pass.svg` (REGISTERS.md 7.1–7.5); `png/`: the same, rendered at twice their size.
- `src/examples_tile.py`: the worked-example template, figure 1.3's tile with a top band for the controller, its memories and the router. An example names the units and paths it uses (drawn in colour, the rest faint), numbers its register stores and letters its data-flow steps. It imports `common.py` from `../../diagrams/src/` for the set's colours and line styles, and reads each example's program from its `<!-- program: exN -->` block in `../REGISTERS.md`, so the diagram and the document can't disagree.
- `src/ex1_local_copy.py` … `src/ex5_bucket_pass.py`: one script per example.
- `src/build_pdf.sh`: redraws the five diagrams into `svg/` and `png/`, then builds `../../Reference.pdf` (the release's top level) from `../REGISTERS.md` with pandoc and WeasyPrint (`style.css`, `toc.py` for the contents with page numbers). `figs.py` turns each SVG into a 3,900-pixel PNG with cairosvg first, because WeasyPrint ignores SVG marker orientation and would draw every arrowhead pointing right.

Needs `pandoc`, and Python 3 with `weasyprint` and `cairosvg` (`pip install weasyprint cairosvg`). Run `src/build_pdf.sh` from anywhere.
