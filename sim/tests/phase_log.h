// phase_log.h (WORKER20, P2, row H3): ref's PhaseLog (sim/ref/meshsim/src/PhaseLog.java), per-phase accounting for a
// whole run: each C&C GO store opens a phase named from the command word C&C last queued for slot 0 (`namer`); it ends
// when C&C sees all READY for the next GO, or at close(). Its counters, peaks and report() and csv() are ref's, field
// for field (the CSV byte for byte).
//
// Here it is attached to a machine on demand (Chip::phases(): ref's is always there; attach it before the run it
// accounts). With one attached, runs go cycle by cycle: ref takes its counters and restarts its peaks inside C&C's
// cycle of the GO, after the tiles; the harness takes them after that cycle. The write-engine input's and the GATHER
// queue's peaks exist in the debug and suite builds only (Sim::fifo_peak): make the CSV in the suite build.
// `unreadOverwrites` (invariant 8 as a statistic) is 0 until W26's tracking lands.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mach {

class Chip;

class PhaseLog {
public:
    static constexpr int NC = 18, NP = 10;
    static const char* const COUNTS[NC];
    static const char* const PEAKS[NP];
    struct Phase {
        int index = 0;
        int32_t word = 0;
        std::string name;
        int64_t go_at = 0, end_at = -1, cnc_reaction = -1;
        int64_t tile_go[64] = {}, tile_ready[64] = {};
        int64_t n[NC] = {};
        int64_t peak[NP] = {};
        int64_t lat_max[3] = {};
        int64_t tile_instr[64] = {}, tile_busy[64] = {};
        int64_t cycles() const { return end_at - go_at; }
    };
    std::function<std::string(int32_t)> namer;   // default: "-" for a negative word, else "cmd %02X" of its top byte
    std::vector<Phase> phases;
    std::vector<std::pair<std::string, int64_t>> run_peak;   // ref's LinkedHashMap, in PEAKS' order as first merged

    explicit PhaseLog(Chip& c);
    void close();                       // end the open phase at the run's end
    std::string report() const;
    std::string csv() const;
    bool csv(const std::string& path, std::string& err) const;

    // ---- hooks (the harness, between cycles) ----
    void on_go(int64_t t, int32_t word);
    void on_all_ready(int64_t t);
    void on_ready(unsigned tile, int64_t t);

private:
    Chip* c_;
    int open_ = -1;
    std::array<int64_t, NC> base_ = {};
    int64_t base_instr_[64] = {}, base_busy_[64] = {};
    int64_t last_all_ready_seen_ = -1;
    void finish(Phase& p, int64_t end);
    std::array<int64_t, NC> counters() const;
    std::array<int64_t, NP> peaks() const;
    void reset_peaks();
    void core(unsigned k, int64_t& instr, int64_t& wfi, int64_t& stalled) const;
};

}  // namespace mach
