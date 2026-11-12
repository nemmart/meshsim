// suite_replayer (WORKER25, P7b; inventory C20, P3's row, owner 2026-11-11): v1.3's ReplayerTest as run_tests.sh's step
// `replayer` runs it (`java ReplayerTest data/vec-821e1bd.tsv ../tables/opcode_table.txt`), its output v1.3's line for
// line: ReplayerTest.opcodeTable and .vectors (W15's port, test_replayer_alu's alu_report: the report lines), then
// ReplayerUnitTests.all (W15's port, test_replayer's groups, which print the program pass's line), then the two
// ConvertDelivery groups W15 left out (ReplayerUnitTests lines 809-865), here on v1.35's C++ converter (D42:
// tools/delivery/convert_delivery.cpp), and T.done's line: "ReplayerTest: N passed, M failed", every check counted.
// W15's two test files and W19's converter are compiled in here unchanged (each in a namespace of its own, their TEST
// bodies kept as plain functions, the converter's main renamed): one source of truth for each check.
//
//   suite_replayer        (run from sim/: it reads data/vec-821e1bd.tsv and data/tables/opcode_table.txt)
// gcc 13's debug build (-O1, sanitizers) warns falsely inside libstdc++'s std::regex (std::function members "may be
// used uninitialized"), which the converter's window-name regex instantiates in this translation unit.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "harness.h"
#include "replay_driver.h"
#include "replay_t.h"
#include "replay_window.h"
#include "sha256.h"
#include "../tools/delivery/json.h"
#include "units/event_counts.h"
#include "units/replayer_alu.h"
#include "units/row_core.h"
#include "units/stream_word.h"

#undef TEST
#define TEST(name) [[maybe_unused]] static void name()

namespace w15alu {
#include "test_replayer_alu.cpp"
}
namespace w15unit {
#include "test_replayer.cpp"
}
namespace w19conv {
#define main convert_delivery_main
#include "../tools/delivery/convert_delivery.cpp"
#undef main
}

namespace test {   // the CHECK macros in the unused TEST bodies refer to it; nothing here calls them
void fail(const char*, int, const char*, ...) {}
}

