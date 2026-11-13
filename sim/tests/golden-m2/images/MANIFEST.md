# golden-m2 images (WORKER7, 2026-11-07)

Built from `ref` (v1.3's simulator with the trace hooks, frozen, outside the frozen copy) with Ubuntu 24.04's riscv64-unknown-elf-gcc 13.2: regs, wfi, a2a by
`v1.3/meshsim/sw/build.sh`; the sort by `v1.3/meshsim/sw/ext/sort/build.sh` (byte-identical to Quest 118's image, the one behind
1,229,687 cycles). Committed because the runner has no riscv toolchain (D4). the oracle's `golden-m2.sh` (outside the frozen copy) checks them against this list,
then installs them into its scratch copy of `ref`'s simulator folder. The boot ROM (`v1.3/meshsim/sw/bootrom.hex`) and `hops.txt` come
from `ref` itself.

```
49d234782e2c88a48d83a1a16c434c82284ab02bff540d2a2827c83737d7a5bd  a2a.code.bin
b5abd0ca112905f852b3722772a5489b979570c7f8b754a45af33040bc3de81a  a2a.data.bin
be0cd808098b8d0d796074255b28e572333614a81e5e81a855fa5be725c681af  regs.code.bin
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  regs.data.bin
2ab3c746aa6bf2f8d41a637a5ee4bda514754ea54fc3445beab59a50e5762f99  sort.code.bin
412d9f3fa18dac9d629472b3935d12acd529a0725f39f848ae270d71fc5c31f7  sort.data.bin
4575a66ae11fb8ae803f4a5fd48893978d4673b29e589ed5318c555f78c4d04e  wfi.code.bin
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  wfi.data.bin
1a941d9049c099ac3e98e732635ca6d609af56be56ef5e55dfae1a94e13ebc02  a2a.sym
43ce506a5f2fd6b092fb7979d96268eb95bdb160a696329afa91fd3bd2887265  regs.sym
4a742353a535f95a0e2c501743393b55955fed701a12c2edf3f60c006b943a90  sort.sym
50c63a964132d2980b01b7ad592e4cdabf2125d0482832ec5b3fe2b6d1532afb  wfi.sym
```
