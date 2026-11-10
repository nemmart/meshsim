// statefp.h: the canonical state fingerprint, the C++ side (WORKER8; sim/tests/AUDIT-M2.md section 6; ref's side is
// sim/tests/ref/StateFp.java, whose per-unit streams are normative).
//
// At the end of cycle c each unit writes a defined sequence of 64-bit words (its architectural state, its memories as
// order-free sums, every bus delivering into it); its fingerprint is a hash of the stream:
//     h = 0x6A09E667F3BCC908; for each word w: h = fmix64(h ^ w); at the end h = fmix64(h ^ n), n the word count.
// A memory is one word: the sum mod 2^64 of fmix64(region << 56 ^ index << 32 ^ value) over its non-zero 32-bit words
// (MemSum keeps it incrementally). A point of a `.fp` file is one line `cycle unit hash` per unit, then `cycle chip hash`
// over those hashes in order (the same hash, over the unit hashes).
//
// The pieces:
//   FpOut       a stream: hashes, and optionally dumps each word with its name (StateFp's `fpdump` format), so the
//               first differing named word can be found by diffing the two sides' dumps
//   MemSum      a memory's sum, updated at every write (two fmix64 and an add); `scan` recomputes it in full (tests)
//   bus writers the three bus kinds' canonical encodings, from entries (the port observer's, or a unit's own)
//   FpRegistry  name -> writer: each unit contributes through one function `void(const void* unit, FpOut&, const
//               FpCtx&)`; W9's and later groups' units plug in the same way; stubs write a not-yet-built unit's reset
//               stream (statefp_stubs.cpp)
//   FpGolden    a golden `.fp` file: its D12 header (checked against ours, refusing on any difference, named) and its
//               points; `compare` finds the first differing cycle and unit
//
// Not in the cycle loop: a test and debug facility (D6's configuration regime: any C++).
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "units/memsum.h"

namespace sim { class Sim; }

