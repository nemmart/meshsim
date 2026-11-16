//! Program images for the core's Rust `program_test`
//! (`rtl/blocks/core/test/src/program_test.rs`), and the profile matrix. Only
//! prepares data and orchestrates: the programs run on Verilator models of the core
//! in the Rust tests. CoreMark's build and the benchmark task are in coremark.rs.
//!
//! `core-images` builds every program, audits each ELF for instructions outside the
//! core's ISA, checks that each fits in IMEM and DMEM, and writes to
//! `build/core-tests/`:
//!
//! ```text
//! programs.txt   the program list program_test reads
//! elf/           the ELF files and their disassembly
//! rust/          Cargo build output for the Rust firmware
//! boot_rom.elf   the boot ROM's program, and boot_rom.dis its disassembly
//! ```
//!
//! The boot ROM's program, `rtl/blocks/core/src/boot_rom.S`, is the ROM's contents
//! (boot_rom_pkg.veryl holds them, written by hand), which boot_rom_test and
//! core_test compare with this ELF file and run. It depends on no configuration
//! (it branches on the straps alone, spec 6.5). It is linked at the ROM's base, so
//! its symbols are its addresses, and audited like the programs.
//!
//! It builds the tile programs for the tile's test there too (tile_tests.rs). A
//! partition's test task (partitions.rs) builds only the images its tests read
//! (`build_core_images`, `tile_tests::build_images`).
//!
//! Every program is linked the same way, with the firmware's startup code and linker
//! script (programs/firmware/start.S, link.ld): it starts at address 0, its code is
//! loaded into IMEM and its read-only data, `.data` and zeroed `.bss` into DMEM (the
//! boot load, rtl/blocks/core/spec.md §1, "DMEM at boot"), and it reports its
//! result through the test memory's result device. `programs.txt` holds one program
//! per line, after comment lines that begin with `#`:
//!
//! ```text
//! <cycle limit> <ELF file, relative to the list's directory> <name>
//! ```
//!
//! What program_test does with the programs, its mode (`Mode`), is the environment's
//! PROGRAM_TEST_MODE, and the list it reads PROGRAM_TEST_LIST's
//! (`program_test`); by default it tests the programs of `core-images`.
//!
//! `test-core-matrix` copies the project to a temporary directory for each profile
//! in `rtl/chip/profiles.toml`, sets the profile's `[properties]` (a mesh size,
//! capacities, the LPDDR address width, the RW engine's sizes) in the copy's `Veryl.toml`, builds the
//! programs (the tile programs among them) there, lints the design
//! (`common::lint`), and runs the Rust tests of
//! Cargo's workspace there, on models built from the copy's design: every package's
//! but xtask's own and those of the partitions whose manifests set `matrix =
//! false`, which run at the default configuration alone (the chip's sort). The
//! profiles run at once, each in its own copy, and their verdicts are printed in
//! the file's order once all have finished. A failing profile's copy is kept, with
//! the test output in its `output.log`.
//!
//! `cosim-core` (cosim_core.rs) builds the same programs in a temporary copy and
//! compares their RVFI traces, from program_test in trace mode, with Whisper.

use crate::common::{
    MemoryCapacities, Partition, RISCV_TESTS, SETUP, cargo, core_map_word, gcc_tool, load_toml, memory_capacities,
    partitions, run, temporary_copy,
};
use crate::coremark;
use crate::tile_tests;
use anyhow::{Context, Result, anyhow, bail, ensure};
use fs_err as fs;
use object::read::elf::{ElfFile32, ProgramHeader};
use object::{Endianness, Object, ObjectSymbol, elf};
use serde::Deserialize;
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use tempfile::TempDir;

/// The bases of IMEM and DMEM in the core's map, where a program's loadable bytes go
/// (`Project::memory_bases`).
#[derive(Clone, Copy)]
pub struct MemoryBases {
    pub imem: u32,
    pub dmem: u32,
}

/// What program_test does with every program of a list (PROGRAM_TEST_MODE), but
/// test, its default, which runs each with ideal, then randomly delayed memory
/// timing.
#[derive(Clone, Copy)]
pub enum Mode {
    /// As the test mode, printing every run and every RVFI record (format:
    /// program_test.rs).
    Trace,
    /// Run each once with ideal timing, printing its console bytes.
    Bench,
}

impl Mode {
    fn name(self) -> &'static str {
        match self {
            Mode::Trace => "trace",
            Mode::Bench => "bench",
        }
    }
}

/// The Rust test crate of the core's partition, and its program test.
const TEST_CRATE: &str = "blocks_core";
const PROGRAM_TEST: &str = "program_test::program_test";

/// The cycle limit of every run in programs.txt: CoreMark's test run is the
/// longest program.
pub const CYCLE_LIMIT: u32 = coremark::cycle_limit(coremark::TEST_ITERATIONS);

/// The core's ISA, which every program is built for and Whisper models.
pub const MARCH: &str = "rv32ic_zicond_zicsr_zmmul";
const ABI: &str = "ilp32";
/// The Rust target closest to MARCH (riscv32imc would emit division), with
/// RUST_FEATURES adding the rest of the core's ISA.
pub const RUST_TARGET: &str = "riscv32i-unknown-none-elf";
/// C, Zmmul and Zicond for the firmware's own code (the precompiled core library
/// stays plain RV32I), and linker relaxation, which turns auipc+jalr calls into
/// JAL.
pub const RUST_FEATURES: &str = "-Ctarget-feature=+c,+zmmul,+zicond,+relax";

