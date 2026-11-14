//! The chip's runs, built by `sort-images`: the sorter's tile sort software on Snarky One's tile, the memory tile's
//! firmware, the master tile's firmware for each run, and the sort runs' keys. Only builds programs and their input:
//! the chip's tests (rtl/chip/test/src/) load them, compute each run's input and expected results themselves (sort.rs)
//! and run them on a Verilator model of the chip.
//!
//! Sources, in `rtl/chip/test/programs/`:
//!
//! ```text
//! sort/      the tile image: meshsim v1.25's sort, its split and its skew planner, vendored, on the port layer
//!            (sort/port/: mesh.h, crt0.S, port.c, sort.ld); sort/PORTING.md lists every change
//! firmware/  the chip's firmware, Rust, its own Cargo workspace: the master tile's (src/bin/master.rs), the memory
//!            tile's (src/bin/memory.rs), and the segment's images, which the master loads: the compute tiles'
//!            replay image (src/bin/replay.rs) and the tracer tile's (src/bin/tracer.rs), on the base tile's
//!            registers (src/lib.rs)
//! ```
//!
//! Every image is linked at the base tile specification's map (rtl/tile/base/spec.md 5.1, 6.3; `IMEM_BASE`,
//! `DMEM_FIRST`), the code in IMEM from its base and the data in DMEM past its 16 reserved bytes, the tile image's
//! past its arguments' row too (`ARGUMENTS`, `TILE_DATA`: sort/port/mesh.h).
//!
//! The tile image is built as meshsim's sort/build.sh builds it (`-O2 -fno-jump-tables`, function and data sections,
//! its warnings, `--no-relax`, `--gc-sections`, the split as `split_bisect.o` with `-DSPLIT_BARRIER=sort_barrier`) for
//! the core's ISA (core_tests.rs's MARCH) with the pinned xPack GCC, and the C preprocessor gets the configuration of
//! Veryl.toml (MESH_SIZE, IMEM_BYTES, DMEM_BYTES, SCRATCHPAD_BYTES) for the sources and the link script, with
//! `-Werror`, `--orphan-handling=error` and `--fatal-warnings` beside. It is audited like the core's programs
//! (core_tests.rs's `audit`), without a libgcc division or multiplication helper (the sorter's check), and every WFI
//! in it must keep the port's rules (`check_wfis`). Its layout table (port.c, `.sort_layout`: its magic, then the
//! buffers BUF_IN, BUF_VIN, BUF_Y and BUF_VS) must be present.
//!
//! The firmware is built with the Rust target and features of the core's firmware: the memory tile's once, which the
//! chip's tests place in the memory tile's IMEM and DMEM before the reset ends (its boot strap, 6.5), and the master's
//! once per run, with the run linked in: the run image (`run_image`), made an object by objcopy whose name
//! carries the image's digest, so that a new image changes the link flags and Cargo relinks. Each is audited likewise,
//! without a division helper, and the master's summary (`.summary`, src/bin/master.rs, `Summary`) must be DMEM's first
//! data. Each image must fit its IMEM and DMEM (the link scripts' checks); the listing gives the headroom.
//!
//! The runs: the 8x8 mesh's sorts, one with 32-bit values (`kv32`) and one with 128-bit values (`k128`) at each size
//! (`Size`): 16 records per tile, which every tier runs, and 64K, the most a tile's input area holds, which the
//! release tier runs (`kv32-64k`, `k128-64k`), each with the tile image, which the master loads on every compute tile
//! with the run's arguments (6.5); the memory run (`memory`), with no image; and the trace run (`trace`), with the
//! segment's two images, linked at loaded.ld's map, the replay image for every compute tile and the tracer tile's
//! (`build_loaded`, `trace_run_image`).
//! The keys are SortTest's built-in `uniform` set (meshsim/test/SortTest.java, `keys`, from java.util.Random at its
//! seed), written as SortTest's key file, so that meshsim can run on exactly this input and the chip's test reads the
//! same keys; the values are SortTest's functions of (tile, position), which the chip's test computes.
//!
//! `sort-images` writes to `build/sort/`:
//!
//! ```text
//! sort.keys        the keys of the runs of 16 records per tile, SortTest's key file (readKeys): little-endian
//!                  32-bit words, the magic 0x4B455953, the 64 tiles' record counts, then the keys tile by tile
//! sort-64k.keys    those of the runs of 64K
//! sort.txt         the images' sizes and headroom, the runs' addresses
//! tile/            the tile image: its objects, sort.elf, its disassembly and map
//! memory-tile/     the memory tile's firmware, memory.elf, and its disassembly
//! segment/         the segment's images, replay.elf and tracer.elf, and their disassemblies
//! kv32/, k128/,    each run's run image, its object, the master's ELF (master.elf) and its disassembly
//! kv32-64k/,
//! k128-64k/,
//! memory/, trace/
//! ```