namespace fp {

// ------------------------------------------------------------------------------------------------------------ hashing
inline uint64_t fmix(uint64_t k) { return units::fmix64(k); }
constexpr uint64_t kSeed = 0x6A09E667F3BCC908ull;
inline uint64_t term(uint32_t region, uint64_t index, uint32_t value) { return units::mem_term(region, index, value); }
enum : uint32_t { R_DMEM = units::FP_DMEM, R_PMEM = units::FP_PMEM, R_SRAM = units::FP_SRAM,
                  R_STREAM_SRC = units::FP_STREAM_SRC, R_STREAM_DEST = units::FP_STREAM_DEST,
                  R_STREAM_COUNT = units::FP_STREAM_COUNT, R_GROUP = units::FP_GROUP, R_PTR = units::FP_PTR,
                  R_HIST = units::FP_HIST };

// A canonical stream. `w` hashes a word; when dumping, it also prints `unit name hex` (StateFp.Dump's format). Names with
// an index are formatted only when dumping (`wi`: name and index concatenated, as Java's "x" + r).
class FpOut {
public:
    FpOut() = default;
    FpOut(FILE* dump, const char* unit) : dump_(dump), unit_(unit) {}
    void w(const char* name, uint64_t v) {
        h_ = fmix(h_ ^ v); n_++;
        if (dump_) std::fprintf(dump_, "%s %s %llx\n", unit_, name, (unsigned long long)v);
    }
    void wi(const char* name, unsigned i, uint64_t v);                       // name + i, e.g. "x5"
    void wp(const char* prefix, const char* name, uint64_t v);               // prefix + name, e.g. "portB" + ".n"
    void wpi(const char* prefix, unsigned i, const char* name, uint64_t v);  // prefix + i + name, e.g. "ret3" + ".n"
    uint64_t done() const { return fmix(h_ ^ n_); }
    uint64_t words() const { return n_; }
    bool dumping() const { return dump_ != nullptr; }
private:
    uint64_t h_ = kSeed, n_ = 0;
    FILE* dump_ = nullptr;
    const char* unit_ = nullptr;
};

// The hash over a point's unit hashes (the `chip` line).
inline uint64_t combine(const uint64_t* h, size_t n) {
    uint64_t a = kSeed;
    for (size_t i = 0; i < n; i++) a = fmix(a ^ h[i]);
    return fmix(a ^ uint64_t(n));
}

// ------------------------------------------------------------------------------------------------------------ memories
using units::MemSum;   // src/units/memsum.h: kept by the unit owning the memory

// --------------------------------------------------------------------------------------------------------------- buses
// A bus entry as the observer gives it (and as a unit may hold one): its arrival cycle (Links) and three words.
struct BusEntry { uint64_t at; uint64_t a, b, c; };

// The encodings (StateFp.delay, fifo, hold), each as at the end of cycle c:
//   Link:  n, then each entry (at, a, b, c) in arrival order, in flight included (ref's arrival: its put cycle + ref's L)
//   Fifo:  n, the entries (a, b, c), then put (0/1) and, if 1, the put made at c (pa, pb, pc), not yet judged
//   Hold:  occ, its word (a, b, c) if occupied, then put (0/1) and the put made at c
//   none:  a bus ref leaves null (`.none`, 0)
void link(FpOut& o, const char* nm, const BusEntry* e, unsigned n);
void fifo(FpOut& o, const char* nm, const BusEntry* e, unsigned n, const BusEntry* put);
void hold(FpOut& o, const char* nm, const BusEntry* held, const BusEntry* put);
inline void none(FpOut& o, const char* nm) { o.wp(nm, ".none", 0); }
// Empty buses (the stubs until the port observer lands, and units not built yet).
inline void link_empty(FpOut& o, const char* nm) { link(o, nm, nullptr, 0); }
inline void fifo_empty(FpOut& o, const char* nm) { fifo(o, nm, nullptr, 0, nullptr); }
inline void hold_empty(FpOut& o, const char* nm) { hold(o, nm, nullptr, nullptr); }

// ------------------------------------------------------------------------------------------------------------ registry
// What a writer may read besides its unit: the cycle (the end of which is fingerprinted) and the machine (for the port
// observer, once it lands; may be null in tests).
struct FpCtx {
    uint64_t cycle;
    const sim::Sim* sim;
    // W16: the label's convention. `cycle` is always the end of the framework's cycle (M2's: ref's c.cycle, between
    // cycles now() - 1). ref's writers compare event cycles with their `cyc`: M2's StateFp.write passes c.cycle (shift
    // 0); M3's StateFpM3.write passes c.cycle - 1 (shift 1, AUDIT-M3 7). A writer whose stream compares an event cycle
    // with cyc (a sleeping core's mcycle, sendRetry, regLoad, the director's K-line) uses cycle - label_shift there.
    uint64_t label_shift = 0;
};
using FpWriter = void (*)(const void* unit, FpOut& o, const FpCtx& x);

// The units of a tile and of the chip, in StateFp's order (TILE_UNITS, CHIP_UNITS).
extern const char* const kTileUnits[16];
extern const char* const kChipUnits[4];

class FpRegistry {
public:
    // `name` as the .fp file writes it: "t0.core", "cnc", ... One writer per name; a second is fatal.
    void add(const std::string& name, FpWriter w, const void* unit);
    bool has(const std::string& name) const { return by_name_.count(name) != 0; }
    uint64_t hash(const std::string& name, const FpCtx& x) const;                 // fatal if unknown
    void dump(const std::string& name, const FpCtx& x, FILE* out) const;          // "# fpdump cycle C unit U", then words
    // A point as ref writes it (StateFp.write): every tile unit of `tiles` (tile-major, kTileUnits' order, sram.data only
    // if `with_data`), then the chip units, then `chip`; each line "c name hash" appended to `out`.
    void point(const FpCtx& x, const std::vector<unsigned>& tiles, bool with_data, std::vector<std::string>& out) const;
private:
    struct Entry { FpWriter w; const void* unit; };
    std::map<std::string, Entry> by_name_;
};

// ------------------------------------------------------------------------------------------------------- golden files
struct FpLine { uint64_t cycle; std::string unit; uint64_t hash; };
struct FpPoint { uint64_t cycle; std::vector<FpLine> lines; };   // its units in order, `chip` last

struct FpGolden {
    std::string path;
    std::vector<std::string> header;             // the leading `#` lines, as written
    std::map<std::string, std::string> config;   // `# config KEY VALUE`
    std::vector<FpPoint> points;                 // in file order (a cycle may appear twice: a GO and an all-READY point)
    std::vector<std::string> notes;              // `#` lines after the header (e.g. fpwin's check), kept for reports
    std::string error;                           // empty if loaded
    // Load a .fp file; on failure `error` says why (the file and line).
    static FpGolden load(const std::string& path);
    // D12: empty if `ours` (the header lines this run would write, the same format) equals the golden's header, else the
    // first difference, named: a config key with both values, a key on one side only, or a line with both texts.
    std::string check_header(const std::vector<std::string>& ours) const;
    // The points at cycle c (0, 1 or 2 of them).
    std::vector<const FpPoint*> at(uint64_t c) const;
};

// The first difference between a golden point and ours (the same unit names in the same order are expected; a missing
// or extra line is a difference too). Empty if equal.
struct FpDiff {
    bool any = false;
    uint64_t cycle = 0;
    std::string unit, why;   // why: "hash golden X, ours Y" or a structural difference
};
FpDiff compare(const FpPoint& golden, const std::vector<FpLine>& ours);

// Parse "c unit hash" lines (as FpRegistry::point writes them) back into FpLines.
std::vector<FpLine> parse_lines(const std::vector<std::string>& lines);

}  // namespace fp
