// t2_harness.h (WORKER16, G7; AUDIT-M3 sections 0 and 7): the framework's T2 harness, ref's TracerT2.chip and
// W13's M3Seg as the C++ chip runs them. Configuration and between-cycle code only (D6 doesn't apply here).
//
//   - the inputs W13 dumped on the runner (`$DATA/w13-golden-m3`, M3Inputs.java): the memory at the segment's start
//     (`*.files.gz` into the Tracer's FileStore) and the harness's big-SRAM landings before the boot (`*.sram.gz`,
//     C&C 3's tracer_layout.h and every tile's replay_layout.h, poked into the units' big SRAMs);
//   - the boot (Mach.bootCores, Mach.hostBoot): C&C 1's staged images in its big SRAM (from chunk 0x10000), its boot
//     descriptor in its data image, its program, data and first command through the host port; `cores_booted` is
//     Mach.coresBooted (C&C 1's mbox[3] == 2), read between cycles;
//   - CC_SEGMENT (Mach.cc1Command(0x54, 2, 1 << 24, 0x53 << 24)): C&C 1's one command for the segment;
//   - the rows against the delivery's rows.bin: per tile, the row count and the replayer's sink hash (replayer.cpp: h =
//     mix(h ^ word) over each record's ten words, mix the splitmix64 finalizer), computed from rows.bin at setup, so no
//     row is kept during the run (the replayer's optional buffer localises a mismatch);
//   - the partition (AUDIT-M3 section 5): C&C c with tile c (cc_cycle does it), tile 2 and C&C 3 (the critical path)
//     alone in domain 0, the other tiles spread over the rest.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "sim.h"
#include "units/big_sram.h"
#include "units/file_store.h"
#include "units/net.h"
#include "units/tile_ctl.h"

namespace t2 {

// The whole of a gzipped (or plain) file (configuration: `gzip -dc` through popen).
bool read_gz(const std::string& path, std::vector<uint8_t>& out, std::string& err);

// M3Inputs.files: "M3FILES1", nextFree, the page count, then each page's address (128 B units) and its 512 longs.
// Restores the store page by page (FileStore::grow_to, raw_page); `fs` must be fresh (first = PAGE_UNITS, as ref's).
bool load_files(const std::string& path, units::tracer::FileStore& fs, std::string& err, uint64_t* pages = nullptr);

// M3Inputs.sram: "M3SRAM01", then per unit 0-66 with a non-zero word: the unit, the count, then (chunk, value) pairs.
struct SramLanding { uint32_t unit, chunk, value; };
bool load_sram(const std::string& path, std::vector<SramLanding>& out, std::string& err);
// BigSRAM.poke into t<k>.sram and cc<c>.sram (between cycles: before the first, as ref's harness pokes before the boot).
void poke_sram(sim::Sim& m, const std::vector<SramLanding>& l);

// A firmware image (Mach.Image): code, data and the symbols from DIR/NAME.code.bin, .data.bin, .sym.
struct Image {
    std::string name;
    std::vector<uint32_t> code, data;
    std::map<std::string, uint32_t> sym;
    uint32_t at(const char* s) const;                    // FATAL if absent
};
bool load_image(const std::string& dir, const std::string& name, Image& im, std::string& err);

// Mach.bootCores(tile, cc3) then Mach.hostBoot(cc1, data): between cycles, before the boot. `go_latency` and
// `uart_divisor` go into the descriptor (Mach: cfg.goLatency, uartDivisor 0). Wakes C&C 1's host port.
void boot_cores(sim::Sim& m, const Image& cc1, const Image& cc23, const Image& tile, const Image& cc3,
                unsigned go_latency, uint32_t uart_divisor = 0);
// Mach.cc1Command(type, arg, args...): a command into C&C 1's command block through the host port; wakes it.
void cc1_command(sim::Sim& m, uint32_t type, uint32_t arg, const uint32_t* args, size_t nargs);
constexpr uint32_t CC_SEGMENT = 0x54, TL_CMD_START = 0x53;
// CC_SEGMENT as TracerT2 sends it: the tiles' command (1 << 24) and C&C 3's start (TL_CMD_START << 24).
void cc_segment(sim::Sim& m);
struct Machine;
// Mach.coresBooted: C&C 1's mbox[3] == 2 (cc1.c), read from its data memory (`cc1_dmem`: unit 64's MEM_WORDS words).
bool cores_booted(const uint32_t* cc1_dmem, const Image& cc1);
// Runs to Mach.coresBooted (at most `cap` cycles), checked between cycles as ref's runUntil. Returns ref's boot-end point
// (M3Seg: c.cycle - 1; the framework's cycle F is ref's F - 1, so between cycles now() is ref's c.cycle + 1), or -1.
// With `trace`, appends "CYCLE pc1 pc2 pc3 pc_t0" (ref's c.cycle; C&C 1-3's and tile 0's pc, signed) whenever one
// changes, as tests/ref/BootStage.java's trace (tests/m3-images/boottrace.ref.gz).
int64_t run_boot(Machine& m, const Image& cc1, uint64_t cap, std::string* trace = nullptr);

// The replayer's sink hash over n records of ten words.
uint64_t rows_hash(const uint64_t* recs, size_t n, uint64_t h = 0);
// Per tile t < windows: rows.bin of window 64 * segment + t in `delivery` (DATA/w9-deliveries/PROGRAM).
struct RowsRef { uint64_t rows = 0, hash = 0; bool present = false; };
bool rows_ref(const std::string& delivery, unsigned segment, unsigned windows, RowsRef out[64], std::string& err);

// The chip with C&C 1-3 as cores (ChipSpec::cc_cores; C&C 3's DMA), as T2 builds it: M2's tile units with W8's
// builder over 67 units, ref's Config defaults, the boot ROM. Owns the memories the machine points into.
struct Machine {
    units::Hops hops;
    std::vector<uint32_t> mem, bootrom;
    units::SramBlocks sram;
    units::G1Ctx gx;
    std::optional<sim::Sim> sim;
    uint32_t* dmem(unsigned unit) { return mem.data() + size_t(unit) * 2 * units::MEM_WORDS; }
};
// `domains` (64 entries, or null: one domain); `threads` for the split.
// `reqbus` (W14): the request bus's configuration (D34: credit latency 3 the default, 0 exact on one domain).
std::unique_ptr<Machine> build_machine(const uint8_t* domains, sim::Options opt, std::string& err,
                                       const units::reqbus::ReqBusConfig* reqbus = nullptr);

// T2's partition over `parts` domains (1-64): tile 2 (with C&C 3) alone in domain 0; tiles 0, 1 (with C&C 1, 2) and the
// rest round-robin over domains 1 .. parts - 1 (everything in domain 0 when parts is 1).
void partition(unsigned parts, uint8_t dom[64]);

}  // namespace t2