use crate::common::{MemoryCapacities, cargo, gcc_tool, hex_digest, run, veryl_properties};
use crate::core_tests::{self, MemoryBases, Project, RUST_FEATURES, RUST_TARGET};
use anyhow::{Context, Result, bail, ensure};
use fs_err as fs;
use object::read::elf::ProgramHeader;
use object::{Object, ObjectSection, ObjectSymbol, elf};
use std::path::{Path, PathBuf};
use std::process::Command;

/// The master's run image (its src/bin/master.rs reads it): magic, then its header's other words.
const RUN_MAGIC: u32 = 0x5352_554E; // "SRUN"
/// The run's kinds (src/bin/master.rs): a sort, the memory run, or the trace run.
const KIND_SORT: u32 = 0;
const KIND_MEMORY: u32 = 1;
const KIND_TRACE: u32 = 2;
/// The tile image's layout table (port.c, `.sort_layout`): its magic, then BUF_IN, BUF_VIN, BUF_Y and BUF_VS.
const LAYOUT_MAGIC: u32 = 0x4C41_594F; // "LAYO"

/// The base tile specification's map (5.1, 6.3): IMEM's base, region 0x01, and DMEM's, region 0x00, whose first 16
/// bytes are reserved, so that an image's data starts at DMEM_FIRST.
const IMEM_BASE: u32 = 0x0100_0000;
const DMEM_BASE: u32 = 0x0000_0000;
const DMEM_FIRST: u32 = 0x10;
/// The tile image's arguments' row (sort/port/mesh.h, ARGS_BASE): word 0 the records per tile, word 1 the format;
/// and its data, past that row (DMEM_DATA).
const ARGUMENTS: u32 = DMEM_FIRST;
const TILE_DATA: u32 = ARGUMENTS + 16;
/// The sort design's mesh: 64 tiles, an 8x8 mesh (the sort's NT and its 64-way schedule).
const MESH_SIZE: u64 = 8;
const TILES: usize = 64;
/// SortTest's built-in set and its seed, `11 + kind.hashCode()`.
const KEY_SET: &str = "uniform";

const SOURCES: &str = "rtl/chip/test/programs";
const OUTPUT: &str = "build/sort";

/// The sorter's flags (its build.sh), beside the core's ISA.
const TILE_FLAGS: [&str; 6] =
    ["-ffreestanding", "-fno-builtin", "-Wall", "-Wno-array-bounds", "-Werror", "-Wl,--no-relax"];
const TILE_OPTIMIZATION: [&str; 4] = ["-O2", "-fno-jump-tables", "-ffunction-sections", "-fdata-sections"];
/// The sorter's check: no library arithmetic in the tile image.
const TILE_FORBIDDEN: [&str; 6] = ["__udivsi3", "__umodsi3", "__divsi3", "__modsi3", "__mulsi3", "__muldi3"];

/// No division helper in the firmware.
const FIRMWARE_FORBIDDEN: [&str; 8] =
    ["__udivsi3", "__umodsi3", "__divsi3", "__modsi3", "__udivdi3", "__umoddi3", "__divdi3", "__moddi3"];
/// The vendored and port C sources of the tile image, from sort/; the split is built on its own (`build_tile_image`).
const TILE_C_SOURCES: [&str; 12] = [
    "sw/bt_plan.c",
    "sw/bt_run.c",
    "sw/config.c",
    "sw/exchange.c",
    "sw/local.c",
    "sw/main.c",
    "sw/skew.c",
    "sw/transfer.c",
    "skew-planner/skew_common.c",
    "skew-planner/skew_th.c",
    "skew-planner/skew_bt.c",
    "port/port.c",
];
/// Its assembly, linked from source: crt0, the image's first code.
const TILE_ASSEMBLY: &str = "port/crt0.S";

/// The run's record format, sort.h's FMT_*.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Format {
    Kv32 = 0,
    K128 = 1,
}

impl Format {
    fn name(self) -> &'static str {
        match self {
            Format::Kv32 => "kv32",
            Format::K128 => "k128",
        }
    }
}

/// A run's size: its records per tile, the same on every tile.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Size {
    /// 16 records per tile, which every tier's chip_test runs.
    Small,
    /// 64K records per tile, the most a tile's 512 KiB input area holds (sort.h, AREA), which the release tier runs.
    Full,
}

impl Size {
    fn records(self) -> usize {
        match self {
            Size::Small => 16,
            Size::Full => 65536,
        }
    }

    /// The suffix of its runs' directories and of its key file: none, or "-64k".
    fn suffix(self) -> &'static str {
        match self {
            Size::Small => "",
            Size::Full => "-64k",
        }
    }
}

/// A sort run's directory in build/sort/: kv32, k128, kv32-64k or k128-64k.
fn run_name(format: Format, size: Size) -> String {
    format!("{}{}", format.name(), size.suffix())
}

