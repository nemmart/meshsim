# WORKER30 report: P8's tooling for the freeze

Branch `sim-w30`. Scope: the docs check, the freeze's copy, the comment strip (dry run), the Java-format helper switch,
the firmware build steps, and the fixes found on the way. Nothing here is a document of v1.35 but the new tools folder's
README (the docs session owns the documents).

## What v1.35 has now

| what | where | checked by |
|---|---|---|
| `sw-build`, `sort-build` live (the owner's ruling C): `fw_part` builds and compares with the toolchain; without it, the committed images against the hash lists beside them | `run_tests.sh` (`fw_part`), `fw/check_images.sh` | both paths run; a changed image fails; with the toolchain, the check's image names equal the build's for every part (157 images) |
| the locale: `export LC_ALL=C` where an output depends on collation (nm's symbol order, sorted manifests, globs) | `fw/build.sh`, `tests/stageb/replay_build.sh`, `tests/m3-images/build.sh`, `build_req.sh`, `tests/golden/build_images.sh`, `pipeline/inputs/build.sh`, `record.sh`, `compare_runner.sh`, `tests/acceptance/make_kit.sh`, the tools here | under en_US.UTF-8, without it `wfi.sym` differs (Quests 187/188); with it all 157 images equal. The committed pipeline manifests are already in C order |
| the bus step on a committed fixture; `run_tests.sh` reads nothing in `ref/` | `tests/p2suites/bus.log` (v1.3's `results/bus.log`, unchanged), PROVENANCE | quick --check |
| the documents' checks (proposal section 6): cited paths exist, no sim/, ref/, oracle/, wip/ paths, a README in every folder, every document a comment names in the tree; run on the frozen copy's files | `tools/doccheck.sh`, `make doccheck`, in `make check` with `DOCCHECK=1` (`tools/doccheck.mk`) | baseline: `sessions/WORKER30/doccheck-baseline.txt` (the docs-writing session's work list) |
| the freeze's copy: a plain filtered copy, byte for byte | `tools/freeze/copy.sh`, `frozen.sh`, `exclude.txt` | the copy test (below) |
| the comment strip, the messages, the bench renames: a dry run with its proof | `tools/freeze/strip.sh`, `cstrip.cpp`, `strip-rules.txt`, `messages.txt`, `keep.txt` | the dry run (below) |
| mesh.h's and REGISTERS.md's banners describe v1.35 | `tools/regs/regs_gen.h`, regenerated; out of COPIES.sha256 (reason recorded) | every `#define` (211) and table row (138) as v1.3's; test_regs; regdoc's log equal to v1.3's; `fw/build.sh --check`; `oracle/regs_crosscheck.sh` compares after the banners (not run: no JDK) |
| `tracer_rig.cpp` on `tests/java_format.h` | | `sessions/WORKER30/helper_diff.cpp`: equal on every value of the call site's domain (0 to 100) |

Every push: clang release and gcc debug, every test (`test_p4suites` and `test_sort_suites` case by case under the
sandbox's limit), `run_tests.sh quick --check` (all passed).

## The copy test

`tools/freeze/copy.sh /tmp/v135` at 44eb12dd: 1,847 files, each byte-identical, 894 left out. Inside the copy (no ref/,
no oracle/, no git): bench_sort, bench_chip, trace_filter, tracer_play and bench_threads build, and
`run_tests.sh quick --check` passes (all passed, no LOG DIFFERS). Since then `spec/check/` is left out too (it reads
v1.3/), and copy.sh refuses a destination without README.md.

## The strip's dry run

`tools/freeze/strip.sh` (dry run into /tmp/strip-dry; `--apply` in sim/ at P8's end; `--make-check` adds make check and
quick --check):
- **renames**: bench/w10's kept benches to bench/sort/ (w10.mk to sort.mk), bench/w14 to bench/tracer/ (tracer.mk);
  every path to them outside planning/ follows (Makefile, the freeze task's body, bench/README.md, the make files);
- **comments**: 329 files, 288 changed, about 1,230 lines: cstrip's `@untag` pass (tags in parentheticals with their
  planning codes, leading tags, possessives, bare tags) and 60 hand-reviewed entries (keyed by exact text: the run
  stops if one is no longer found). Only comments change; no line is added or removed (so `__LINE__` is unchanged);
  `#define W0`, `"W0-W2"`, raw strings, char literals and continued comments are safe by the lexer;
- **messages**: 17 edits in 10 files (FATAL texts, usage and step descriptions); text in a fixture or a v1.3 output
  stays (keep.txt: `W4-02` is in tests/mach/checkself.out; ConvertDelivery's "by" field is in v1.3's outputs);
- **proof**: 166 translation units, each preprocessed in the debug, release and suite builds' defines: identical but
  the 8 that include a messages.txt file; the firmware images byte-identical (fw/build.sh --check); every changed
  shell script parses. `--make-check` not run yet (the real run should use it).
- **left** (7 comment lines, for the owners): `config_impl.h:1` and `stream_word.h:1` (a tag mid-sentence across
  lines), `tracer_rig.h:2`; three stale status lines (`suite_ag.cpp:8`, `suite_engine.cpp:16`, `suite_net.cpp:15`: work
  they wait for is done); `tests/m3-images/build.sh:3` (below).

## Findings

- **F1.** `tests/m3-images/build.sh` reads `planning/sessions/WORKER13-tasks/IMAGES`, which the copy leaves out: the
  script fails in v1.35. Its image list should move beside the images.
- **F2.** The pipeline's files are held to v1.3's bytes (`pipeline/FROM-V1.3.sha256`, the snapshots under
  `pipeline/inputs/src/`): the strip leaves them alone. Their comments citing earlier-series notes
  (`wip/planning/sessions/WORKER18-NOTES.md` and others, doccheck's DOC-MISSING lines) stay unless they are unpinned as
  COPIES.sha256's documents were.
- **F3.** Identifiers named after a worker (`struct W26`, `load_w26` in tests/chain*) are code, outside rule 4: kept.
- **F4.** `tests/freeze/make_task.sh` (sorted HASHES; `planning/sessions/WORKER16-tasks/rows.txt`) and body.sh's
  messages: sent to W27.
- **F5.** The Makefile's clang message named lld, which nothing uses: fixed.

## Open

The strip runs once, after the ports finish: `tools/freeze/strip.sh` (dry run, review its report), then `--apply
--make-check`, committed as one change with anything the freeze task needs. The order is in `tools/freeze/README.md`.
Option B for the build steps is live wherever the toolchain is (the runner has it now).