/// `cargo xtask core-images`
pub fn build_images(root: &Path) -> Result<()> {
    let build_summary = build_and_pack(&Project::checkout(root))?;
    println!("core-tests: {build_summary}");
    Ok(())
}

// ---------------------------------------------------------------------------
// Project layout and configuration
// ---------------------------------------------------------------------------

/// A checkout, or a temporary copy of one, whose programs are built.
pub struct Project {
    pub root: PathBuf,
    /// For a copy, the temporary directory `root` is: deleted with the project.
    copy: Option<TempDir>,
}

impl Project {
    pub fn checkout(root: &Path) -> Self {
        Self { root: root.to_path_buf(), copy: None }
    }

    /// A temporary copy of the checkout at `root` with the given Veryl.toml, or the
    /// checkout's own with `None`.
    pub fn temporary_copy(root: &Path, manifest: Option<&str>) -> Result<Self> {
        let copy = temporary_copy(root, manifest)?;
        Ok(Self { root: copy.path().to_path_buf(), copy: Some(copy) })
    }

    /// Keeps a temporary copy instead of deleting it, and returns its path.
    fn keep(self) -> PathBuf {
        match self.copy {
            Some(copy) => copy.keep(),
            None => self.root,
        }
    }

    pub fn programs(&self) -> PathBuf {
        self.root.join("rtl/blocks/core/test/programs")
    }

    /// The local riscv-tests environment: pin file and header.
    fn riscv_tests(&self) -> PathBuf {
        self.programs().join("riscv-tests")
    }

    fn firmware(&self) -> PathBuf {
        self.programs().join("firmware")
    }

    pub fn output(&self) -> PathBuf {
        self.root.join("build/core-tests")
    }

    /// An upstream checkout the setup scripts fetched (`common::RISCV_TESTS`,
    /// `common::COREMARK`), after checking that it is at the revision `pin_file`
    /// pins.
    pub fn pinned_upstream(&self, upstream: &str, pinned: &str, pin_file: &str) -> Result<PathBuf> {
        let repository = crate::common::upstream(upstream, pinned)?;
        ensure!(repository.join(".git").is_dir(), "{upstream} is not fetched. {SETUP}");
        let revision = run(Command::new("git").arg("-C").arg(&repository).args(["rev-parse", "HEAD"]))?;
        let revision = revision.trim();
        ensure!(revision == pinned, "{upstream} is at {revision}, {pin_file} pins {pinned}. {SETUP}");
        Ok(repository)
    }

    /// The memory capacities in this project's Veryl.toml.
    pub fn capacities(&self) -> Result<MemoryCapacities> {
        memory_capacities(&self.root)
    }

    /// The bases of IMEM and DMEM in this project's core map (`core_map_word`).
    pub fn memory_bases(&self) -> Result<MemoryBases> {
        Ok(MemoryBases { imem: core_map_word(&self.root, "IMEM_BASE")?, dmem: core_map_word(&self.root, "DMEM_BASE")? })
    }
}

/// rtl/blocks/core/test/programs/riscv-tests/riscv-tests.toml
#[derive(Deserialize)]
struct RiscvTests {
    /// Upstream groups: every test in a group's list runs, less the exclusions.
    groups: Vec<String>,
    upstream: Upstream,
    /// "group/name" -> reason.
    exclusions: BTreeMap<String, String>,
}

/// The pinned revision of an upstream repository, in riscv-tests.toml and
/// coremark.toml.
#[derive(Deserialize)]
pub struct Upstream {
    pub revision: String,
}

/// rtl/chip/profiles.toml
#[derive(Deserialize)]
struct Profiles {
    profile: Vec<Profile>,
}

/// A profile for the matrix: the Veryl.toml `[properties]` it sets, at least one;
/// the others keep Veryl.toml's values.
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Profile {
    name: String,
    #[serde(rename = "MESH_SIZE")]
    mesh_size: Option<u64>,
    #[serde(rename = "IMEM_BYTES")]
    imem_bytes: Option<u64>,
    #[serde(rename = "DMEM_BYTES")]
    dmem_bytes: Option<u64>,
    #[serde(rename = "SCRATCHPAD_BYTES")]
    scratchpad_bytes: Option<u64>,
    #[serde(rename = "LPDDR_ADDRESS_BITS")]
    lpddr_address_bits: Option<u64>,
    #[serde(rename = "RW_BUFFER_BYTES")]
    rw_buffer_bytes: Option<u64>,
    #[serde(rename = "RW_QUEUE_BEATS")]
    rw_queue_beats: Option<u64>,
    #[serde(rename = "STREAM_BYTES")]
    stream_bytes: Option<u64>,
}