/// `cargo xtask sort-images`
pub fn build_images(root: &Path) -> Result<()> {
    let summary = build(root)?;
    println!("sort-images: {summary}");
    Ok(())
}

/// What the build takes from the project: the configuration.
struct Configuration {
    root: PathBuf,
    capacities: MemoryCapacities,
}

impl Configuration {
    fn output(&self) -> PathBuf {
        self.root.join(OUTPUT)
    }

    fn sources(&self) -> PathBuf {
        self.root.join(SOURCES)
    }

    /// The C preprocessor's view of the configuration (port/mesh.h).
    fn defines(&self) -> Vec<String> {
        vec![
            format!("-DMESH_SIZE={MESH_SIZE}"),
            format!("-DIMEM_BYTES={}", self.capacities.imem_bytes),
            format!("-DDMEM_BYTES={}", self.capacities.dmem_bytes),
            format!("-DSCRATCHPAD_BYTES={}", self.capacities.scratchpad_bytes),
        ]
    }

    /// The bases a program's loadable bytes go to: IMEM's and DMEM's (core_tests.rs's memory images).
    fn bases(&self) -> MemoryBases {
        MemoryBases { imem: IMEM_BASE, dmem: DMEM_BASE }
    }
}

/// An image the master loads on a tile (6.5): its IMEM rows from IMEM's base, where the boot ROM enters it, and its
/// DMEM rows from `dmem_address`, four words each; and its arguments' row's address, or 0 for none.
struct Image {
    imem: Vec<u32>,
    dmem: Vec<u32>,
    dmem_address: u32,
    arguments: u32,
}

fn build(root: &Path) -> Result<String> {
    let project = Project::checkout(root);
    let capacities = project.capacities()?;
    let properties = veryl_properties(root)?;
    let mesh_size =
        properties.get("MESH_SIZE").and_then(toml::Value::as_integer).context("Veryl.toml has no MESH_SIZE")?;
    ensure!(
        u64::try_from(mesh_size)? == MESH_SIZE && capacities.scratchpad_bytes == 4 << 20,
        "the sort is the sort design's 64-tile, 4 MiB sort: Veryl.toml must set MESH_SIZE 8 and SCRATCHPAD_BYTES 4 MiB \
         (it sets {mesh_size} and {})",
        capacities.scratchpad_bytes
    );
    let config = Configuration { root: root.to_path_buf(), capacities };
    fs::create_dir_all(config.output())?;

    let tile = build_tile_image(&config)?;
    let mut listing = tile.listing(&config);
    let memory = build_memory_tile(&config)?;
    listing += &format!(
        "memory tile (build/sort/memory-tile/memory.elf): IMEM {} bytes, DMEM {} bytes from {:#x}\n",
        4 * memory.imem.len(),
        4 * memory.dmem.len(),
        memory.dmem_address
    );
    let no_image = Image { imem: Vec::new(), dmem: Vec::new(), dmem_address: 0, arguments: 0 };
    for size in [Size::Small, Size::Full] {
        let keys = sort_test_keys(KEY_SET, size.records());
        write_key_file(&config.output().join(format!("sort{}.keys", size.suffix())), &keys)?;
        for format in [Format::Kv32, Format::K128] {
            let header = [KIND_SORT, format as u32, size.records() as u32];
            let master = build_master(&config, &run_image(header, &tile.image), &run_name(format, size))?;
            listing += &master.listing(&config, &run_name(format, size));
        }
    }
    let master = build_master(&config, &run_image([KIND_MEMORY, 0, 0], &no_image), "memory")?;
    listing += &master.listing(&config, "memory");
    let replay = build_loaded(&config, "replay")?;
    let tracer = build_loaded(&config, "tracer")?;
    for (name, image) in [("replay", &replay), ("tracer", &tracer)] {
        listing += &format!(
            "{name} image (build/sort/segment/{name}.elf): IMEM {} bytes, DMEM {} bytes loaded from {:#x}\n",
            4 * image.imem.len(),
            4 * image.dmem.len(),
            image.dmem_address
        );
    }
    let master = build_master(&config, &trace_run_image(&replay, &tracer), "trace")?;
    listing += &master.listing(&config, "trace");
    listing += &format!(
        "buffers (scratchpad byte addresses): BUF_IN {:#x}, BUF_VIN {:#x}, BUF_Y {:#x}, BUF_VS {:#x}\n",
        4 * tile.layout.buf_in,
        4 * tile.layout.buf_vin,
        4 * tile.layout.buf_y,
        4 * tile.layout.buf_vs,
    );
    fs::write(config.output().join("sort.txt"), &listing)?;
    Ok(format!(
        "tile image {} of {} IMEM bytes, 6 runs -> build/sort/kv32/, k128/, kv32-64k/, k128-64k/, memory/, \
         trace/ (master.elf), segment/ (replay.elf, tracer.elf), sort.keys, sort-64k.keys (sizes: build/sort/sort.txt)",
        tile.code_bytes, config.capacities.imem_bytes
    ))
}

