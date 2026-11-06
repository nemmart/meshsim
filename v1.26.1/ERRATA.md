# v1.26.1 errata

Known errors in **v1.26.1** (this folder, frozen 2026-11-05), with their corrections. Frozen folders are never edited except for
this file (README).

| # | where in `v1.26.1/` | the error | correct | affects the RTL | found, corrected |
|---|---|---|---|---|---|
| E1 | `meshsim`'s `ConvertDelivery` (the v1.25 to v1.26.1 conversion of the test deliveries) | its `manifest.json` lists the converted files in an order that varies from one JVM run to the next (its `converted` object comes from `Map.of`), so **the manifest's checksum changes between runs although every converted file is identical** | compare the converted files, or the manifest's entries, not the manifest's checksum; `wip/inputs/` (the input pipeline) rewrites it in a fixed order, and `wip`'s `ConvertDelivery` writes a fixed order, with a test | no: test tooling only; the converted data is unaffected | WORKER27, 2026-11-05 (Quest 105 and 106 logged different checksums for the same conversion); fixed in `wip` (`3c7323c`) |