impl Profile {
    /// The properties the profile sets, as (Veryl.toml key, value), in Veryl.toml's
    /// order.
    fn properties(&self) -> Vec<(&'static str, u64)> {
        [
            ("MESH_SIZE", self.mesh_size),
            ("IMEM_BYTES", self.imem_bytes),
            ("DMEM_BYTES", self.dmem_bytes),
            ("SCRATCHPAD_BYTES", self.scratchpad_bytes),
            ("LPDDR_ADDRESS_BITS", self.lpddr_address_bits),
            ("RW_BUFFER_BYTES", self.rw_buffer_bytes),
            ("RW_QUEUE_BEATS", self.rw_queue_beats),
            ("STREAM_BYTES", self.stream_bytes),
        ]
        .into_iter()
        .filter_map(|(key, value)| value.map(|value| (key, value)))
        .collect()
    }
}

// ---------------------------------------------------------------------------
// Upstream selection
// ---------------------------------------------------------------------------

/// Every test of the selected groups' upstream lists, less the exclusions, as
/// (group, name). Every exclusion must name an upstream test.
fn selected_tests(checkout: &Path, tests: &RiscvTests) -> Result<Vec<(String, String)>> {
    let mut upstream_tests = BTreeSet::new();
    for group in &tests.groups {
        let makefrag = fs::read_to_string(checkout.join(format!("isa/{group}/Makefrag")))?;
        let marker = format!("{group}_sc_tests = \\");
        let Some((_, body)) = makefrag.split_once(&marker) else {
            bail!("{group}/Makefrag has no '{marker}' list");
        };
        let body = body.split_once("\n\n").map_or(body, |(list, _)| list);
        for name in body.split(|c: char| c == '\\' || c.is_whitespace()).filter(|name| !name.is_empty()) {
            upstream_tests.insert(format!("{group}/{name}"));
        }
    }
    let unknown: Vec<_> = tests.exclusions.keys().filter(|test| !upstream_tests.contains(*test)).collect();
    ensure!(unknown.is_empty(), "riscv-tests.toml excludes tests the selected groups do not have: {unknown:?}");
    let selected: Vec<(String, String)> = upstream_tests
        .iter()
        .filter(|test| !tests.exclusions.contains_key(*test))
        .filter_map(|test| test.split_once('/').map(|(group, name)| (group.to_string(), name.to_string())))
        .collect();
    ensure!(!selected.is_empty(), "the riscv-tests selection is empty");
    Ok(selected)
}

// ---------------------------------------------------------------------------
// Building programs
// ---------------------------------------------------------------------------

/// One program of a program list.
pub struct Program {
    pub name: String,
    pub elf: PathBuf,
}

/// A bare-metal GCC link for the core's ISA. The caller adds the rest.
pub fn bare_metal_gcc() -> Result<Command> {
    let mut command = Command::new(gcc_tool("gcc")?);
    command.arg(format!("-march={MARCH}")).arg(format!("-mabi={ABI}")).args(["-nostdlib", "-nostartfiles", "-static"]);
    Ok(command)
}

/// A bare-metal GCC link of a program with the firmware's startup code and linker
/// script, for the configured capacities. The caller adds sources and `-o`.
pub fn gcc(project: &Project, capacities: &MemoryCapacities) -> Result<Command> {
    let firmware = project.firmware();
    let mut command = bare_metal_gcc()?;
    command
        .arg("-T")
        .arg(firmware.join("link.ld"))
        .arg(format!("-Wl,--defsym=IMEM_BYTES={}", capacities.imem_bytes))
        .arg(format!("-Wl,--defsym=DMEM_BYTES={}", capacities.dmem_bytes))
        .arg(firmware.join("start.S"));
    Ok(command)
}

fn build_upstream(
    project: &Project,
    upstream: &Path,
    group: &str,
    name: &str,
    capacities: &MemoryCapacities,
) -> Result<PathBuf> {
    let elf = project.output().join(format!("elf/{group}-{name}.elf"));
    run(gcc(project, capacities)?
        .arg(format!("-I{}", project.riscv_tests().display()))
        .arg(format!("-I{}", upstream.join("isa/macros/scalar").display()))
        .arg(upstream.join(format!("isa/{group}/{name}.S")))
        .arg("-o")
        .arg(&elf))?;
    Ok(elf)
}

fn build_isa_edges(project: &Project, capacities: &MemoryCapacities) -> Result<PathBuf> {
    let elf = project.output().join("elf/firmware-isa_edges.elf");
    run(gcc(project, capacities)?.arg(project.firmware().join("isa_edges.S")).args(["-lgcc", "-o"]).arg(&elf))?;
    Ok(elf)
}

