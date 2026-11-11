#!/bin/sh
# Builds every image the golden traces are made from into sim/tests/golden/images/ and writes images/MANIFEST.md (the
# toolchain, how they are built, each file's sha256). The golden traces are only meaningful against these exact bytes:
# oracle/regen/golden.sh checks them against the manifest before it runs anything.
#   - the M1 programs, sim/fw/m1/*.S, and sw/tests' hello, faults, misalign_ld, misalign_st (sim/fw/tests/), as
#     sim/fw/build.sh builds them (W19: v1.3's flags, crt0.S, app.ld and mesh.h; byte-identical to the images first
#     built here from sim/ref by W2's scripts).
# The riscv-tests images are sim/fw/riscv-tests/bin (riscv-tests 3df703c, v1.3's): not rebuilt, their sha256 recorded
# here too.
set -e
HERE=$(cd $(dirname $0) && pwd); SIM=$(cd $HERE/../.. && pwd); OUT=$HERE/images
command -v riscv64-unknown-elf-gcc >/dev/null || { echo "build_images: needs riscv64-unknown-elf-gcc (apt install gcc-riscv64-unknown-elf)"; exit 1; }
T=$(mktemp -d); trap 'rm -rf $T' EXIT
OUT=$T/fw sh $SIM/fw/build.sh --build-only > $T/build.log 2>&1 || { tail $T/build.log; exit 1; }
rm -rf $OUT; mkdir -p $OUT
for n in $(cd $SIM/fw/m1 && ls *.S | sed 's/\.S$//') hello faults misalign_ld misalign_st; do
  for x in code.bin data.bin sym; do cp $T/fw/$n.$x $OUT/; done
  echo "$n: code $(stat -c %s $OUT/$n.code.bin) B, data $(stat -c %s $OUT/$n.data.bin) B"
done
cd $OUT
{
  echo "# The golden traces' images (written by sim/tests/golden/build_images.sh)"
  echo
  echo "Toolchain: \`$(riscv64-unknown-elf-gcc --version | head -1)\` (Ubuntu 24.04's gcc-riscv64-unknown-elf"
  echo "13.2.0-11ubuntu1+12), binutils \`$(riscv64-unknown-elf-objcopy --version | head -1)\`."
  echo
  echo "Built by \`sim/fw/build.sh\` (v1.3's flags, start-up files and linker script):"
  echo
  echo "- the M1 programs, from \`sim/fw/m1/<n>.S\`:"
  echo
  echo '      riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,--no-relax \'
  echo '          -Iinclude -T app.ld m1/<n>.S -o <n>.elf'
  echo
  echo "- \`hello\`, \`faults\`, \`misalign_ld\`, \`misalign_st\`, from \`sim/fw/tests/<n>.c\` (as v1.3's \`sw/build.sh\` built them):"
  echo
  echo "      riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding -nostdlib -nostartfiles \\"
  echo "          -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -Iinclude -Os -T app.ld crt0.S tests/<n>.c -o <n>.elf"
  echo
  echo "Then, for each: \`objcopy -O binary --only-section=.text\` to \`<n>.code.bin\`, \`--only-section=.data\` to"
  echo "\`<n>.data.bin\`, \`nm\` to \`<n>.sym\`."
  echo
  echo "## sha256"
  echo
  echo '```'
  sha256sum *.bin *.sym
  echo '```'
  echo
  echo "The riscv-tests images, v1.3's (\`sim/fw/riscv-tests/bin\`):"
  echo
  echo '```'
  (cd $SIM/fw/riscv-tests/bin && sha256sum *.bin)
  echo '```'
} > MANIFEST.md
echo "images: $(ls *.code.bin | wc -l) programs; MANIFEST.md written"