namespace {

using rtest::T;
namespace fs = std::filesystem;

std::string le_bytes(const std::vector<uint64_t>& w) {
    std::string s(8 * w.size(), '\0');
    for (size_t i = 0; i < w.size(); i++) for (int k = 0; k < 8; k++) s[8 * i + size_t(k)] = char(w[i] >> (8 * k));
    return s;
}
uint64_t le64(const std::string& b, size_t at) { uint64_t v = 0; for (int k = 7; k >= 0; k--) v = v << 8 | uint8_t(b[at + size_t(k)]); return v; }
std::string slurp(const fs::path& p) {
    std::string s;
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}
void spill(const fs::path& p, const std::string& s) { FILE* f = std::fopen(p.c_str(), "wb"); if (f) { std::fwrite(s.data(), 1, s.size(), f); std::fclose(f); } }
std::string size_string(const units::streamword::Size* s) {   // Arrays.toString of StreamSize records
    std::string r = "[";
    for (int k = 0; k < 3; k++)
        r += std::string(k ? ", " : "") + "StreamSize[words=" + std::to_string(s[k].words) + ", values=" + std::to_string(s[k].values) +
             ", compressed=" + (s[k].compressed ? "true" : "false") + "]";
    return r + "]";
}

// ReplayerUnitTests lines 809-832: a v1.25 record to v1.26.1 (ConvertDelivery.convertRecord).
void convert_record_group() {
    T::group("v1.26.1: ConvertDelivery, a v1.25 record to v1.26.1");
    std::vector<uint64_t> v(2 * 67, 0);
    v[0] = 0xB1; v[1] = 0xB2;
    v[2] = 0x80001000ull;
    v[3] = 0x1234ull | 7ull << 32 | 5ull << 48 | 1ull << 51;
    v[4] = 100ull * 32 | uint64_t(40 * 64) << 32; v[5] = 6ull * 64;
    for (int i = 0; i < 128; i++) v[size_t(6 + i)] = 0x1000ull + uint64_t(i);
    const units::streamword::Size sz[3] = {{30, 100, true}, {20, 40, false}, {3, 6, true}};
    std::string out;
    std::string err = w19conv::convert_record(le_bytes(v), sz, out);
    T::eq(16 * 68, int64_t(out.size()), "68 words");
    if (out.size() < 16 * 68) out.resize(16 * 68, '\0');
    T::eq(int64_t(0x80001000ll), int64_t(le64(out, 0)), "word 0: pc");
    T::eq(int64_t(0x1234ll | 7ll << 32), int64_t(le64(out, 8)), "instr_idx and offset kept; [127:112] 0 (no flags, v1.26.1)");
    units::streamword::Size got[3];
    units::streamword::parse(le64(out, 16), le64(out, 24), got);
    bool same = true;
    for (int k = 0; k < 3; k++) same = same && got[k].words == sz[k].words && got[k].values == sz[k].values && got[k].compressed == sz[k].compressed;
    T::check(same, "word 1: the sizes in v1.26.1's layout: " + size_string(got));
    T::eq(0, int64_t(units::streamword::advice(le64(out, 16), le64(out, 24))), "the advice field 0");
    T::eq(0x1001, int64_t(le64(out, 32 + 8)), "the image: register 1 at word 2's high half");
    T::eq(0, int64_t(le64(out, 16 * 66) | le64(out, 16 * 66 + 8)), "word 66, files: 0");
    T::eq(0xB1, int64_t(le64(out, 16 * 67)), "word 67: the bitmap");
    T::eq(0xB2, int64_t(le64(out, 16 * 67 + 8)), "(its high half)");
    v[3] = 0x1234;
    std::string out2;
    T::check(!w19conv::convert_record(le_bytes(v), sz, out2).empty(), "a v1.25 record with [115] = 0 is refused (the tracer emits 1)");
    (void)err;
}

// ReplayerUnitTests lines 834-863: the converted manifest the same from every run (ref: two JVMs and this one; here
// two processes and this one).
void convert_determinism_group(const std::string& self_dir) {
    T::group("ConvertDelivery: the same manifest from every run (a fixed key order)");
    char tmpl[] = "/tmp/convert-det.XXXXXX";
    const char* t = mkdtemp(tmpl);
    if (!t) { T::check(false, "ConvertDelivery determinism test: mkdtemp"); return; }
    fs::path tmp = t, src = tmp / "src";
    fs::create_directories(src / "p");
    std::vector<uint64_t> v(2 * 67, 0);
    v[2] = 0x80000000ull; v[3] = 1ull | 1ull << 51;
    v[4] = 128ull | 128ull << 32;
    spill(src / "p/window_00000.state.bin", le_bytes(v));
    spill(src / "p/window_00000.end.bin", le_bytes(v));
    spill(src / "p/window_00000.code.bin", std::string(16, '\0'));
    spill(src / "p/window_00000.rlog.bin", std::string(16, '\0'));
    spill(src / "p/window_00000.wlog.bin", std::string());
    spill(src / "p.manifest.json", "{\"programs\": [{\"name\": \"p\", \"window_entries\": [{\"index\": 0, "
                                   "\"files\": {\"state.bin\": {\"bytes\": 1072, \"sha256\": \"0\"}, \"end.bin\": {\"bytes\": 1072, \"sha256\": \"0\"}}}]}]}\n");
    std::string mfs[3];
    for (int k = 0; k < 2; k++) {   // two runs, each in a process of its own
        std::string cmd = self_dir + "/convert_delivery " + src.string() + " " + (tmp / ("out" + std::to_string(k))).string() + " p 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        std::string log;
        char b[4096];
        size_t n;
        while (p && (n = std::fread(b, 1, sizeof b, p)) > 0) log.append(b, n);
        int rc = p ? pclose(p) : -1;
        std::string first = log.substr(0, log.find('\n'));
        T::check(rc == 0, "run " + std::to_string(k + 1) + " converts: " + first);
        mfs[k] = slurp(tmp / ("out" + std::to_string(k)) / "p.manifest.json");
    }
    {   // and this one (ref: ConvertDelivery.convert in the test's own JVM); its progress lines aren't the log's
        std::string a1 = src.string(), a2 = (tmp / "out2").string(), a3 = "p", a4 = "--manifest", a5 = (src / "p.manifest.json").string();
        char* argv[] = {const_cast<char*>("convert_delivery"), a1.data(), a2.data(), a3.data(), a4.data(), a5.data(), nullptr};
        std::fflush(stdout);
        FILE* keep = std::fopen("/dev/null", "w");
        FILE* old = stdout;
        stdout = keep;
        w19conv::convert_delivery_main(6, argv);
        std::fflush(stdout);
        stdout = old;
        std::fclose(keep);
    }
    mfs[2] = slurp(tmp / "out2/p.manifest.json");
    T::check(!mfs[0].empty() && mfs[0] == mfs[1] && mfs[1] == mfs[2], "the manifest is the same in two JVM runs and in this one");
    size_t by = mfs[0].find("\"by\""), from = mfs[0].find("\"from_manifest_sha256\"");
    T::check(by != std::string::npos && by > 0 && from != std::string::npos && from > by, "\"converted\": \"by\" first, then \"from_manifest_sha256\"");
    std::string rec = slurp(tmp / "out0/p/window_00000.state.bin");
    T::check(mfs[0].find("\"sha256\": \"" + test::sha256_hex(rec.data(), rec.size()) + "\"") != std::string::npos,
             "the converted record's SHA-256 in the manifest");
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

std::string self_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    return p.substr(0, p.rfind('/'));
}

}  // namespace

int main() {
    // ReplayerTest.opcodeTable and .vectors (the report lines)
    std::vector<std::string> report;
    int alu_passed = 0;
    int alu_failed = w15alu::alu_report(report, &alu_passed);
    for (const std::string& l : report) std::printf("%s\n", l.c_str());
    std::fflush(stdout);
    // ReplayerUnitTests.all, in its order (W15's groups; `program` prints the pass's line)
    using namespace w15unit;
    void (*groups[])() = {decode_index, immediates, decompressors, program, program_compressed, store_log_off, mid_start, pext_any_mask,
                          ld_to_x0, controller_fed, controller_fed_rd0, padding, debug_length, immediate_zero, invalid, write_port, lines,
                          timing, v1261_a, rows_run_past_2_16, working_state, stream_word_layout, underflows, leftovers, padding_anything,
                          flushes, rd_prev_flag, record_words, queue_sizes};
    for (auto g : groups) { g(); std::fflush(stdout); }
    convert_record_group();
    convert_determinism_group(self_dir());
    int passed = alu_passed + T::passed, failed = alu_failed + T::failed;
    std::printf("ReplayerTest: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