/// Builds the Rust firmware with the toolchain pinned in rust-toolchain.toml, with
/// the Cargo and Rust homes of mise's environment (`common::cargo`).
fn build_rust_firmware(project: &Project, capacities: &MemoryCapacities) -> Result<PathBuf> {
    // One argument per flag, separated by 0x1f (CARGO_ENCODED_RUSTFLAGS).
    let rustflags = [
        RUST_FEATURES.to_string(),
        format!("-Clink-arg=-T{}", project.firmware().join("link.ld").display()),
        format!("-Clink-arg=--defsym=IMEM_BYTES={}", capacities.imem_bytes),
        format!("-Clink-arg=--defsym=DMEM_BYTES={}", capacities.dmem_bytes),
    ];
    let target_dir = project.output().join("rust");
    run(cargo()?
        .args(["build", "--release", "--locked", "--offline", "--target", RUST_TARGET, "--target-dir"])
        .arg(&target_dir)
        .current_dir(project.firmware())
        .env("CARGO_ENCODED_RUSTFLAGS", rustflags.join("\x1f")))?;
    let elf = project.output().join("elf/firmware-rust.elf");
    fs::copy(target_dir.join(format!("{RUST_TARGET}/release/core-firmware")), &elf)?;
    Ok(elf)
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

/// The core's instructions (rtl/blocks/core/spec.md §1) by extension, each a
/// list of mnemonics as `objdump -M no-aliases` prints them.
const ISA_MNEMONICS: [&str; 6] = [
    // RV32I. objdump prints FENCE as fence or fence.tso.
    "lui auipc jal jalr beq bne blt bge bltu bgeu lb lh lw lbu lhu sb sh sw addi slti sltiu xori ori andi slli srli \
     srai add sub sll slt sltu xor srl sra or and fence fence.tso ecall ebreak",
    // C without floating point. c.slli64, c.srli64 and c.srai64 are objdump's names
    // for the zero-shift HINTs, c.unimp for the all-zero illegal instruction.
    "c.addi4spn c.lw c.sw c.nop c.addi c.jal c.li c.addi16sp c.lui c.srli c.srai c.andi c.sub c.xor c.or c.and c.j \
     c.beqz c.bnez c.slli c.lwsp c.jr c.mv c.ebreak c.jalr c.add c.swsp c.slli64 c.srli64 c.srai64 c.unimp",
    // Zmmul.
    "mul mulh mulhsu mulhu",
    // Zicsr. unimp is objdump's name for csrrw zero, cycle, zero, which traps here.
    "csrrw csrrs csrrc csrrwi csrrsi csrrci unimp",
    // Zicond.
    "czero.eqz czero.nez",
    // Machine mode.
    "mret wfi",
];

/// Whether a mnemonic printed by `objdump -M no-aliases` may appear in an image:
/// an instruction of the core's ISA, or a word objdump does not decode as an
/// instruction (data, or an encoding of an extension the program does not
/// declare), which it prints as a directive such as `.word` or `.insn`.
fn is_allowed(mnemonic: &str) -> bool {
    mnemonic.starts_with('.') || ISA_MNEMONICS.iter().any(|group| group.split_whitespace().any(|name| name == mnemonic))
}

pub fn parse_elf(data: &[u8]) -> Result<ElfFile32<'_, Endianness>> {
    ElfFile32::parse(data).context("not a 32-bit ELF file")
}

/// Rejects an image containing an instruction outside the core's ISA, as objdump
/// decodes it (`is_allowed`), and writes its disassembly next to it.
///
/// isa_edges deliberately executes unimplemented instructions to check that they
/// trap; those are accepted only between its `unimplemented_instructions_*` labels.
pub fn audit(elf: &Path) -> Result<()> {
    let disassembly = run(Command::new(gcc_tool("objdump")?).args(["-d", "-M", "no-aliases"]).arg(elf))?;
    fs::write(elf.with_extension("dis"), &disassembly)?;

    let data = fs::read(elf)?;
    let file = parse_elf(&data).with_context(|| elf.display().to_string())?;
    let symbol_address = |wanted: &str| {
        file.symbols().find(|symbol| symbol.name() == Ok(wanted)).map(|symbol| symbol.address()).unwrap_or(0)
    };
    let trap_tests =
        symbol_address("unimplemented_instructions_start")..symbol_address("unimplemented_instructions_end");

    // Instruction lines: "<address>:\t<encoding>\t<mnemonic>[\t<operands>]".
    let found: Vec<String> = disassembly
        .lines()
        .filter_map(|line| {
            let [address, _, mnemonic, ..] = line.split('\t').collect::<Vec<_>>()[..] else {
                return None;
            };
            let address = u64::from_str_radix(address.trim().strip_suffix(':')?, 16).ok()?;
            let mnemonic = mnemonic.trim();
            (!is_allowed(mnemonic) && !trap_tests.contains(&address)).then(|| format!("{mnemonic} at {address:#x}"))
        })
        .collect();
    ensure!(
        found.is_empty(),
        "{} contains instructions outside the core's ISA ({MARCH}, MRET and WFI): {found:?}",
        elf.display()
    );
    Ok(())
}

/// A program's memory images, as the boot load writes them: IMEM's and DMEM's
/// contents as word lists from each one's base, from the load (physical) addresses
/// of the ELF's loadable segments, each its file bytes and then zeros to its memory
/// size (`.bss`), as an ELF loader fills it. Gaps are zero. Every loaded byte must
/// be in IMEM or DMEM.
pub struct MemoryImages {
    pub imem: Vec<u32>,
    pub dmem: Vec<u32>,
}