// ---------------------------------------------------------------------------
// The tile image
// ---------------------------------------------------------------------------

/// The buffers' word addresses, from the tile image's layout table (port.c).
struct Layout {
    buf_in: u32,
    buf_vin: u32,
    buf_y: u32,
    buf_vs: u32,
}

struct TileImage {
    image: Image,
    layout: Layout,
    code_bytes: u64,
    /// DMEM: the loaded data's end and .bss's end (`_end`), from DMEM's base; the stack's top, from DMEM's base.
    data_end: u64,
    bss_end: u64,
    stack_top: u64,
    /// The largest stack frame (-fstack-usage) and its function.
    largest_frame: (u64, String),
}

impl TileImage {
    fn listing(&self, config: &Configuration) -> String {
        format!(
            "tile image (build/sort/tile/sort.elf):\n  IMEM: {} of {} bytes, {} free\n  DMEM: loaded data to {} bytes, \
             .bss to {} bytes, {} bytes free for the stack below the reply block at {}; the largest frame {} bytes ({})\n",
            self.code_bytes,
            config.capacities.imem_bytes,
            config.capacities.imem_bytes - self.code_bytes,
            self.data_end,
            self.bss_end,
            self.stack_top - self.bss_end,
            self.stack_top,
            self.largest_frame.0,
            self.largest_frame.1
        )
    }
}

fn tile_gcc(config: &Configuration) -> Result<Command> {
    let sources = config.sources().join("sort");
    let mut command = core_tests::bare_metal_gcc()?;
    command.args(TILE_FLAGS).args(config.defines());
    for include in ["sw", "skew-planner", "port"] {
        command.arg(format!("-I{}", sources.join(include).display()));
    }
    Ok(command)
}

fn build_tile_image(config: &Configuration) -> Result<TileImage> {
    let sources = config.sources().join("sort");
    let output = config.output().join("tile");
    if output.exists() {
        fs::remove_dir_all(&output)?;
    }
    fs::create_dir_all(&output)?;

    // The objects, each with its stack usage (.su) beside it.
    let mut objects = Vec::new();
    for source in TILE_C_SOURCES {
        let stem = Path::new(source).file_stem().context("a source file")?.to_string_lossy().into_owned();
        let object = output.join(format!("{stem}.o"));
        run(tile_gcc(config)?
            .args(TILE_OPTIMIZATION)
            .arg("-fstack-usage")
            .arg("-c")
            .arg(sources.join(source))
            .arg("-o")
            .arg(&object))?;
        objects.push(object);
    }
    // The split, its barriers the sort's (sort_barrier, main.c).
    let split = output.join("split_bisect.o");
    run(tile_gcc(config)?
        .args(TILE_OPTIMIZATION)
        .args(["-fstack-usage", "-DSPLIT_BARRIER=sort_barrier"])
        .arg(format!("-I{}", sources.join("split-bisect").display()))
        .arg("-c")
        .arg(sources.join("split-bisect/split.c"))
        .arg("-o")
        .arg(&split))?;
    objects.push(split);

    // The link script, through the C preprocessor as assembly sees the map (port/mesh.h).
    let script = output.join("sort.ld");
    run(Command::new(gcc_tool("cpp")?)
        .args(["-P", "-undef", "-x", "c", "-D__ASSEMBLER__"])
        .args(config.defines())
        .arg(format!("-I{}", sources.join("port").display()))
        .arg(sources.join("port/sort.ld"))
        .arg("-o")
        .arg(&script))?;

    let elf = output.join("sort.elf");
    let mut link = tile_gcc(config)?;
    link.arg("-T")
        .arg(&script)
        .args(["-Wl,--gc-sections", "-Wl,--orphan-handling=error", "-Wl,--build-id=none", "-Wl,--fatal-warnings"])
        .arg(format!("-Wl,-Map={}", output.join("sort.map").display()));
    run(link.arg(sources.join(TILE_ASSEMBLY)).args(&objects).arg("-o").arg(&elf))?;

    core_tests::audit(&elf)?;
    forbid_symbols(&elf, &TILE_FORBIDDEN)?;
    check_wfis(&elf.with_extension("dis"))?;

    let data = fs::read(&elf)?;
    let file = core_tests::parse_elf(&data).with_context(|| elf.display().to_string())?;
    let symbol = |name: &str| -> Result<u64> {
        file.symbols()
            .find(|symbol| symbol.name() == Ok(name))
            .map(|symbol| symbol.address())
            .with_context(|| format!("{}: no symbol {name}", elf.display()))
    };
    ensure!(symbol("_start")? == u64::from(IMEM_BASE), "{}: _start is not at IMEM's base", elf.display());
    let bss_end = symbol("_end")? - u64::from(DMEM_BASE);

    let layout = file.section_by_name(".sort_layout").context("the tile image has no .sort_layout")?.data()?;
    let layout: Vec<u32> = layout.chunks_exact(4).map(|word| u32::from_le_bytes(word.try_into().unwrap())).collect();
    let [magic, buf_in, buf_vin, buf_y, buf_vs] = layout[..] else {
        bail!("{}: .sort_layout holds {} words, not 5", elf.display(), layout.len());
    };
    ensure!(magic == LAYOUT_MAGIC, "{}: .sort_layout's magic is {magic:#x}", elf.display());

    let (imem, dmem) = loaded_rows(&elf, &data, config)?;
    let code_bytes = 4 * imem.len() as u64;
    let data_end = u64::from(TILE_DATA) + 4 * dmem.len() as u64;
    // The stack's top: the reply block's base, DMEM's top 1 KiB (port/mesh.h).
    let stack_top = config.capacities.dmem_bytes - 1024;
    Ok(TileImage {
        image: Image { imem, dmem, dmem_address: TILE_DATA, arguments: ARGUMENTS },
        layout: Layout { buf_in, buf_vin, buf_y, buf_vs },
        code_bytes,
        data_end,
        bss_end,
        stack_top,
        largest_frame: largest_frame(&output)?,
    })
}

