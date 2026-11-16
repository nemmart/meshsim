//! The tile programs for rtl/tile/base's tests, built by `core-images` with the
//! core's programs (core_tests.rs). Only prepares data: the tests run them on
//! Verilator models of the tile.
//!
//! The programs come in sets, one for each tile design the partition tests, each a
//! directory of `rtl/tile/base/test/programs/` named for that design (`SOURCES`):
//! `base_tile/`, which `base_tile_test` runs. A set is
//! assembly, each file saying what it checks: `tile_map.h` holds its addresses and its
//! protocol with its test (the boot and the reports), `start.S` its startup and
//! report, and `link.ld` its link script. The test boots each program from its ELF
//! file's segments, as the master tile does, so it takes the ELF files, not memory
//! images. Each is built for the core's ISA (core_tests.rs's MARCH) and audited like
//! the core's programs. The C preprocessor gets the mesh size and the capacities
//! configured in Veryl.toml, for the programs and for their link script, so that
//! every address comes from tile_map.h. They are linked with a 16-byte page size, with
//! no more padding than that, each segment starting a 16-byte row, as the boot writes
//! rows (`check_segments`), stripped of their symbols once audited and cut after the
//! last bytes the test reads, with no section headers (`without_sections`), so the
//! files are small and the same on every build.
//! `core-images` writes, for each set, to `build/core-tests/tile/<set>/`:
//!
//! ```text
//! programs.txt   the set's programs, which its test reads: one a line, its kind
//!                and its ELF file's name in this directory ("0 boot.elf")
//! <name>.elf     the ELF files as the test reads them, beside their disassembly,
//!                with the symbols, and the preprocessed link script
//! ```
//!
//! A set's programs, each with its kind, are listed in its `programs.toml`
//! (`PROGRAMS`): a program's kind is its index there, the number its test gives it.
//! Each is linked from start.S and the program, with link.ld.

use crate::common::{gcc_tool, load_toml, run, veryl_properties};
use crate::core_tests::{self, Project};
use anyhow::{Context, Result, ensure};
use fs_err as fs;
use object::elf;
use object::read::elf::{FileHeader, ProgramHeader};
use serde::Deserialize;
use std::path::{Path, PathBuf};
use std::process::Command;

/// The sets' directories, each a set, from the project's root.
const SOURCES: &str = "rtl/tile/base/test/programs";

/// The directory of the sets' built programs, in `build/core-tests/`, and in each
/// set's the list of its programs that its test reads.
const OUTPUT: &str = "tile";
const LIST: &str = "programs.txt";

/// The list of a set's programs, in its directory.
const PROGRAMS: &str = "programs.toml";

/// `PROGRAMS`: the tile programs in kind order.
#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Programs {
    program: Vec<TileProgram>,
}

/// A tile program: its kind, its index in the list, and its name, of its source file
/// `<name>.S`.
#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct TileProgram {
    kind: usize,
    name: String,
}

/// The tile programs of `PROGRAMS` in `directory`, checked: each kind its index,
/// each name once, each with its source file.
fn programs(directory: &Path) -> Result<Vec<TileProgram>> {
    let path = directory.join(PROGRAMS);
    let programs: Programs = load_toml(&path)?;
    ensure!(!programs.program.is_empty(), "{} lists no program", path.display());
    for (index, program) in programs.program.iter().enumerate() {
        ensure!(
            program.kind == index,
            "{}: {} has kind {}, not its index, {index}",
            path.display(),
            program.name,
            program.kind
        );
        ensure!(
            !programs.program[..index].iter().any(|other| other.name == program.name),
            "{}: {} is listed twice",
            path.display(),
            program.name
        );
        let source = directory.join(format!("{}.S", program.name));
        ensure!(source.is_file(), "{}: {} has no {}", path.display(), program.name, source.display());
    }
    Ok(programs.program)
}

/// The tile programs alone, as `core-images` builds them, for the test of the
/// partition whose tests read them (partitions.rs). Returns a one-line summary.
pub fn build_images(root: &Path) -> Result<String> {
    build_and_pack(&Project::checkout(root))
}

