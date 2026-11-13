# pipeline/keys: the sort's key sets

*For the proof-pipeline user: how to make the sort's key sets and the split's data sets, and how they are checked.*
Java, as v1.3's (D40); built by `pipeline/build.sh` into `pipeline/build/keys/` (`BUILDING.md`).

| file | what | run |
|---|---|---|
| `KeyGen.java` | the sort's key sets, bit-exact with numpy 2.x | `java -cp pipeline/build/keys KeyGen OUTDIR [--n PER_TILE] [--only slug,...]` |
| `SplitSets.java` | the split's 25 data sets | `java -cp pipeline/build/keys SplitSets OUTDIR [--n PER_TILE]` |
| `EXPECTED.sha256` | the outputs' hashes: the uniform key sets at 16,384 and 65,536 per tile, every key and split set at 2,000 | checked by `pipeline/check.sh` |