pub fn memory_images(elf: &Path, capacities: &MemoryCapacities, bases: MemoryBases) -> Result<MemoryImages> {
    let data = fs::read(elf)?;
    let file = parse_elf(&data).with_context(|| elf.display().to_string())?;
    let endian = file.endian();
    let mut imem = Vec::new();
    let mut dmem = Vec::new();
    for segment in file.elf_program_headers().iter().filter(|segment| segment.p_type(endian) == elf::PT_LOAD) {
        let bytes = segment.data(endian, &*data).ok().with_context(|| format!("{}: bad segment", elf.display()))?;
        let memory_bytes = u64::from(segment.p_memsz(endian));
        if memory_bytes == 0 {
            continue; // nothing to load
        }
        let start = u64::from(segment.p_paddr(endian));
        let end = start + memory_bytes;
        let within = |base: u32, bytes: u64| start >= u64::from(base) && end <= u64::from(base) + bytes;
        let (image, base) = if within(bases.imem, capacities.imem_bytes) {
            (&mut imem, bases.imem)
        } else if within(bases.dmem, capacities.dmem_bytes) {
            (&mut dmem, bases.dmem)
        } else {
            bail!(
                "{} has bytes outside IMEM ({} bytes) and DMEM ({} bytes) at {start:#x}..{end:#x}",
                elf.display(),
                capacities.imem_bytes,
                capacities.dmem_bytes
            );
        };
        let (start, end) = ((start - u64::from(base)) as usize, (end - u64::from(base)) as usize);
        if image.len() < end {
            image.resize(end, 0);
        }
        image[start..start + bytes.len()].copy_from_slice(bytes);
        image[start + bytes.len()..end].fill(0);
    }
    let words = |image: Vec<u8>| -> Vec<u32> {
        let mut image = image;
        image.resize(image.len().next_multiple_of(4), 0);
        image.chunks_exact(4).map(|word| u32::from_le_bytes([word[0], word[1], word[2], word[3]])).collect()
    };
    Ok(MemoryImages { imem: words(imem), dmem: words(dmem) })
}

/// Builds and audits every program and writes programs.txt, then does the same for
/// the tile programs (tile_tests.rs). Returns a one-line summary.
fn build_and_pack(project: &Project) -> Result<String> {
    let core_summary = build_and_pack_core(project)?;
    let tile_summary = tile_tests::build_and_pack(project)?;
    Ok(format!("{core_summary}; {tile_summary}"))
}

/// The core's programs alone, as `core-images` builds them, for the test of the
/// partition whose tests read them (partitions.rs). Returns a one-line summary.
pub fn build_core_images(root: &Path) -> Result<String> {
    build_and_pack_core(&Project::checkout(root))
}

/// Builds and audits every program and writes programs.txt, and builds the boot
/// ROM's program. Returns a one-line summary.
fn build_and_pack_core(project: &Project) -> Result<String> {
    let programs = build_programs(project)?;
    write_program_list(project, CYCLE_LIMIT, &programs, &project.capacities()?)?;
    let boot_rom = build_boot_rom(project)?;
    Ok(format!(
        "{} programs -> build/core-tests/programs.txt; the boot ROM -> {}",
        programs.len(),
        boot_rom.strip_prefix(&project.root).unwrap_or(&boot_rom).display()
    ))
}

/// The boot ROM's base, the reset PC, in the base tile specification's map (its
/// 5.1), where the boot ROM's program is linked.
const BOOT_ROM_BASE: u32 = 0x0200_0000;

/// Assembles the boot ROM's program (the module's comment) into
/// build/core-tests/boot_rom.elf, and audits it. Returns its path.
fn build_boot_rom(project: &Project) -> Result<PathBuf> {
    let elf = project.output().join("boot_rom.elf");
    run(bare_metal_gcc()?
        .arg(format!("-Wl,-Ttext={BOOT_ROM_BASE:#x}"))
        .arg(project.root.join("rtl/blocks/core/src/boot_rom.S"))
        .arg("-o")
        .arg(&elf))?;
    audit(&elf)?;
    Ok(elf)
}

/// Builds every program for the project's capacities, into build/core-tests/elf/,
/// and audits each.
pub fn build_programs(project: &Project) -> Result<Vec<Program>> {
    let tests: RiscvTests = load_toml(&project.riscv_tests().join("riscv-tests.toml"))?;
    let upstream = project.pinned_upstream(RISCV_TESTS, &tests.upstream.revision, "riscv-tests.toml")?;
    let capacities = project.capacities()?;
    fs::create_dir_all(project.output().join("elf"))?;

    let mut programs = Vec::new();
    for (group, name) in selected_tests(&upstream, &tests)? {
        let elf = build_upstream(project, &upstream, &group, &name, &capacities)?;
        programs.push(Program { name: format!("{group}/{name}"), elf });
    }
    programs.push(Program { name: "firmware/isa_edges".into(), elf: build_isa_edges(project, &capacities)? });
    programs.push(Program { name: "firmware/rust".into(), elf: build_rust_firmware(project, &capacities)? });
    programs.push(coremark::test_program(project, &capacities)?);
    for program in &programs {
        audit(&program.elf)?;
    }
    Ok(programs)
}

/// Writes the program list, build/core-tests/programs.txt, after checking that
/// every program fits in IMEM and DMEM (`memory_images`); every run has
/// `cycle_limit`. Returns the list's path.
pub fn write_program_list(
    project: &Project,
    cycle_limit: u32,
    programs: &[Program],
    capacities: &MemoryCapacities,
) -> Result<PathBuf> {
    let bases = project.memory_bases()?;
    let output = project.output();
    let mut list = String::from(
        "# The core's programs, for program_test (rtl/blocks/core/test/src/program_test.rs), written by\n\
         # cargo xtask core-images (xtask/src/core_tests.rs): per line, the cycle limit, the ELF file\n\
         # (relative to this directory) and the name.\n",
    );
    for program in programs {
        memory_images(&program.elf, capacities, bases)?;
        let elf = program
            .elf
            .strip_prefix(&output)
            .with_context(|| format!("{} is not in {}", program.elf.display(), output.display()))?;
        ensure!(!elf.to_string_lossy().contains(' '), "{} has a space in its path", elf.display());
        list += &format!("{cycle_limit} {} {}\n", elf.display(), program.name);
    }
    let path = output.join("programs.txt");
    fs::write(&path, list)?;
    Ok(path)
}