/// Builds and audits every set's tile programs for the project's capacities into
/// build/core-tests/tile/<set>/, with their list, programs.txt. Returns a one-line
/// summary.
pub fn build_and_pack(project: &Project) -> Result<String> {
    let capacities = project.capacities()?;
    let mesh_size = veryl_properties(&project.root)?
        .get("MESH_SIZE")
        .and_then(toml::Value::as_integer)
        .context("Veryl.toml has no MESH_SIZE")?;
    let defines = [
        format!("-DMESH_SIZE={mesh_size}"),
        format!("-DIMEM_BYTES={}", capacities.imem_bytes),
        format!("-DDMEM_BYTES={}", capacities.dmem_bytes),
        format!("-DSCRATCHPAD_BYTES={}", capacities.scratchpad_bytes),
    ];
    let mut summary = Vec::new();
    for set in sets(&project.root.join(SOURCES))? {
        let count = build_set(project, &set, &defines)?;
        summary.push(format!("{count} {set}"));
    }
    Ok(format!("tile programs ({}) -> build/core-tests/{OUTPUT}/<set>/", summary.join(", ")))
}

/// The sets, by name: the directories in `sources`, each with its PROGRAMS, in name
/// order.
fn sets(sources: &Path) -> Result<Vec<String>> {
    let mut sets = Vec::new();
    for entry in fs::read_dir(sources)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            let name = entry.file_name().into_string().map_err(|name| anyhow::anyhow!("a set named {name:?}"))?;
            ensure!(entry.path().join(PROGRAMS).is_file(), "{}: no {PROGRAMS}", entry.path().display());
            sets.push(name);
        }
    }
    ensure!(!sets.is_empty(), "{}: no set of tile programs", sources.display());
    sets.sort();
    Ok(sets)
}

/// Builds and audits one set's programs into build/core-tests/tile/<set>/ and writes
/// their list there. Returns how many it built.
fn build_set(project: &Project, set: &str, defines: &[String]) -> Result<usize> {
    let sources = project.root.join(SOURCES).join(set);
    let output = project.output().join(OUTPUT).join(set);
    fs::create_dir_all(&output)?;
    let script = preprocess_link_script(&sources, &output, "link.ld", defines)?;

    let programs = programs(&sources)?;
    let mut list = String::new();
    for TileProgram { kind, name } in &programs {
        let elf = output.join(format!("{name}.elf"));
        let mut command = gcc(defines)?;
        command.arg("-T").arg(&script).arg(sources.join("start.S"));
        run(command.arg(sources.join(format!("{name}.S"))).arg("-o").arg(&elf))?;
        core_tests::audit(&elf)?;
        // The symbols are only for the disassembly, which the audit has written, and
        // name GCC's temporary files: without them the file is the same on every build.
        run(Command::new(gcc_tool("objcopy")?).arg("--strip-all").arg(&elf))?;
        let data = without_sections(&fs::read(&elf)?)?;
        fs::write(&elf, &data)?;
        check_segments(&elf, &data)?;

        list += &format!("{kind} {name}.elf\n");
    }
    fs::write(output.join(LIST), list)?;
    Ok(programs.len())
}

/// A bare-metal GCC link for the core's ISA (`core_tests::bare_metal_gcc`) with the
/// capacities defined and a 16-byte page size. The caller adds the link script, the
/// sources and `-o`.
fn gcc(defines: &[String]) -> Result<Command> {
    let mut command = core_tests::bare_metal_gcc()?;
    command.arg("-Wl,-z,max-page-size=16").args(defines);
    Ok(command)
}

/// A set's link script, through the C preprocessor with the capacities defined,
/// into its output directory. As C, not assembly, so that tile_map.h's assembler
/// macros (under `__ASSEMBLER__`) stay out; `-undef` leaves no macros but
/// tile_map.h's and the capacities, and `-P` no line markers, which ld cannot read.
fn preprocess_link_script(sources: &Path, output: &Path, script: &str, defines: &[String]) -> Result<PathBuf> {
    let preprocessed = output.join(script);
    run(Command::new(gcc_tool("cpp")?)
        .args(["-P", "-undef", "-x", "c"])
        .args(defines)
        .arg(sources.join(script))
        .arg("-o")
        .arg(&preprocessed))?;
    Ok(preprocessed)
}

