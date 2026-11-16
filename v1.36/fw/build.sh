#!/bin/bash
# fw/build.sh [--check | --build-only | --toolchain]: builds every firmware image v1.36 runs, from fw/'s sources, into
# fw/build/out/ (OUT=DIR to change): the RTL's (the boot ROM, the tile programs, the sort's tile image and the core's
# programs) exactly as the RTL's recipes build them (Snarky One's `mise run core-images` and `mise run sort-images`: the
# same flags, start-up files and link scripts; each step names the recipe it reproduces), and v1.35's firmware, upgraded
# to v1.36, as v1.35's fw/build.sh built it (its flags and checks). Then:
#   --check       every image equal, byte for byte, to its committed copy; exit 1 on any difference or missing copy.
#   (none)        the images of fw/images/ are (re)written there, with fw/images/MANIFEST (sha256); the core's programs
#                 are compared with their fixture folder, tests/core-programs/elf, and never overwritten: its
#                 references are the RTL's measurements of those bytes (tests/core-programs/README.md).
#   --build-only  only the build into OUT.
#   --toolchain   builds nothing: exit 0 if the toolchain is the one below, else 1 with the reason on one line.
# The committed images: fw/images/ (the boot ROM, the tile programs, the sort's tile image, the 16-record runs' keys,
# the controllers', the replay controller and the tracer tile's program) and tests/core-programs/elf/ (the core's own
# programs). No image v1.36 runs is built anywhere else.
#
# PARTS="boot tile sort runs cc replay tracer core riscv coremark": build (and check) only those parts; empty: all.
#   boot      the boot ROM: boot/boot_rom.S -> boot/boot_rom.elf, .hex (one 32-bit word a line), .sym
#   tile      the tile programs: tile/<name>.S with tile/start.S -> tile/<name>.elf, tile/programs.txt
#   sort      the sort's tile image: sort/ -> sort/tile/sort.elf
#   runs      the 16-record runs' keys (tools/fwimage, built here with the host's C++ compiler, CXX): sort/sort.keys
#   cc        C&C 1's program, the master tile's: cc/cc1.c -> cc/cc1.elf; the memory tile's, cc/memory.c, and the
#             memory run's master, cc/cc_mem.c -> cc/memory.elf, cc/cc_mem.elf
#   replay    the replay controller: replay/replay_ctl.c -> replay/replay_ctl.elf
#   tracer    the tracer tile's program: tracer/tracer_ctl.c -> tracer/tracer_ctl.elf
#   core      the core's own programs that are ours: core/isa_edges.S and the interrupt programs -> core/*.elf
#   riscv     riscv-tests' programs, only with RISCV_TESTS=DIR, a riscv-tests checkout at the revision
#             core/riscv-tests/riscv-tests.toml pins -> core/rv32*.elf
#   coremark  CoreMark, only with COREMARK=DIR, a CoreMark checkout at the revision core/coremark/coremark.toml pins
#             -> core/coremark.elf (1 iteration) and core/coremark-bench.elf (10)
#
# Toolchain: xPack riscv-none-elf-gcc 15.2.0-1 (GCC 15.2.0, binutils 2.45), the one the committed images were built
# with (v1.35's were Ubuntu 24.04's riscv64-unknown-elf-gcc 13.2, which this host has not); RISCV_PREFIX names it (default `riscv-none-elf-`, on PATH). The machine's configuration is the RTL's default:
# an 8 x 8 mesh, 32 KiB of IMEM and of DMEM, a 4 MiB scratchpad (CONFIG below).
#
# Every image is linked from named object files, in the build's own folder, so that no file holds a name that changes
# from build to build (GCC names a temporary object file at random, and the linker records an object's name in the
# symbol table): two builds give the same bytes.
set -eu
export LC_ALL=C   # byte order for sort, ls and globs, and nm's symbol order: the outputs are the same in any locale
FW=$(cd "$(dirname "$0")" && pwd); SIM=$(cd "$FW/.." && pwd)
part() { [ -z "${PARTS:-}" ] || [[ " $PARTS " == *" $1 "* ]]; }
MODE=write; case "${1:-}" in --check) MODE=check;; --build-only) MODE=build;; --toolchain) MODE=toolchain;;
  '') ;; *) echo "usage: fw/build.sh [--check | --build-only | --toolchain]"; exit 2;; esac