/// The Rust program_test on the programs of `list` in `mode`, its output
/// uncaptured, run by Cargo in the checkout at `root` (its models built from the
/// checkout's design, which `common::build` refreshes first). The caller pipes or
/// collects its output.
pub fn program_test(root: &Path, list: &Path, mode: Mode) -> Result<Command> {
    crate::common::build(root)?;
    let mut command = cargo()?;
    command
        .args(["test", "--quiet", "--locked", "--offline", "--package", TEST_CRATE, "--lib", PROGRAM_TEST])
        .args(["--", "--exact", "--nocapture"])
        .env("PROGRAM_TEST_LIST", list)
        .env("PROGRAM_TEST_MODE", mode.name())
        .current_dir(root);
    Ok(command)
}

/// The lines of program_test's standard output as the test wrote them, without
/// libtest's notice "test <name> has been running for over <n> seconds". The harness
/// writes that notice to the same standard output as the test, whose lines reach
/// it in buffered pieces (program_test.rs), so the notice can land inside one of
/// the test's lines and split it in two: the line's start, the notice, then its
/// end on the next line. `next` takes each line read and returns the test's line it
/// completes, if any; a line with a notice inside is joined again, and anything
/// else, malformed or not, is returned as read, for its parser to judge.
#[derive(Default)]
pub struct TestOutputLines {
    /// The start of a line that a notice cut off.
    start: String,
}

impl TestOutputLines {
    pub fn next(&mut self, line: &str) -> Option<String> {
        match before_long_running_notice(line) {
            Some(start) => {
                self.start.push_str(start);
                None
            }
            None => Some(std::mem::take(&mut self.start) + line),
        }
    }
}

/// What precedes libtest's long-running notice when `line` ends with one (the
/// notice ends its line).
fn before_long_running_notice(line: &str) -> Option<&str> {
    line.match_indices("test ").find_map(|(index, _)| {
        let notice = &line[index + "test ".len()..];
        let (name, rest) = notice.split_once(' ')?;
        let seconds = rest.strip_prefix("has been running for over ")?.strip_suffix(" seconds")?;
        let well_formed = !name.is_empty() && !seconds.is_empty() && seconds.bytes().all(|byte| byte.is_ascii_digit());
        well_formed.then_some(&line[..index])
    })
}

// ---------------------------------------------------------------------------
// Profile matrix
// ---------------------------------------------------------------------------

/// The chip's configuration profiles, which `test-core-matrix` runs.
const PROFILES: &str = "rtl/chip/profiles.toml";

/// `cargo xtask test-core-matrix`: runs the Rust tests at every profile, each in
/// its own copy, at once (the module's comment), and prints each profile's verdict.
pub fn run_matrix(root: &Path) -> Result<()> {
    let profiles: Profiles = load_toml(&root.join(PROFILES))?;
    // Prepare every profile before running any, so a bad one fails immediately.
    let manifest = fs::read_to_string(root.join("Veryl.toml"))?;
    let manifests =
        profiles.profile.iter().map(|profile| with_properties(&manifest, profile)).collect::<Result<Vec<_>>>()?;
    let excluded = default_only_packages(root)?;

    println!("core-tests: running {} profiles at once, without {excluded:?}", profiles.profile.len());
    let excluded = excluded.as_slice();
    let runs: Vec<Result<ProfileRun>> = thread::scope(|scope| {
        let threads: Vec<_> =
            manifests.iter().map(|manifest| scope.spawn(move || run_profile(root, manifest, excluded))).collect();
        threads
            .into_iter()
            .map(|thread| thread.join().unwrap_or_else(|_| Err(anyhow!("the profile's thread panicked"))))
            .collect()
    });

    let mut failed = Vec::new();
    for (profile, run) in profiles.profile.iter().zip(runs) {
        let settings: Vec<String> = profile.properties().iter().map(|(key, value)| format!("{key}={value}")).collect();
        let label = format!("{} ({})", profile.name, settings.join(", "));
        let run = match run {
            Ok(run) => run,
            Err(error) => {
                // The copy could not even be made: nothing to keep.
                println!("core-tests: ERROR {label}: {error:#}");
                failed.push(profile.name.clone());
                continue;
            }
        };

        let passed: u64 = run.output.lines().filter_map(passed_count).sum();
        let verdict = if run.passed { "PASS" } else { "FAIL" };
        println!("core-tests: {verdict}  {label}  {passed} tests passed");
        if run.passed {
            continue;
        }
        failed.push(profile.name.clone());
        for line in failure_lines(&run.output) {
            println!("{line}");
        }
        fs::write(run.project.root.join("output.log"), &run.output)?;
        let kept = run.project.keep();
        println!("core-tests: kept {} (output.log, Veryl.toml, build/core-tests/)", kept.display());
    }
    ensure!(failed.is_empty(), "profiles failed: {failed:?}");
    Ok(())
}