/// The file cut after the last bytes the test reads, its program header table or a
/// segment's file bytes, with no section header table (e_shoff, e_shnum and
/// e_shstrndx 0): the sections that stripping leaves (`.shstrtab`) and the table
/// lie past those bytes, and the test reads neither.
fn without_sections(data: &[u8]) -> Result<Vec<u8>> {
    let file = core_tests::parse_elf(data)?;
    let endian = file.endian();
    let header = file.elf_header();
    let table_end =
        u64::from(header.e_phoff(endian)) + u64::from(header.e_phnum(endian)) * u64::from(header.e_phentsize(endian));
    let mut end = table_end;
    for segment in file.elf_program_headers() {
        if segment.p_type(endian) == elf::PT_LOAD && segment.p_filesz(endian) != 0 {
            end = end.max(u64::from(segment.p_offset(endian)) + u64::from(segment.p_filesz(endian)));
        }
    }
    let mut packed = data[..usize::try_from(end)?].to_vec();
    // ELF32: e_shoff at byte 32, e_shnum at 48 and e_shstrndx at 50.
    packed[32..36].fill(0);
    packed[48..52].fill(0);
    Ok(packed)
}

/// Checks that each segment the boot writes starts a 16-byte row, which the link
/// script gives: the tests' boots, as the master tile's, write whole rows.
fn check_segments(path: &Path, data: &[u8]) -> Result<()> {
    let file = core_tests::parse_elf(data)?;
    let endian = file.endian();
    for segment in file.elf_program_headers() {
        if segment.p_type(endian) != elf::PT_LOAD || segment.p_memsz(endian) == 0 {
            continue; // the boot skips it
        }
        let address = segment.p_paddr(endian);
        ensure!(address % 16 == 0, "{}: the segment at {address:#x} does not start a 16-byte row", path.display());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{SOURCES, programs, sets, without_sections};
    use fs_err as fs;
    use std::path::Path;

    /// The committed sets and their lists read (`programs` checks each kind, name
    /// and source).
    #[test]
    fn the_committed_programs_read() {
        let sources = Path::new(env!("CARGO_MANIFEST_DIR")).join("..").join(SOURCES);
        for set in sets(&sources).unwrap() {
            assert!(!programs(&sources.join(set)).unwrap().is_empty());
        }
    }

    /// A kind out of order, a name twice, a missing source and an unknown key fail.
    #[test]
    fn a_bad_list_is_refused() {
        let directory = tempfile::tempdir().unwrap();
        fs::write(directory.path().join("a.S"), "").unwrap();
        fs::write(directory.path().join("b.S"), "").unwrap();
        let check = |text: &str| {
            fs::write(directory.path().join("programs.toml"), text).unwrap();
            programs(directory.path()).map(|programs| programs.len())
        };
        let entry = |kind: usize, name: &str| format!("[[program]]\nkind = {kind}\nname = \"{name}\"\n");
        assert_eq!(check(&(entry(0, "a") + &entry(1, "b"))).unwrap(), 2);
        assert!(check(&(entry(1, "a") + &entry(0, "b"))).is_err());
        assert!(check(&(entry(0, "a") + &entry(1, "a"))).is_err());
        assert!(check(&(entry(0, "a") + &entry(1, "c"))).is_err());
        assert!(check(&(entry(0, "a") + "extra = 1\n")).is_err());
    }

    /// The file ends with the last segment's file bytes, and its header names no
    /// section header table.
    #[test]
    fn a_packed_file_ends_after_its_last_segment() {
        let mut file = vec![0u8; 160];
        file[..8].copy_from_slice(&[0x7f, b'E', b'L', b'F', 1, 1, 1, 0]);
        let mut put = |offset: usize, bytes: &[u8]| file[offset..offset + bytes.len()].copy_from_slice(bytes);
        put(16, &[2, 0, 0xf3, 0, 1, 0, 0, 0]); // ET_EXEC, EM_RISCV, EV_CURRENT
        put(28, &52u32.to_le_bytes()); // e_phoff
        put(32, &120u32.to_le_bytes()); // e_shoff: a null section header at 120
        put(40, &[52, 0, 32, 0, 1, 0, 40, 0]); // e_ehsize, e_phentsize, e_phnum 1, e_shentsize
        put(52, &1u32.to_le_bytes()); // PT_LOAD
        put(56, &96u32.to_le_bytes()); // p_offset
        put(68, &8u32.to_le_bytes()); // p_filesz
        put(72, &16u32.to_le_bytes()); // p_memsz
        put(96, &[0xaa; 24]); // the segment's 8 bytes, then 16 the test never reads
        let packed = without_sections(&file).unwrap();
        assert_eq!(packed.len(), 104);
        assert_eq!(packed[96..], [0xaa; 8]);
        assert_eq!(packed[32..36], [0; 4]);
        assert_eq!(packed[48..52], [0; 4]);
        assert_eq!(packed[..32], file[..32]);
    }
}