P=${RISCV_PREFIX:-riscv-none-elf-}
WANT="xPack riscv-none-elf-gcc 15.2.0-1 (https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/tag/v15.2.0-1; unpack it and put its bin folder on PATH, or name it: RISCV_PREFIX=/path/to/bin/riscv-none-elf-)"
toolchain() {   # the reason the toolchain is not the one the images were built with, or nothing
  command -v "${P}gcc" > /dev/null || { echo "no ${P}gcc: needs $WANT"; return; }
  local v; v=$("${P}gcc" --version | head -1)
  case $v in *xPack*' 15.2.0') ;; *) echo "${P}gcc is '$v': needs $WANT";; esac
}
why=$(toolchain)
if [ $MODE = toolchain ]; then [ -z "$why" ] || { echo "$why"; exit 1; }; exit 0; fi
[ -z "$why" ] || { echo "fw/build.sh: $why"; exit 1; }

OUT=${OUT:-$FW/build/out}; rm -rf "$OUT"; mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
OC=${P}objcopy; NM=${P}nm; OD=${P}objdump

# The core's ISA (the RTL's MARCH), and a bare-metal link (core_tests.rs, `bare_metal_gcc`).
RV="${P}gcc -march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32"
BARE="-nostdlib -nostartfiles -static"
# The configuration the images are built for: Snarky One's Veryl.toml [properties].
MESH_SIZE=8; IMEM_BYTES=32768; DMEM_BYTES=32768; SCRATCHPAD_BYTES=4194304
CONFIG="-DMESH_SIZE=$MESH_SIZE -DIMEM_BYTES=$IMEM_BYTES -DDMEM_BYTES=$DMEM_BYTES -DSCRATCHPAD_BYTES=$SCRATCHPAD_BYTES"

# ---- the recipes' audits

