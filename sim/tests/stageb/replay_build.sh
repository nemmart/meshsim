#!/bin/bash
# replay_build.sh (WORKER25, P7b): run_tests.sh's step replay-build, v1.3's sw/replay/build.sh in v1.35. The replay
# controller's three images (replay_ctl; log8k/, the 8 KB-log control; oldmap/, CL4's test) are committed
# (tests/m3-images/replay_ctl, fw/images/log8k, fw/images/oldmap: byte-identical to v1.3's build, W19). With the RISC-V
# toolchain (fw/build.sh's: riscv64-unknown-elf-gcc 13.2), they are rebuilt from fw/replay into scratch with v1.3's
# commands and must equal the committed ones; without it, the committed images are checked against their MANIFESTs.
# Either way the step prints v1.3's line ("replay_ctl: code N B, data N B, data+bss end 0x... (stack below 0x7BC0)")
# from the committed image, and exits 1 on any difference.
set -u
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
cd "$(dirname "$0")/../.."
FW=$PWD/fw; IMG=tests/m3-images; FAIL=0
if command -v riscv64-unknown-elf-gcc > /dev/null; then
  T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
  CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wextra -Wno-array-bounds -Wl,--no-relax -I. -I$FW/include -Os"
  img() {   # img SRC OUT [flags]: build, then the three files against the committed copy
    local src=$1 out=$2; shift 2
    (cd "$FW/replay" && $CC "$@" -T ../app.ld ../crt0.S "$src" -o "$T/x.elf") || { echo "FAIL: $out did not build"; FAIL=1; return; }
    if riscv64-unknown-elf-nm "$T/x.elf" | grep -qE ' (__udivsi3|__umodsi3|__divsi3|__modsi3)$'; then echo "ERROR: software division"; FAIL=1; fi
    riscv64-unknown-elf-objcopy -O binary --only-section=.text "$T/x.elf" "$T/x.code.bin"
    riscv64-unknown-elf-objcopy -O binary --only-section=.data "$T/x.elf" "$T/x.data.bin"
    riscv64-unknown-elf-nm "$T/x.elf" > "$T/x.sym"
    for f in code.bin data.bin sym; do cmp -s "$T/x.$f" "$out.$f" || { echo "FAIL: $out.$f differs from the build"; FAIL=1; }; done
  }
  img replay_ctl.c "$IMG/replay_ctl"
  img replay_ctl.c fw/images/log8k/replay_ctl -DHALF_LOG=256
  img replay_ctl_oldmap.c fw/images/oldmap/replay_ctl
else
  for m in "$IMG/MANIFEST:replay_ctl" "fw/images/MANIFEST:log8k/replay_ctl" "fw/images/MANIFEST:oldmap/replay_ctl"; do
    man=${m%%:*}; n=${m#*:}; d=$(dirname "$man")
    for f in code.bin data.bin sym; do
      want=$(awk -v n="$n.$f" '$2 == n {print $1}' "$man"); got=$(sha256sum "$d/$n.$f" 2>/dev/null | cut -d' ' -f1)
      [ -n "$want" ] && [ "$want" = "$got" ] || { echo "FAIL: $d/$n.$f differs from $man"; FAIL=1; }
    done
  done
fi
END=$(sed -n 's/^\([0-9a-f]*\) . _end$/\1/p' "$IMG/replay_ctl.sym")
echo "replay_ctl: code $(stat -c %s "$IMG/replay_ctl.code.bin") B, data $(stat -c %s "$IMG/replay_ctl.data.bin") B, data+bss end 0x$END (stack below 0x7BC0)"
exit $FAIL
