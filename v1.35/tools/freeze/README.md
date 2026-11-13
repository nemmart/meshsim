# tools/freeze: the freeze's tools

What turns the work-in-progress folder into the frozen v1.35. Run from anywhere; paths are relative to the version's root.

| file | what it does |
|---|---|
| `exclude.txt` | the paths the frozen copy leaves out: the oracle and v1.3's Java, the stage-1 API sketches, the leftover benches, the workers' session material (reports and the index stay). One extended regular expression per line; `!` keeps a path an earlier line left out |
| `frozen.sh` | the files of the frozen copy, one per line: the tracked files less `exclude.txt`'s (`--untracked`: with the untracked files git doesn't ignore) |
| `copy.sh DST [--dry-run]` | the copy: `frozen.sh`'s files into `DST`, byte for byte with their modes, nothing edited or added; refuses an existing `DST` or uncommitted changes. `--dry-run` lists what is left out, by the line that leaves it out |
| `strip_files.sh` | the source files the comment strip reads, with the lexer's language for each |
| `cstrip.cpp` | the strip's lexer: lists a file's comments (`list`), its code without them (`code`), or rewrites only its comments (`apply`); see its header |

`tools/doccheck.sh` (one folder up) checks the documents of the same file set.

## The freeze, in order

1. The ports finish: every `run_tests.sh` step live, none PENDING.
2. `tools/freeze/strip.sh --apply` in this folder's version (the benches renamed, the comments and messages cleaned,
   with its proof; run its dry run first), committed as one change with the freeze task's update to the new bench paths.
3. `make check DOCCHECK=1`: the documents' checks run after the renames, since the documents use the new names.
4. The freeze check on the runner (`tests/freeze/`).
5. `tools/freeze/copy.sh DST` (`DST` the repository's `v1.35/`), then one commit of `DST`. That commit adds
   `v1.35/README.md`, by which `tests/README.md` finds the freeze commit.