# audit ELF: no instruction outside the core's ISA, as objdump decodes it (core_tests.rs, `audit`). isa_edges executes
# unimplemented instructions to check that they trap: accepted between its unimplemented_instructions_* labels.
ISA="lui auipc jal jalr beq bne blt bge bltu bgeu lb lh lw lbu lhu sb sh sw addi slti sltiu xori ori andi slli srli
srai add sub sll slt sltu xor srl sra or and fence fence.tso ecall ebreak
c.addi4spn c.lw c.sw c.nop c.addi c.jal c.li c.addi16sp c.lui c.srli c.srai c.andi c.sub c.xor c.or c.and c.j
c.beqz c.bnez c.slli c.lwsp c.jr c.mv c.ebreak c.jalr c.add c.swsp c.slli64 c.srli64 c.srai64 c.unimp
mul mulh mulhsu mulhu csrrw csrrs csrrc csrrwi csrrsi csrrci unimp czero.eqz czero.nez mret wfi"
audit() {
  local lo hi
  lo=$($NM "$1" | awk '$3 == "unimplemented_instructions_start" { print $1 }'); hi=$($NM "$1" | awk '$3 == "unimplemented_instructions_end" { print $1 }')
  $OD -d -M no-aliases "$1" > "$T/audit.dis"
  awk -F'\t' -v isa="$ISA" -v lo="0x${lo:-0}" -v hi="0x${hi:-0}" -v f="$(basename "$1")" '
    function hex(s,   v, i, c) { v = 0; s = tolower(s); sub(/^0x/, "", s); for (i = 1; i <= length(s); i++) { c = index("0123456789abcdef", substr(s, i, 1)); v = v * 16 + c - 1 } return v }
    BEGIN { n = split(isa, a, /[ \n]+/); for (i = 1; i <= n; i++) ok[a[i]] = 1; lo = hex(lo); hi = hex(hi) }
    NF >= 3 && $1 ~ /^ *[0-9a-f]+:$/ {
      m = $3; gsub(/ /, "", m); addr = $1; gsub(/[ :]/, "", addr); addr = hex(addr)
      if (!(m in ok) && m !~ /^\./ && !(addr >= lo && addr < hi)) { printf "ERROR: %s: %s at 0x%x is outside the core'"'"'s ISA\n", f, m, addr; bad = 1 }
    }
    END { exit bad }' "$T/audit.dis" || exit 1
}
# nolib ELF NAMES...: none of the library's arithmetic helpers linked in (sort_images.rs, `forbid_symbols`)
nolib() { local e=$1 s; shift; for s in "$@"; do
  if $NM "$e" | grep -q " $s\$"; then echo "ERROR: $(basename "$e"): links library arithmetic ($s)"; exit 1; fi; done; }
# wfis DISASSEMBLY: every WFI of the sort keeps the port's rules (sort_images.rs, `check_wfis`; port/mesh.h): a
# load of EVENTS follows it at once, and exactly one, the barrier's, follows a store to ARRIVE.
wfis() {
  awk -F'\t' '
    NF >= 3 && $1 ~ /^ *[0-9a-f]+:$/ { n++; m[n] = $3; gsub(/ /, "", m[n]); o[n] = $4; sub(/ *#.*/, "", o[n]); a[n] = $1 }
    END {
      for (i = 1; i <= n; i++) if (m[i] == "wfi") {
        if (!((m[i + 1] == "lw" || m[i + 1] == "c.lw") && o[i + 1] ~ /,-2036\(zero\)$/)) { print "ERROR: the sort: the WFI at " a[i] " keeps none of the port'"'"'s patterns"; exit 1 }
        if ((m[i - 1] == "sw" || m[i - 1] == "c.sw") && o[i - 1] ~ /,-2024\(zero\)$/) barriers++
      }
      if (barriers != 1) { print "ERROR: the sort: expected the barrier'"'"'s one WFI after ARRIVE, found " barriers + 0; exit 1 }
    }' "$1" || exit 1
}

# ---- an ELF file's header, read with od (a little-endian host)
u32() { od -An -tu4 -j "$2" -N4 "$1" | tr -d ' '; }
u16() { od -An -tu2 -j "$2" -N2 "$1" | tr -d ' '; }
# pack ELF: the file cut after the last bytes a loader reads (its program header table or a loaded segment's file
# bytes), with no section header table (e_shoff, e_shnum, e_shstrndx 0), and each loaded segment checked to start a
# 16-byte row, as the boot writes rows (tile_tests.rs, `without_sections`, `check_segments`).
pack() {
  local f=$1 phoff phentsize phnum end i h e
  phoff=$(u32 "$f" 28); phentsize=$(u16 "$f" 42); phnum=$(u16 "$f" 44)
  end=$((phoff + phnum * phentsize))
  for ((i = 0; i < phnum; i++)); do
    h=$((phoff + i * phentsize))
    [ "$(u32 "$f" $h)" = 1 ] || continue                                   # PT_LOAD
    if [ "$(u32 "$f" $((h + 16)))" != 0 ]; then e=$(($(u32 "$f" $((h + 4))) + $(u32 "$f" $((h + 16))))); [ $e -le $end ] || end=$e; fi
    if [ "$(u32 "$f" $((h + 20)))" != 0 ] && [ $(($(u32 "$f" $((h + 12))) % 16)) != 0 ]; then
      echo "ERROR: $(basename "$f"): a loaded segment does not start a 16-byte row"; exit 1; fi
  done
  head -c "$end" "$f" > "$f.packed"
  printf '\0\0\0\0' | dd of="$f.packed" bs=1 seek=32 conv=notrunc status=none
  printf '\0\0\0\0' | dd of="$f.packed" bs=1 seek=48 conv=notrunc status=none
  mv "$f.packed" "$f"
}

# ---- the boot ROM (core_tests.rs, `build_boot_rom`): linked at the ROM's base, the reset PC; its words and the symbols
# the simulator reads (tests/rtl_images.h)
if part boot; then
mkdir -p "$T/boot" "$OUT/boot"; cd "$T/boot"
$RV -c "$FW/boot/boot_rom.S" -o boot_rom.o
$RV $BARE -Wl,-Ttext=0x2000000 boot_rom.o -o "$OUT/boot/boot_rom.elf"
audit "$OUT/boot/boot_rom.elf"
$OC -O binary -j .text "$OUT/boot/boot_rom.elf" boot_rom.bin
od -An -v -tx4 -w4 boot_rom.bin | sed 's/^ *//' > "$OUT/boot/boot_rom.hex"
$NM "$OUT/boot/boot_rom.elf" | grep -E ' (handler|image|park|wait_load|wait_release|_start)$' | sort > "$OUT/boot/boot_rom.sym"
echo "boot ROM: $(wc -l < "$OUT/boot/boot_rom.hex") words"
fi  # boot

# ---- the tile programs (tile_tests.rs): tile/programs.toml's, in kind order, each start.S and the program, linked by
# link.ld through the C preprocessor with a 16-byte page size; audited, then stripped and packed
if part tile; then
mkdir -p "$T/tile" "$OUT/tile"; cd "$T/tile"
${P}cpp -P -undef -x c $CONFIG "$FW/tile/link.ld" -o link.ld
$RV $CONFIG -c "$FW/tile/start.S" -o start.o
: > "$OUT/tile/programs.txt"; kind=0
for n in $(sed -n 's/^name = "\(.*\)"$/\1/p' "$FW/tile/programs.toml"); do
  $RV $CONFIG -c "$FW/tile/$n.S" -o "$n.o"
  $RV $BARE -Wl,-z,max-page-size=16 $CONFIG -T link.ld start.o "$n.o" -o "$OUT/tile/$n.elf"
  audit "$OUT/tile/$n.elf"
  $OC --strip-all "$OUT/tile/$n.elf"; pack "$OUT/tile/$n.elf"
  echo "$kind $n.elf" >> "$OUT/tile/programs.txt"; kind=$((kind + 1))
done
[ "$(grep -c '^kind = ' "$FW/tile/programs.toml")" = $kind ] && [ "$(sed -n 's/^kind = //p' "$FW/tile/programs.toml" | tr '\n' ' ')" = "$(seq -s' ' 0 $((kind - 1))) " ] ||
  { echo "ERROR: tile/programs.toml: each program's kind must be its index"; exit 1; }
echo "tile programs: $kind"
fi  # tile

# ---- the sort's tile image (sort_images.rs, `build_tile_image`): v1.35's sort (sort/program/sw), with its planner
# (sort/skew/planner) and split (sort/split) patched as v1.35's build patched them (sort/program/patches: planner.patch,
# split-barrier.patch) and for the port layer (port.patch), on the port layer (sort/port, port/mesh.h; sort/PORTING.md),
# built as meshsim's sort was (-O2 -fno-jump-tables, function and data sections, --no-relax, --gc-sections, the split
# with -DSPLIT_BARRIER=sort_barrier), -Werror
if part sort; then
mkdir -p "$T/sort/p/sort/skew" "$T/sort/p/sort/split" "$OUT/sort/tile"; cd "$T/sort"
S=$FW/sort
cp -r "$S/skew/planner" p/sort/skew/; cp "$S/split/split.c" "$S/split/split.h" "$S/split/split_regs.h" p/sort/split/
for x in planner split-barrier port; do patch -s -p0 -d p < "$S/program/patches/$x.patch"; done
SK=$T/sort/p/sort/skew/planner; SP=$T/sort/p/sort/split
SCC="$RV $BARE -ffreestanding -fno-builtin -Wall -Wno-array-bounds -Werror -Wl,--no-relax $CONFIG -I$S/program/sw -I$SK -I$FW/port"
SOPT="-O2 -fno-jump-tables -ffunction-sections -fdata-sections -fstack-usage"
OBJS=""
for f in "$S"/program/sw/*.c "$SK/skew_common.c" "$SK/skew_th.c" "$SK/skew_bt.c" "$S/port/port.c"; do
  o=$(basename "$f" .c).o
  $SCC $SOPT -c "$f" -o "$o"; OBJS="$OBJS $o"
done
$SCC $SOPT -DSPLIT_BARRIER=sort_barrier -I"$SP" -c "$SP/split.c" -o split_bisect.o
OBJS="$OBJS split_bisect.o"
${P}cpp -P -undef -x c -D__ASSEMBLER__ $CONFIG -I"$FW/port" "$S/port/sort.ld" -o sort.ld
$SCC -c "$S/port/crt0.S" -o crt0.o
$SCC -T sort.ld -Wl,--gc-sections -Wl,--orphan-handling=error -Wl,--build-id=none -Wl,--fatal-warnings -Wl,-Map=sort.map \
  crt0.o $OBJS -o "$OUT/sort/tile/sort.elf"
audit "$OUT/sort/tile/sort.elf"; wfis "$T/audit.dis"
nolib "$OUT/sort/tile/sort.elf" __udivsi3 __umodsi3 __divsi3 __modsi3 __mulsi3 __muldi3
[ "$($NM "$OUT/sort/tile/sort.elf" | awk '$3 == "_start" { print $1 }')" = 01000000 ] || { echo "ERROR: the sort: _start is not at IMEM's base"; exit 1; }
$OC --dump-section .sort_layout=layout.bin "$OUT/sort/tile/sort.elf"
[ "$(stat -c %s layout.bin)" = 20 ] && [ "$(u32 layout.bin 0)" = $((0x4C41594F)) ] || { echo "ERROR: the sort: no layout table (.sort_layout: its magic and four buffers)"; exit 1; }
echo "the sort's tile image: code $(${P}size -A "$OUT/sort/tile/sort.elf" | awk '$1 == ".text" { print $2 }') B of $IMEM_BYTES"
fi  # sort

# ---- the keys of the 16-record sort runs (sort_images.rs, `sort_test_keys`): tools/fwimage, built here with the
# host's C++ compiler (CXX)
if part runs; then
mkdir -p "$T/runs"; cd "$T/runs"
${CXX:-c++} -std=c++20 -O2 -Wall -Wextra -Werror "$SIM/tools/fwimage/fwimage.cpp" -o fwimage
./fwimage keys 16 "$OUT/sort/sort.keys"
echo "runs: the 16-record runs' keys"
fi  # runs

# ---- v1.35's programs, on the port layer (port/mesh.h): the controllers' (cc/: C&C 1's, the master tile's; and the
# memory tile's and the memory run's master, which v1.35 had not), the replay controller (replay/) and the tracer
# tile's (tracer/), each with crt0.S and app.ld, as v1.35's build.sh built them (meshsim/sw/build_cc.sh's flags for
# cc/, replay/build.sh's and tracer/build.sh's for the others), and v1.35's -march; each compiled in its own folder,
# linked from a named object, audited, its symbols kept (the tests find the program's data by them). Software
# division refused.
V135="${P}gcc -march=rv32i_zicsr_zmmul -mabi=ilp32"
RCC="$V135 $CONFIG -std=gnu11 -O2 -Wall -Wextra -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wl,--no-relax -include $FW/port/mesh.h"
CTLCC="$V135 $CONFIG -std=gnu11 -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wall -Wextra -Wno-array-bounds -Wl,--no-relax -I. -I$FW/port -Os"
SOFTDIV='__udivsi3 __umodsi3 __divsi3 __modsi3'
# v135 SRC CC OUT: SRC (a C file under fw/) with crt0.S, by app.ld, into OUT
v135() {
  local src=$1 cc=$2 out=$3 n; n=$(basename "$src" .c)
  mkdir -p "$T/v135" "$(dirname "$out")"
  (cd "$FW" && $V135 $CONFIG -I"$FW/port" -c crt0.S -o "$T/v135/crt0.o")
  (cd "$(dirname "$FW/$src")" && $cc -c "$n.c" -o "$T/v135/$n.o")
  (cd "$T/v135" && $cc -T "$FW/app.ld" -Wl,--build-id=none crt0.o "$n.o" -o "$out")
  audit "$out"; nolib "$out" $SOFTDIV
  [ "$($NM "$out" | awk '$3 == "_start" { print $1 }')" = 01000000 ] || { echo "ERROR: $n: _start is not at IMEM's base"; exit 1; }
  echo "$n: code $(${P}size -A "$out" | awk '$1 == ".text" { print $2 }') B, data $(${P}size -A "$out" | awk '$1 == ".data" { print $2 }') B"
}
if part cc; then
v135 cc/cc1.c "$RCC -Werror" "$OUT/cc/cc1.elf"
end=$($NM "$OUT/cc/cc1.elf" | awk '$3 == "_end" { print $1 }')
[ $((0x$end)) -le $((0x1000)) ] || { echo "cc1: data and .bss end at 0x$end, above 0x1000 (cc1's report buffer)"; exit 1; }
# the memory tile's program and the memory run's master, new in v1.36, on v1.36's interface (include/mesh.h)
NEWCC="$V135 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -nostdlib -nostartfiles -static -fno-builtin -Wl,--no-relax -I$FW/include"
v135 cc/memory.c "$NEWCC" "$OUT/cc/memory.elf"
v135 cc/cc_mem.c "$NEWCC" "$OUT/cc/cc_mem.elf"
fi  # cc
if part replay; then v135 replay/replay_ctl.c "$CTLCC" "$OUT/replay/replay_ctl.elf"; fi
if part tracer; then v135 tracer/tracer_ctl.c "$CTLCC" "$OUT/tracer/tracer_ctl.elf"; fi

# ---- the core's own programs (core_tests.rs, `gcc`): each linked with core/start.S by core/link.ld at the test
# memory's map, for the configured capacities
CORE="$RV $BARE -T $FW/core/link.ld -Wl,--defsym=IMEM_BYTES=$IMEM_BYTES -Wl,--defsym=DMEM_BYTES=$DMEM_BYTES"
if part core || part riscv || part coremark; then
mkdir -p "$T/core" "$OUT/core"; cd "$T/core"
$RV -c "$FW/core/start.S" -o start.o
fi

if part core; then
cd "$T/core"
# isa_edges (`build_isa_edges`)
$RV -c "$FW/core/isa_edges.S" -o isa_edges.o
$CORE start.o isa_edges.o -lgcc -o "$OUT/core/firmware-isa_edges.elf"
audit "$OUT/core/firmware-isa_edges.elf"
# the interrupt programs (tests/core-programs/README.md, step 4): each its own _start, with no start-up file
for n in irq_wfi nmi_wfi nmi_run trap_nmi; do
  $RV -c "$FW/core/$n.S" -o "$n.o"
  $RV $BARE -Wl,--build-id=none -Wl,--defsym=IMEM_BYTES=0x8000 -Wl,--defsym=DMEM_BYTES=0x8000 -T "$FW/core/link.ld" "$n.o" -o "$OUT/core/$n.elf"
  audit "$OUT/core/$n.elf"
done
echo "the core's programs: isa_edges and 4 interrupt programs"
fi  # core

# riscv-tests (`build_upstream`, `selected_tests`): every test of the groups' upstream lists, less the exclusions
if part riscv && [ -n "${RISCV_TESTS:-}" ]; then
cd "$T/core"
TOML=$FW/core/riscv-tests/riscv-tests.toml
rev=$(sed -n 's/^revision = "\(.*\)"$/\1/p' "$TOML")
[ "$(git -C "$RISCV_TESTS" rev-parse HEAD 2> /dev/null)" = "$rev" ] || { echo "fw/build.sh: RISCV_TESTS=$RISCV_TESTS is not a riscv-tests checkout at $rev (core/riscv-tests/riscv-tests.toml)"; exit 1; }
nr=0
for g in $(sed -n 's/^groups = \[\(.*\)\]$/\1/p' "$TOML" | tr -d '",'); do
  for n in $(awk -v g="$g" 'index($0, g "_sc_tests = \\") == 1 { on = 1; next } on && /^[ \t]*$/ { exit } on { gsub(/\\/, ""); for (i = 1; i <= NF; i++) print $i }' "$RISCV_TESTS/isa/$g/Makefrag" | sort); do
    grep -q "^\"$g/$n\" = " "$TOML" && continue   # excluded, with its reason there
    $RV -I"$FW/core/riscv-tests" -I"$RISCV_TESTS/isa/macros/scalar" -c "$RISCV_TESTS/isa/$g/$n.S" -o "$g-$n.o"
    $CORE start.o "$g-$n.o" -o "$OUT/core/$g-$n.elf"
    audit "$OUT/core/$g-$n.elf"; nr=$((nr + 1))
  done
done
echo "riscv-tests: $nr programs"
fi  # riscv

# CoreMark (coremark.rs, `build_elf`): the benchmark's sources with the port, the flags of core/coremark/coremark.toml
if part coremark && [ -n "${COREMARK:-}" ]; then
TOML=$FW/core/coremark/coremark.toml
rev=$(sed -n 's/^revision = "\(.*\)"$/\1/p' "$TOML")
[ "$(git -C "$COREMARK" rev-parse HEAD 2> /dev/null)" = "$rev" ] || { echo "fw/build.sh: COREMARK=$COREMARK is not a CoreMark checkout at $rev (core/coremark/coremark.toml)"; exit 1; }
CMFLAGS=$(sed -n '/^flags = \[/,/^\]/s/^ *"\(.*\)",\{0,1\}$/\1/p' "$TOML" | tr '\n' ' '); CMFLAGS=${CMFLAGS% }
for it in 1:coremark 10:coremark-bench; do
  mkdir -p "$T/core/${it#*:}"; cd "$T/core/${it#*:}"
  OBJS=""
  for f in "$COREMARK"/core_list_join.c "$COREMARK"/core_main.c "$COREMARK"/core_matrix.c "$COREMARK"/core_state.c "$COREMARK"/core_util.c "$FW/core/coremark/core_portme.c"; do
    o=$(basename "$f" .c).o
    $RV $CMFLAGS -I"$FW/core/coremark" -I"$COREMARK" -DITERATIONS=${it%:*} -DFLAGS_STR="\"$CMFLAGS\"" -c "$f" -o "$o"; OBJS="$OBJS $o"
  done
  cp ../start.o start.o
  $CORE $CMFLAGS start.o $OBJS -lgcc -o "$OUT/core/${it#*:}.elf"
  audit "$OUT/core/${it#*:}.elf"
done
echo "CoreMark: the test run (1 iteration) and the benchmark run (10)"
fi  # coremark
cd "$FW"

[ $MODE = build ] && { echo "fw/build.sh: built into $OUT"; exit 0; }

# ---- compare with (or install) the committed images. OUT's folders map to: core/ -> tests/core-programs/elf (a
# fixture folder: compared, never written); the rest -> fw/images
bad=0; nfix=0; nfw=0
cd "$OUT"
for f in $(find . -type f | sed 's|^\./||' | sort); do
  case $f in
    core/*)
      nfix=$((nfix + 1))
      cmp -s "$f" "$SIM/tests/core-programs/elf/${f#core/}" || { echo "DIFFERS (or missing): $f against tests/core-programs/elf/${f#core/}"; bad=1; } ;;
    *)
      nfw=$((nfw + 1))
      if [ $MODE = check ]; then cmp -s "$f" "$FW/images/$f" || { echo "DIFFERS (or missing): $f against fw/images/$f"; bad=1; }
      else mkdir -p "$FW/images/$(dirname "$f")"; cp "$f" "$FW/images/$f"; chmod 644 "$FW/images/$f"; fi ;;
  esac
done
if [ $MODE = write ]; then
  { echo "# fw/images' hash list (sha256, path in fw/images/), written by fw/build.sh; fw/check_images.sh checks it."
    (cd "$FW/images" && find . -type f ! -name MANIFEST ! -name README.md | sed 's|^\./||' | sort | xargs sha256sum); } > "$FW/images/MANIFEST"
fi
# With every part built, nothing committed may be left that the build does not make.
if [ -z "${PARTS:-}" ]; then
  for f in $(cd "$FW/images" && find . -type f ! -name MANIFEST ! -name README.md ! -name sort-64k.keys.sha256 | sed 's|^\./||' | sort); do
    [ -f "$OUT/$f" ] || { echo "NOT BUILT: fw/images/$f"; bad=1; }
  done
  notbuilt=0
  for f in $(cd "$SIM/tests/core-programs/elf" && ls); do
    [ -f "$OUT/core/$f" ] || notbuilt=$((notbuilt + 1))
  done
  # riscv-tests' and CoreMark's need their checkouts
  expect=0; [ -n "${RISCV_TESTS:-}" ] || expect=$((expect + $(cd "$SIM/tests/core-programs/elf" && ls rv32*.elf | wc -l)))
  [ -n "${COREMARK:-}" ] || expect=$((expect + 2))
  [ $notbuilt = $expect ] || { echo "NOT BUILT: $notbuilt programs of tests/core-programs/elf, where $expect are expected (riscv-tests' and CoreMark's without their checkouts)"; bad=1; }
  [ -n "${RISCV_TESTS:-}" ] || echo "not rebuilt: riscv-tests' programs (RISCV_TESTS names no checkout); their committed images: fw/check_images.sh"
  [ -n "${COREMARK:-}" ] || echo "not rebuilt: CoreMark's two programs (COREMARK names no checkout); their committed images: fw/check_images.sh"
fi
[ $bad = 0 ] || { echo "fw/build.sh: FAILED"; exit 1; }
if [ $MODE = check ]; then echo "fw/build.sh --check: $nfw images equal to fw/images/, $nfix to tests/core-programs/elf/"
else echo "fw/build.sh: $nfw images written to fw/images/; $nfix equal to tests/core-programs/elf/"; fi
