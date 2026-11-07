#!/bin/sh
# Builds every image the golden traces are made from into sim/tests/golden/images/ and writes images/MANIFEST.md (the
# toolchain, the exact build commands, each file's sha256). The golden traces are only meaningful against these exact
# bytes: regen.sh checks them against the manifest before it runs anything.
#   - the M1 programs, sim/fw/*.S (sim/fw/build.sh);
#   - sw/tests' hello, faults, misalign_ld, misalign_st, built exactly as sim/ref/meshsim/sw/build.sh builds them (-Os,
#     its crt0.S and app.ld, against its generated mesh.h).
# The riscv-tests images are sim/ref's own (sim/ref/meshsim/test/riscv/bin, riscv-tests 3df703c): not rebuilt, their
# sha256 recorded here too.
set -e
HERE=$(cd $(dirname $0) && pwd); SIM=$(cd $HERE/../.. && pwd); SW=$SIM/ref/meshsim/sw; OUT=$HERE/images
command -v riscv64-unknown-elf-gcc >/dev/null || { echo "build_images: needs riscv64-unknown-elf-gcc (apt install gcc-riscv64-unknown-elf)"; exit 1; }
rm -rf $OUT; mkdir -p $OUT
sh $SIM/fw/build.sh $OUT
T=$(mktemp -d); trap 'rm -rf $T' EXIT
CC="riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wno-array-bounds -Wl,--no-relax -I."
cd $SW
for n in hello faults misalign_ld misalign_st; do
  $CC -Os -T app.ld crt0.S tests/$n.c -o $T/$n.elf
  riscv64-unknown-elf-objcopy -O binary --only-section=.text $T/$n.elf $OUT/$n.code.bin
  riscv64-unknown-elf-objcopy -O binary --only-section=.data $T/$n.elf $OUT/$n.data.bin
  riscv64-unknown-elf-nm $T/$n.elf > $OUT/$n.sym
  echo "$n: code $(stat -c %s $OUT/$n.code.bin) B, data $(stat -c %s $OUT/$n.data.bin) B"
done
cd $OUT
{
  echo "# The golden traces' images (written by sim/tests/golden/build_images.sh)"
  echo
  echo "Toolchain: \`$(riscv64-unknown-elf-gcc --version | head -1)\` (Ubuntu 24.04's gcc-riscv64-unknown-elf"
  echo "13.2.0-11ubuntu1+12), binutils \`$(riscv64-unknown-elf-objcopy --version | head -1)\`."
  echo
  echo "The M1 programs (\`sim/fw/build.sh\`), from \`sim/fw\`, for each \`<n>.S\`:"
  echo
  echo '    riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -nostdlib -nostartfiles -static -Wl,--no-relax \'
  echo '        -I../ref/meshsim/sw -T ../ref/meshsim/sw/app.ld <n>.S -o <n>.elf'
  echo
  echo "\`hello\`, \`faults\`, \`misalign_ld\`, \`misalign_st\`, from \`sim/ref/meshsim/sw\` (as its \`build.sh\` builds them):"
  echo
  echo "    $CC \\"
  echo '        -Os -T app.ld crt0.S tests/<n>.c -o <n>.elf'
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
  echo "The riscv-tests images, sim/ref's own (\`sim/ref/meshsim/test/riscv/bin\`):"
  echo
  echo '```'
  (cd $SIM/ref/meshsim/test/riscv/bin && sha256sum *.bin)
  echo '```'
} > MANIFEST.md
echo "images: $(ls *.code.bin | wc -l) programs; MANIFEST.md written"