/// The bytes the master lands on a tile: each loadable segment's file bytes, the code from IMEM's base and the data
/// from TILE_DATA, each padded with zeros to whole 16-byte rows (.bss is not landed: crt0 zeroes it).
fn loaded_rows(path: &Path, data: &[u8], config: &Configuration) -> Result<(Vec<u32>, Vec<u32>)> {
    let file = core_tests::parse_elf(data)?;
    let endian = file.endian();
    let mut imem = None;
    let mut dmem = None;
    for segment in file.elf_program_headers() {
        if segment.p_type(endian) != elf::PT_LOAD || segment.p_filesz(endian) == 0 {
            continue;
        }
        let address = segment.p_paddr(endian);
        let bytes = segment.data(endian, data).ok().with_context(|| format!("{}: bad segment", path.display()))?;
        let mut bytes = bytes.to_vec();
        bytes.resize(bytes.len().next_multiple_of(16), 0);
        let words: Vec<u32> = bytes.chunks_exact(4).map(|word| u32::from_le_bytes(word.try_into().unwrap())).collect();
        let slot = if address == IMEM_BASE {
            ensure!(
                bytes.len() as u64 <= config.capacities.imem_bytes,
                "{}: the code does not fit IMEM",
                path.display()
            );
            &mut imem
        } else if address == TILE_DATA {
            &mut dmem
        } else {
            bail!("{}: a loaded segment at {address:#x}, neither IMEM's base nor TILE_DATA", path.display());
        };
        ensure!(slot.is_none(), "{}: two loaded segments at {address:#x}", path.display());
        *slot = Some(words);
    }
    Ok((imem.with_context(|| format!("{}: no code at IMEM's base", path.display()))?, dmem.unwrap_or_default()))
}

/// The largest frame in the objects' stack usage files (`<function>\t<bytes>\t<kind>` lines).
fn largest_frame(directory: &Path) -> Result<(u64, String)> {
    let mut largest = (0, String::new());
    for entry in fs::read_dir(directory)? {
        let path = entry?.path();
        if path.extension().is_some_and(|extension| extension == "su") {
            for line in fs::read_to_string(&path)?.lines() {
                let fields: Vec<&str> = line.split('\t').collect();
                if let [function, bytes, ..] = fields[..]
                    && let Ok(bytes) = bytes.parse::<u64>()
                    && bytes > largest.0
                {
                    largest = (bytes, function.rsplit(':').next().unwrap_or(function).to_string());
                }
            }
        }
    }
    Ok(largest)
}

/// Fails if the ELF defines any of `names` (a library helper linked in).
fn forbid_symbols(path: &Path, names: &[&str]) -> Result<()> {
    let data = fs::read(path)?;
    let file = core_tests::parse_elf(&data)?;
    let found: Vec<&str> =
        file.symbols().filter_map(|symbol| symbol.name().ok()).filter(|name| names.contains(name)).collect();
    ensure!(found.is_empty(), "{} links library arithmetic: {found:?}", path.display());
    Ok(())
}

/// The tile window's EVENTS and ARRIVE as the disassembly prints an x0-relative access (port/mesh.h).
const EVENTS_OFFSET: &str = "-2036";
const ARRIVE_OFFSET: &str = "-2024";