/// Veryl.toml with the profile's properties in place of the configured ones (a
/// replaced value loses its own comment, which described it); everything else,
/// comments included, is kept as written.
fn with_properties(manifest: &str, profile: &Profile) -> Result<String> {
    let settings = profile.properties();
    ensure!(!settings.is_empty(), "profile '{}' in profiles.toml sets no property", profile.name);
    let mut document: toml_edit::DocumentMut = manifest.parse().context("Veryl.toml")?;
    let properties = document
        .get_mut("properties")
        .and_then(|item| item.as_table_like_mut())
        .context("Veryl.toml has no [properties] table")?;
    for (key, setting) in settings {
        let value = properties.get_mut(key).with_context(|| format!("Veryl.toml [properties] has no {key}"))?;
        *value = toml_edit::value(i64::try_from(setting)?);
    }
    Ok(document.to_string())
}

/// One profile's run: its copy, whether everything passed, and what the build and
/// the tests reported.
struct ProfileRun {
    project: Project,
    passed: bool,
    output: String,
}

/// Copies the project with the given Veryl.toml, builds the programs, lints the
/// design (`common::lint`) and runs the Rust tests there (`rust_tests`). A failure to copy is an error, not a failed
/// run.
fn run_profile(root: &Path, manifest: &str, excluded: &[String]) -> Result<ProfileRun> {
    let project = Project::temporary_copy(root, Some(manifest))?;
    let run = build_and_pack(&project).and_then(|summary| {
        crate::common::lint(&project.root)?;
        let (passed, output) = rust_tests(root, &project.root, excluded)?;
        Ok((passed, format!("core-tests: {summary}\n{output}")))
    });
    let (passed, output) = run.unwrap_or_else(|error| (false, format!("{error:#}")));
    Ok(ProfileRun { project, passed, output })
}

/// The files of Cargo's workspace outside rtl/, which a copy needs to build the
/// partitions' test crates: the manifests and lock file, and xtask's sources, a
/// member of the workspace.
const WORKSPACE_FILES: [&str; 3] = ["Cargo.toml", "Cargo.lock", "xtask/Cargo.toml"];
const XTASK_SOURCES: &str = "xtask/src";

/// Runs every Rust test of the workspace in the copy at `copy` but xtask's
/// (profile-independent) and the `excluded` packages', on models built from the
/// copy's design: whether all passed, and Cargo's output.
fn rust_tests(root: &Path, copy: &Path, excluded: &[String]) -> Result<(bool, String)> {
    for file in WORKSPACE_FILES {
        fs::create_dir_all(copy.join(file).parent().context("a file in a directory")?)?;
        fs::copy(root.join(file), copy.join(file))?;
    }
    fs::create_dir_all(copy.join(XTASK_SOURCES))?;
    for entry in fs::read_dir(root.join(XTASK_SOURCES))? {
        let entry = entry?;
        fs::copy(entry.path(), copy.join(XTASK_SOURCES).join(entry.file_name()))?;
    }
    crate::common::build(copy)?;
    let mut command = cargo()?;
    command.args(["test", "--locked", "--offline", "--no-fail-fast", "--workspace", "--exclude", "xtask"]);
    for package in excluded {
        command.args(["--exclude", package]);
    }
    let output = command.current_dir(copy).output().context("cannot run cargo test")?;
    let text = format!("{}{}", String::from_utf8_lossy(&output.stdout), String::from_utf8_lossy(&output.stderr));
    Ok((output.status.success(), text))
}

/// The test packages of the partitions whose tests run at the default
/// configuration alone (`matrix = false` in their manifests).
fn default_only_packages(root: &Path) -> Result<Vec<String>> {
    Ok(partitions(root)?.iter().filter(|partition| !partition.manifest.matrix).map(Partition::package).collect())
}

/// The lines of Cargo's output that report a failure: a failed test, a test's
/// panic, a build error.
fn failure_lines(output: &str) -> Vec<&str> {
    output
        .lines()
        .filter(|line| line.contains("FAILED") || line.contains("panicked at") || line.starts_with("error"))
        .collect()
}

/// The tests passed, from a "test result:" line of a Rust test binary.
fn passed_count(line: &str) -> Option<u64> {
    let rest = line.strip_prefix("test result: ")?;
    let (_, passed) = rest.split_once(". ")?;
    passed.split_once(" passed")?.0.parse().ok()
}

#[cfg(test)]
mod tests {
    use super::{Profiles, TestOutputLines, default_only_packages, is_allowed, passed_count, with_properties};
    use std::path::Path;

    /// The test's lines through `TestOutputLines`, as read.
    fn test_lines(read: &[&str]) -> Vec<String> {
        let mut lines = TestOutputLines::default();
        read.iter().filter_map(|line| lines.next(line)).collect()
    }

    #[test]
    fn a_long_running_notice_on_its_own_line_is_dropped() {
        let notice = "test program_test::program_test has been running for over 60 seconds";
        assert_eq!(test_lines(&["rvfi: 0 1", notice, "rvfi: 1 2"]), ["rvfi: 0 1", "rvfi: 1 2"]);
    }

