# How the mesh sorter sorts: the introduction's sources

`../Intro.pdf` is a readable walk-through of the sort for engineers new to the project: the four steps on a toy example, (key, ordinal) records and ties, the six steps at full scale (the local sort with count-on-arrival, the gathers, the split, the 16-group all-to-all), skewed data, and the measured numbers. The spec (`../spec/machine-spec.md`) and `../sw/` are authoritative wherever the introduction simplifies.

## Build

```
cd intro/src && ./build.sh
```

This regenerates every figure into `svg/`, renders each to `png/` (with cairosvg) and writes `../Intro.pdf` (at the top of the release). It needs `python3` (standard library only, for the figures), `pandoc` (tested with 3.1) and WeasyPrint (`pip install weasyprint`; tested with 70.0). Fonts: Liberation Sans / Helvetica.

## Contents

| path | what |
|---|---|
| `src/Intro.md` | the text (pandoc Markdown); images link to `../svg/` |
| `src/intro.css` | the print style |
| `src/build.sh` | the build |
| `src/common.py` | the figure helpers: a copy of `diagrams/src/common.py` (palette, fonts, primitives), writing into `svg/`, with explicit arrowheads in place of SVG markers (PDF renderers orient markers differently) and a few helpers of its own |
| `src/toy.py` | the toy example every figure draws from: 4 tiles × 8 keys, the (key, tile, position) order, the slices, the ordinals, and the split run as `sort/split/split.c` runs it (seed bracket, bisection, tie step), with checks that the cuts match the ranks |
| `src/fig*.py` | one script per figure (below) |
| `svg/` | the figures |
| `png/` | the same figures rendered at twice their size (for slides and viewers without SVG) |

| figure | file | script |
|---|---|---|
| 1. Sorting 32 keys on 4 tiles | `fig01-toy-sort.svg` | `fig01_toy.py` |
| 2. The same sort, as (key, ordinal) records | `fig02-toy-records.svg` | `fig02_records.py` |
| 3. Augmenting | `fig03-augmenting.svg` | `fig03_augment.py` |
| 4. How each key finds its value | `fig04-gather.svg` | `fig04_gather.py` |
| 5. The six steps | `fig05-six-steps.svg` | `fig05_steps.py` |
| 6. The local sort on one tile | `fig06-radix-16keys.svg` | `fig06_radix.py` |
| 7. One bucket pass on the tile | `fig07-bucket-pass.svg` | `fig07_bucket.py` |
| 8. The split, round by round | `fig08-split-rounds.svg` | `fig08_split.py` |
| 9. Round 4 of the split, close up | `fig09-split-round.svg` | `fig08_split.py` |
| 10. The mesh and group 12 | `fig10-mesh-routes.svg` | `fig10_mesh.py` |
| 11. The all-to-all as a group run | `fig11-a2a-timeline.svg` | `fig11_timeline.py` |
| 12. The skew switch and the four paths | `fig12-skew-switch.svg` | `fig12_skew.py` |

Each figure carries its own number, title and caption, so the PDF hides pandoc's captions (the Markdown alt text describes each figure).

## The numbers

Every number in sections 3–5 is from the v1.0 full-size measurement (`../results/full-size/SUMMARY.md`). Section 4's "direct would take" column is [model] (`sort/skew/skew-v2.md` §0).

## Choices made while writing

- **32-bit values:** no augmenting, no gathers, the keys all-to-all in mode 2 (the lead's ruling; the spec's §14 says steps 2 and 6 disappear, and is to say the rest explicitly).
- **LB** is named as the spec names it, "loopback as a local copy", and explained as the offset-0 stream moved out of group 15 into its own one-stream group at full rate (`skew-v2.md` §2.2 describes the same mechanism).
- **L2** is called a local sort, as in spec §14 (`sort/program/README.md` calls it "the final local merge").
- **The output format** is not described beyond "keys sorted, each value in its key's place"; it is open (§13, item 13), and the text cites it.
- **The split's cost** is the measured step, 208.9K on uniform keys, with the search alone about 200.7K (barriers and the next step's code load are the rest). Spec §14 quotes ≈ 203K from the RV32 instruction-set simulator; the text uses the measured cycle-level numbers.
- **The seed bracket's width limit** (2^26^) is `split.c`'s `SEED_T`; the toy, at 4 tiles, samples at positions *q* · *n* / 4 and always uses its bracket.
- **Figure 3's arrival order** is an illustration of interleaving (one record per sender per round), not the schedule's exact order at 4 tiles.
- **R ≈ 1,100** on uniform keys is from the measured keys all-to-all (71.7K ≈ 64 × R).
- **Stream numbers:** figure 11 writes the group's streams as `STREAM[s … s + 3]`; the program's numbering isn't part of the spec.