/// Every WFI in the tile image keeps the port's rules (port/mesh.h): each is followed at once by a load of EVENTS,
/// which consumes what it returns of the events the wait was for (`wfi()`, `wait_all()`), and the barrier's
/// (`arrive_and_wait()`, in `sort_barrier()` alone, main.c) comes at once after a store to ARRIVE, the canonical wait
/// (rtl/tile/base/spec.md 7.3), so that every phase ends at the one WFI. `disassembly` is objdump's, with no aliases
/// (core_tests.rs's `audit` writes it).
fn check_wfis(disassembly: &Path) -> Result<()> {
    let counts = wfi_kinds(&fs::read_to_string(disassembly)?).with_context(|| disassembly.display().to_string())?;
    ensure!(
        counts[1] == 1,
        "{}: expected the barrier's one WFI after ARRIVE, found {counts:?} (waits, barriers)",
        disassembly.display()
    );
    Ok(())
}

/// The WFIs of a disassembly by kind: a wait (a load of EVENTS after it) and a barrier's (a store to ARRIVE before it
/// as well); an error names one that is neither.
fn wfi_kinds(text: &str) -> Result<[usize; 2]> {
    // Instruction lines: "<address>:\t<encoding>\t<mnemonic>[\t<operands>]".
    let instructions: Vec<(String, String)> = text
        .lines()
        .filter_map(|line| {
            let fields: Vec<&str> = line.split('\t').collect();
            let [address, _, mnemonic, ..] = fields[..] else { return None };
            let address = address.trim().strip_suffix(':')?;
            let operands = fields.get(3).map_or("", |operands| operands.split('#').next().unwrap_or("").trim());
            Some((format!("{} {operands}", mnemonic.trim()), format!("0x{address}")))
        })
        .collect();
    let mut counts = [0; 2];
    for (index, (instruction, address)) in instructions.iter().enumerate() {
        if instruction.trim() != "wfi" {
            continue;
        }
        let at = |offset: isize| -> &str {
            index.checked_add_signed(offset).and_then(|i| instructions.get(i)).map_or("", |(text, _)| text.as_str())
        };
        let loads_events = matches!(access(at(1), "lw"), Some((_, EVENTS_OFFSET, "zero")));
        let arrives = matches!(access(at(-1), "sw"), Some((_, ARRIVE_OFFSET, "zero")));
        ensure!(loads_events, "the WFI at {address} keeps none of the port's patterns (port/mesh.h)");
        counts[usize::from(arrives)] += 1;
    }
    Ok(counts)
}

/// A load's or a store's register, offset and base register, as objdump prints `lw`, `c.lw`, `sw` and `c.sw`.
fn access<'a>(instruction: &'a str, mnemonic: &str) -> Option<(&'a str, &'a str, &'a str)> {
    let operands = instruction
        .strip_prefix(&format!("{mnemonic} "))
        .or_else(|| instruction.strip_prefix(&format!("c.{mnemonic} ")))?;
    let (register, address) = operands.split_once(',')?;
    let (offset, base) = address.strip_suffix(')')?.split_once('(')?;
    Some((register, offset, base))
}

// ---------------------------------------------------------------------------
// The firmware
// ---------------------------------------------------------------------------

/// The firmware's crate, and a build of one of its programs into `target_dir`, with `link_args` beside its link
/// script and the capacities: its ELF file, copied to `elf` and audited.
fn build_firmware(
    config: &Configuration,
    program: &str,
    script: &str,
    link_args: &[String],
    target_dir: &Path,
    elf: &Path,
) -> Result<()> {
    let firmware = config.sources().join("firmware");
    // One argument per flag, separated by 0x1f (CARGO_ENCODED_RUSTFLAGS).
    let mut rustflags = vec![
        RUST_FEATURES.to_string(),
        format!("-Clink-arg=-T{}", firmware.join(script).display()),
        format!("-Clink-arg=--defsym=IMEM_BYTES={}", config.capacities.imem_bytes),
        format!("-Clink-arg=--defsym=DMEM_BYTES={}", config.capacities.dmem_bytes),
    ];
    rustflags.extend(link_args.iter().cloned());
    run(cargo()?
        .args(["build", "--release", "--locked", "--offline", "--bin", program, "--target", RUST_TARGET])
        .arg("--target-dir")
        .arg(target_dir)
        .current_dir(&firmware)
        .env("CARGO_ENCODED_RUSTFLAGS", rustflags.join("\x1f")))?;
    fs::copy(target_dir.join(format!("{RUST_TARGET}/release/{program}")), elf)?;
    core_tests::audit(elf)?;
    forbid_symbols(elf, &FIRMWARE_FORBIDDEN)
}