    #[test]
    fn a_line_split_by_a_long_running_notice_is_joined_again() {
        let read = [
            "rvfi-run: 0 1",
            "rvfi: 3 0 0 80test program_test::program_test has been running for over 60 seconds",
            "00 2 1",
            "rvfi: 4 0 0 8002",
        ];
        assert_eq!(test_lines(&read), ["rvfi-run: 0 1", "rvfi: 3 0 0 8000 2 1", "rvfi: 4 0 0 8002"]);
    }

    /// Anything but the notice exactly is left to the parser: a line that mentions
    /// a test, or a notice cut short, misspelt or with more after it.
    #[test]
    fn lines_like_a_notice_are_kept_as_read() {
        let read = [
            "test program_test::program_test ... ok",
            "rvfi: 3 test x has been running for over 60 seconds now",
            "rvfi: 3 test x has been running for over sixty seconds",
            "rvfi: 3 test  has been running for over 60 seconds",
            "test x has been running for over",
        ];
        assert_eq!(test_lines(&read), read);
    }

    /// A sample of each part of the ISA (RV32I, C, Zmmul, Zicsr, Zicond, machine
    /// mode), then words objdump does not decode as instructions.
    #[test]
    fn the_core_isa_passes_the_audit() {
        let allowed = "lui auipc jalr bgeu lhu sb srai sltu fence fence.tso ecall ebreak \
                       c.addi4spn c.lw c.swsp c.jal c.lui c.srli c.mv c.ebreak c.slli64 c.unimp \
                       mul mulh mulhsu mulhu csrrw csrrsi unimp czero.eqz czero.nez mret wfi \
                       .insn .word .short";
        for mnemonic in allowed.split_whitespace() {
            assert!(is_allowed(mnemonic), "{mnemonic} is in the core's ISA");
        }
    }

    /// M's division, Zifencei, F, A, Zba, Zbb, Zcb, Zicbom, V and RV64, and the
    /// supervisor and debug instructions objdump decodes for the core's -march.
    #[test]
    fn instructions_outside_the_core_isa_fail_the_audit() {
        let rejected = "div remu fence.i flw fadd.s c.flw c.fsdsp lr.w amoadd.w amoswap.w.aqrl sh1add clz c.mul \
                        c.lbu cbo.clean vadd.vv mulw c.addiw sret sfence.vma dret";
        for mnemonic in rejected.split_whitespace() {
            assert!(!is_allowed(mnemonic), "{mnemonic} is outside the core's ISA");
        }
    }

    /// The chip's tests alone run at the default configuration only.
    #[test]
    fn only_the_chips_tests_leave_the_matrix() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).parent().expect("xtask is in the workspace");
        assert_eq!(default_only_packages(root).expect("the partitions' manifests parse"), ["chip"]);
    }

    #[test]
    fn rust_test_results_are_counted() {
        assert_eq!(passed_count("test result: ok. 12 passed; 0 failed; 0 ignored; 0 measured"), Some(12));
        assert_eq!(passed_count("test result: FAILED. 3 passed; 1 failed; 0 ignored"), Some(3));
        assert_eq!(passed_count("running 4 tests"), None);
    }

    const MANIFEST: &str = "\
[project]
name = \"x\"

[properties]
MESH_SIZE = 8
# Per-tile byte capacities.
IMEM_BYTES = 32768 # 32 KiB
DMEM_BYTES = 32768
SCRATCHPAD_BYTES = 4194304
";

    fn profiles(text: &str) -> Profiles {
        toml::from_str(text).expect("profiles parse")
    }

    /// A profile replaces only the properties it sets, and keeps the rest of
    /// Veryl.toml as written, but for a replaced value's comment.
    #[test]
    fn a_profile_sets_only_its_properties() {
        let mesh = profiles("[[profile]]\nname = \"5x5\"\nMESH_SIZE = 5\n");
        let manifest = with_properties(MANIFEST, &mesh.profile[0]).unwrap();
        assert_eq!(manifest, MANIFEST.replace("MESH_SIZE = 8", "MESH_SIZE = 5"));

        let memory = profiles("[[profile]]\nname = \"m\"\nIMEM_BYTES = 65536\nSCRATCHPAD_BYTES = 512\n");
        let manifest = with_properties(MANIFEST, &memory.profile[0]).unwrap();
        let expected = MANIFEST.replace("IMEM_BYTES = 32768 # 32 KiB", "IMEM_BYTES = 65536").replace("4194304", "512");
        assert_eq!(manifest, expected);
    }

    /// A profile that sets nothing would repeat the default run, and an unknown key
    /// is a typo.
    #[test]
    fn a_profile_must_set_known_properties() {
        let empty = profiles("[[profile]]\nname = \"default again\"\n");
        assert!(with_properties(MANIFEST, &empty.profile[0]).is_err());
        assert!(toml::from_str::<Profiles>("[[profile]]\nname = \"typo\"\nMESH = 5\n").is_err());
        // A property Veryl.toml does not have.
        let without_mesh = MANIFEST.replace("MESH_SIZE = 8\n", "");
        let mesh = profiles("[[profile]]\nname = \"5x5\"\nMESH_SIZE = 5\n");
        assert!(with_properties(&without_mesh, &mesh.profile[0]).is_err());
    }
}