/// The memory tile's firmware, its sizes for the listing: its code from IMEM's base and its data, .bss's zeros
/// included (its start.S does not zero .bss), from DMEM_FIRST, in whole rows; the chip's tests place its ELF file's
/// images themselves.
fn build_memory_tile(config: &Configuration) -> Result<Image> {
    let output = config.output().join("memory-tile");
    fs::create_dir_all(&output)?;
    let elf = output.join("memory.elf");
    build_firmware(config, "memory", "link.ld", &[], &output.join("rust"), &elf)?;
    let images = core_tests::memory_images(&elf, &config.capacities, config.bases())?;
    let rows = |mut words: Vec<u32>| {
        words.resize(words.len().next_multiple_of(4), 0);
        words
    };
    let first = (DMEM_FIRST / 4) as usize;
    Ok(Image {
        imem: rows(images.imem),
        dmem: rows(images.dmem.get(first..).unwrap_or_default().to_vec()),
        dmem_address: DMEM_FIRST,
        arguments: 0,
    })
}

/// One of the segment's images, which the master loads (6.5) in the trace run: the compute tiles' replay image or the
/// tracer tile's (src/bin/replay.rs, src/bin/tracer.rs), linked at loaded.ld's map, its code from IMEM's base and its
/// loaded data from TILE_DATA, in whole rows; its start-up zeroes .bss, which is not landed. No arguments: the
/// segment's input is in the tiles' scratchpads, which the chip's test writes (rtl/chip/test/src/chip_trace_test.rs).
fn build_loaded(config: &Configuration, program: &str) -> Result<Image> {
    let output = config.output().join("segment");
    fs::create_dir_all(&output)?;
    let elf = output.join(format!("{program}.elf"));
    build_firmware(config, program, "loaded.ld", &[], &output.join("rust"), &elf)?;
    let data = fs::read(&elf)?;
    let (imem, dmem) = loaded_rows(&elf, &data, config)?;
    Ok(Image { imem, dmem, dmem_address: TILE_DATA, arguments: 0 })
}

/// The trace run's image (src/bin/master.rs): the run image of the replay image, then the tracer tile's image, its IMEM
/// rows, its DMEM rows and their load address, then its rows.
fn trace_run_image(replay: &Image, tracer: &Image) -> Vec<u32> {
    let mut words = run_image([KIND_TRACE, 0, 0], replay);
    words.extend([(tracer.imem.len() / 4) as u32, (tracer.dmem.len() / 4) as u32, tracer.dmem_address]);
    words.extend(&tracer.imem);
    words.extend(&tracer.dmem);
    words
}

struct MasterImage {
    elf: PathBuf,
    code_bytes: u64,
    /// DMEM: the end of .bss, from DMEM's base, the summary's size and the run image's.
    end_bytes: u64,
    summary_bytes: u64,
    run_bytes: u64,
}

impl MasterImage {
    fn listing(&self, config: &Configuration, name: &str) -> String {
        // The master's stack ends at DMEM's top.
        let stack_top = config.capacities.dmem_bytes;
        format!(
            "master, {} ({}):\n  IMEM: {} of {} bytes, {} free\n  DMEM: {} of {} bytes (the summary {} bytes, the run \
             image {} bytes), {} free for the stack\n",
            name,
            self.elf.strip_prefix(&config.root).unwrap_or(&self.elf).display(),
            self.code_bytes,
            config.capacities.imem_bytes,
            config.capacities.imem_bytes - self.code_bytes,
            self.end_bytes,
            config.capacities.dmem_bytes,
            self.summary_bytes,
            self.run_bytes,
            stack_top - self.end_bytes
        )
    }
}

/// The master's run image (its src/bin/master.rs, `run_image`): magic, kind, format, records per tile, the image's
/// arguments' address, the tile count, its IMEM rows, its DMEM rows and their load address; then its IMEM rows and
/// its DMEM rows. `header` is the kind, the format and the records.
fn run_image(header: [u32; 3], image: &Image) -> Vec<u32> {
    let [kind, format, records] = header;
    let mut words = vec![
        RUN_MAGIC,
        kind,
        format,
        records,
        image.arguments,
        TILES as u32,
        (image.imem.len() / 4) as u32,
        (image.dmem.len() / 4) as u32,
        image.dmem_address,
    ];
    words.extend(&image.imem);
    words.extend(&image.dmem);
    words
}

fn build_master(config: &Configuration, run_words: &[u32], name: &str) -> Result<MasterImage> {
    let output = config.output().join(name);
    fs::create_dir_all(&output)?;
    let image: Vec<u8> = run_words.iter().flat_map(|word| word.to_le_bytes()).collect();
    fs::write(output.join("run.bin"), &image)?;

    // The run image as an object with one symbol, RUN_IMAGE, at its start, in read-only data; named by its digest,
    // so that a new image changes the link flags and Cargo relinks.
    let object = output.join(format!("run-{}.o", &hex_digest(&image)[..16]));
    if !object.exists() {
        run(Command::new(gcc_tool("objcopy")?)
            .current_dir(&output)
            .args(["-I", "binary", "-O", "elf32-littleriscv", "-B", "riscv"])
            .args(["--rename-section", ".data=.rodata.run_image,alloc,load,readonly,data,contents"])
            .args(["--set-section-alignment", ".rodata.run_image=16"])
            .args(["--redefine-sym", "_binary_run_bin_start=RUN_IMAGE"])
            .args(["--strip-symbol", "_binary_run_bin_end", "--strip-symbol", "_binary_run_bin_size"])
            .arg("run.bin")
            .arg(&object))?;
    }

    let elf = output.join("master.elf");
    build_firmware(
        config,
        "master",
        "link.ld",
        &[format!("-Clink-arg={}", object.display())],
        &output.join("rust"),
        &elf,
    )?;
    let images = core_tests::memory_images(&elf, &config.capacities, config.bases())?;
    let data = fs::read(&elf)?;
    let file = core_tests::parse_elf(&data)?;
    let summary = file.section_by_name(".summary").context("the master's firmware has no .summary")?;
    ensure!(
        summary.address() == u64::from(DMEM_FIRST),
        "the master's summary is at {:#x}, not DMEM's first data",
        summary.address()
    );
    Ok(MasterImage {
        elf,
        code_bytes: 4 * images.imem.len() as u64,
        end_bytes: 4 * images.dmem.len() as u64,
        summary_bytes: summary.size(),
        run_bytes: image.len() as u64,
    })
}

// ---------------------------------------------------------------------------
// The runs' keys: SortTest's
// ---------------------------------------------------------------------------

/// java.util.Random: the 48-bit linear congruential generator, as Java specifies it.
struct JavaRandom {
    seed: u64,
}

impl JavaRandom {
    const MULTIPLIER: u64 = 0x5_DEEC_E66D;
    const MASK: u64 = (1 << 48) - 1;

    fn new(seed: i64) -> Self {
        Self { seed: (seed as u64 ^ Self::MULTIPLIER) & Self::MASK }
    }

    /// `nextInt()`: `next(32)`.
    fn next_int(&mut self) -> i32 {
        self.seed = (self.seed.wrapping_mul(Self::MULTIPLIER).wrapping_add(0xB)) & Self::MASK;
        (self.seed >> 16) as u32 as i32
    }
}

/// Java's String.hashCode.
fn java_hash(text: &str) -> i32 {
    text.encode_utf16().fold(0i32, |hash, unit| hash.wrapping_mul(31).wrapping_add(i32::from(unit)))
}

/// SortTest.keys(kind, n, 11 + kind.hashCode()) for the uniform kind: n keys per tile, `nextInt()` each, tile by
/// tile.
fn sort_test_keys(kind: &str, records: usize) -> Vec<Vec<u32>> {
    assert_eq!(kind, "uniform", "only SortTest's uniform set is ported");
    let mut random = JavaRandom::new(i64::from(11i32.wrapping_add(java_hash(kind))));
    (0..TILES).map(|_| (0..records).map(|_| random.next_int() as u32).collect()).collect()
}

/// SortTest's key file (readKeys): the magic, each tile's record count, then the keys tile by tile, little-endian.
fn write_key_file(path: &Path, keys: &[Vec<u32>]) -> Result<()> {
    let mut words = vec![0x4B45_5953u32];
    words.extend(keys.iter().map(|keys| keys.len() as u32));
    words.extend(keys.iter().flatten());
    fs::write(path, words.iter().flat_map(|word| word.to_le_bytes()).collect::<Vec<u8>>())?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{JavaRandom, java_hash, sort_test_keys, wfi_kinds};

    /// java.util.Random and String.hashCode against Java's own values (computed with OpenJDK 21).
    #[test]
    fn java_random_matches_java() {
        let mut random = JavaRandom::new(42);
        assert_eq!([random.next_int(), random.next_int()], [-1_170_105_035, 234_785_527]);
        assert_eq!(java_hash("uniform"), -286_926_412);
        let keys = sort_test_keys("uniform", 4);
        assert_eq!(keys[0], [1_369_945_310u32, -1_470_743_753i32 as u32, 343_795_689, 749_775_144]);
    }

    /// The two WFI patterns are told apart, and a WFI that keeps neither is refused.
    #[test]
    fn every_wfi_keeps_a_port_pattern() {
        let line =
            |address: u32, mnemonic: &str, operands: &str| format!("{address:8x}:\t00000000\t{mnemonic}\t{operands}\n");
        let wait = line(0, "wfi", "") + &line(4, "lw", "a5,-2036(zero) # fffff80c <x>");
        let barrier = line(8, "sw", "a4,-2024(zero)") + &line(12, "wfi", "") + &line(16, "lw", "a5,-2036(zero)");
        assert_eq!(wfi_kinds(&(wait.clone() + &barrier)).unwrap(), [1, 1]);
        // a WFI with no load of EVENTS after it, or with another register's load
        assert!(wfi_kinds(&line(0, "wfi", "")).is_err());
        assert!(wfi_kinds(&wait.replace("-2036(zero)", "-2032(zero)")).is_err());
    }
}
