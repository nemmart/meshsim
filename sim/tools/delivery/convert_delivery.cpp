// convert_delivery (WORKER19, P1, D42): v1.3's ConvertDelivery (sim/ref/meshsim/test/ConvertDelivery.java) in C++, its
// output byte for byte: a v1.25 replay delivery (67-word state records) to v1.26.1 (DECISIONS-v1.26 items 1, 2, 15,
// 16), the streams untouched. The source is only read; DST/PROGRAM/ gets the converted records (every
// window_NNNNN.state*.bin and end.bin) and symbolic links to every other source file, and DST/PROGRAM.manifest.json is
// the source manifest with the converted files' sizes and SHA-256 and a "converted" entry ("by", then
// "from_manifest_sha256": v1.3's fixed order; v1.26.1's varied run to run).
//
// Per record: v1.25's word 0 (the template bitmap) moves to word 67; words 1-66 (state word, stream word, image) become
// words 0-65; word 66 (files) is 0. The state word's [114:112] (the compression flags) move into the stream word and are
// cleared; [115] (the store log) must be 1 and is cleared with [116]. The stream word's bit lengths become each stream's
// 128-bit words (ceil(bits / 128), checked against the stream file the flags name) and its values out from the window's
// raw record (state.bin): code bits / 32, log bits / 64; the advice field 0. end.bin gets a zero stream word.
//
//   convert_delivery SRC DST PROGRAM [--manifest M]   (M: default SRC/PROGRAM.manifest.json, else SRC/manifest.json)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "../../src/units/stream_word.h"
#include "../../tests/sha256.h"
#include "json.h"

namespace fs = std::filesystem;
namespace {

constexpr int V125_WORDS = 67, REC_WORDS = 68, REC_FILES = 66, REC_BITMAP = 67;

struct Stats { int windows = 0, records = 0, links = 0; long long raw_words = 0, pick_words = 0; long long vals[3] = {0, 0, 0};
               std::vector<std::string> errors; fs::path manifest; };
// A window's error (Java's RuntimeException, caught per window) is returned as a message: no exceptions here (D4).

bool read_file(const fs::path& p, std::string& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear(); char buf[1 << 16]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}
void write_file(const fs::path& p, const std::string& s) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f || fwrite(s.data(), 1, s.size(), f) != s.size() || fclose(f) != 0) { fprintf(stderr, "convert_delivery: cannot write %s\n", p.c_str()); exit(2); }
}
uint64_t le64(const std::string& b, size_t at) { uint64_t v = 0; for (int k = 7; k >= 0; k--) v = v << 8 | uint8_t(b[at + k]); return v; }
std::string sha(const std::string& b) { return test::sha256_hex(b.data(), b.size()); }
std::string hex(uint64_t v) { char s[32]; snprintf(s, sizeof s, "%llx", (unsigned long long)v); return s; }

std::string read_record(const fs::path& p, std::string& b) {
    if (!read_file(p, b)) { fprintf(stderr, "convert_delivery: java.nio.file.NoSuchFileException: %s\n", p.c_str()); exit(1); }
    if (b.size() != 16 * V125_WORDS) return p.filename().string() + ": " + std::to_string(b.size()) + " bytes, not a v1.25 record (1,072)";
    return "";
}
// v1.25's stream word (record word 2): code [23:0], read log [55:32], write log [87:64], in bits
void lengths(const std::string& r, long long l[3]) { uint64_t lo = le64(r, 32), hi = le64(r, 40); l[0] = lo & 0xFFFFFF; l[1] = (lo >> 32) & 0xFFFFFF; l[2] = hi & 0xFFFFFF; }
int flags(const std::string& r) { return int((le64(r, 24) >> 48) & 7); }

std::string size(long long words, long long values, bool c, units::streamword::Size& s) {
    if (words < 0 || words > 0xFFFF || values < 0 || values > 0x1FFFF)
        return "stream size " + std::to_string(words) + " words, " + std::to_string(values) + " values";
    s.words = uint32_t(words); s.values = uint32_t(values); s.compressed = c; return "";
}

std::string convert_record(const std::string& r, const units::streamword::Size sz[3], std::string& out) {
    const uint64_t s_hi = le64(r, 24);
    if ((s_hi >> 52) != 0) return "state word [127:116] not 0: 0x" + hex(s_hi >> 52);
    if (((s_hi >> 51) & 1) == 0) return "state word [115] (includes store log) is 0: the tracer emits 1";
    const uint64_t hi = uint64_t(uint32_t(s_hi)) | uint64_t((s_hi >> 32) & 0xFFFF) << 32;   // stateHi(idx, offset)
    uint64_t rec[2 * REC_WORDS] = {};
    uint64_t sw[2]; units::streamword::build(sz[0], sz[1], sz[2], 0, sw);
    rec[0] = le64(r, 16); rec[1] = hi; rec[2] = sw[0]; rec[3] = sw[1];
    for (int i = 0; i < 128; i++) rec[4 + i] = le64(r, 48 + 8 * i);   // the image, register 0 as delivered
    rec[2 * REC_FILES] = rec[2 * REC_FILES + 1] = 0;
    rec[2 * REC_BITMAP] = le64(r, 0); rec[2 * REC_BITMAP + 1] = le64(r, 8);
    out.assign(8 * 2 * REC_WORDS, '\0');
    for (int i = 0; i < 2 * REC_WORDS; i++) for (int k = 0; k < 8; k++) out[8 * i + k] = char(rec[i] >> (8 * k));
    return "";
}

bool is_record(const std::string& n) { static const std::regex re("window_\\d+\\.(state[-a-z]*|end)\\.bin"); return std::regex_match(n, re); }

std::string convert_window(const fs::path& sp, const fs::path& dp, int index, const std::vector<std::string>& names,
                    json::Value& files, Stats& st) {
    char b[32]; snprintf(b, sizeof b, "window_%05d.", index); const std::string base = b;
    std::string raw, e1;
    if (!(e1 = read_record(sp / (base + "state.bin"), raw)).empty()) return e1;
    long long rl[3]; lengths(raw, rl);
    if (flags(raw) != 0) return "state.bin has compression flags set: not the raw record";
    if (rl[0] % 32 != 0 || rl[1] % 64 != 0 || rl[2] % 64 != 0)
        return "raw bit lengths not whole values: [" + std::to_string(rl[0]) + ", " + std::to_string(rl[1]) + ", " + std::to_string(rl[2]) + "]";
    const long long vals[3] = {int(rl[0] / 32), int(rl[1] / 64), int(rl[2] / 64)};
    for (int k = 0; k < 3; k++) st.vals[k] += vals[k];
    static const char* const sn[3] = {"code", "rlog", "wlog"};
    bool has_state = false; for (auto& n : names) if (n == base + "state.bin") has_state = true;
    if (!has_state) return "no " + base + "state.bin";
    for (const std::string& n : names) {
        std::string r;
        if (!(e1 = read_record(sp / n, r)).empty()) return e1;
        const bool end = n.size() >= 8 && n.compare(n.size() - 8, 8, ".end.bin") == 0;
        long long len[3]; lengths(r, len);
        const int fl = flags(r);
        units::streamword::Size sz[3];
        for (int k = 0; k < 3; k++) {
            if (end) { size(0, 0, false, sz[k]); continue; }
            const bool c = ((fl >> k) & 1) != 0;
            const long long words = int((len[k] + 127) / 128);
            const fs::path sf = sp / (base + sn[k] + (c ? "-lru" : "") + ".bin");
            std::error_code ec; const uintmax_t fb = fs::file_size(sf, ec);
            if (ec) { fprintf(stderr, "convert_delivery: java.nio.file.NoSuchFileException: %s\n", sf.c_str()); exit(1); }
            if ((long long)fb != 16LL * words)
                return n + ": " + sn[k] + (c ? "-lru" : "") + " is " + std::to_string(fb) + " bytes, its " + std::to_string(len[k]) + " bits need " + std::to_string(words) + " words";
            if (!c && len[k] != rl[k]) return n + ": raw " + sn[k] + " length " + std::to_string(len[k]) + ", state.bin's " + std::to_string(rl[k]);
            if (!(e1 = size(words, vals[k], c, sz[k])).empty()) return e1;
            if (n.size() >= 10 && n.compare(n.size() - 10, 10, ".state.bin") == 0) st.raw_words += words;
            if (n.size() >= 15 && n.compare(n.size() - 15, 15, ".state-pick.bin") == 0) st.pick_words += words;
        }
        std::string out;
        if (!(e1 = convert_record(r, sz, out)).empty()) return e1;
        std::error_code ec0; fs::remove(dp / n, ec0);
        write_file(dp / n, out);
        st.records++;
        const std::string key = files.has(n) ? n : n.substr(base.size());
        json::Ptr e = files.get(key);
        if (!files.has(key) || !e) return n + ": not in the manifest";
        const std::string h = sha(out);
        if (e->kind == json::Value::OBJECT) {
            auto ne = json::Value::object(); ne->obj = e->obj;
            ne->put("sha256", json::Value::of_string(h));
            if (ne->has("bytes")) ne->put("bytes", json::Value::of_long((int64_t)out.size()));
            files.put(key, ne);
        } else files.put(key, json::Value::of_string(h));
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: convert_delivery SRC DST PROGRAM [--manifest M]\n"); return 2; }
    std::error_code ec0;
    const fs::path src = fs::absolute(argv[1], ec0), dst = fs::absolute(argv[2], ec0);
    const std::string prog = argv[3];
    fs::path mf;
    for (int i = 4; i < argc; i++) if (!strcmp(argv[i], "--manifest") && i + 1 < argc) mf = argv[++i];
    if (mf.empty()) mf = fs::exists(src / (prog + ".manifest.json")) ? src / (prog + ".manifest.json") : src / "manifest.json";
    const auto t0 = std::chrono::steady_clock::now();
    Stats st;
    std::string text, err;
    if (!read_file(mf, text)) { fprintf(stderr, "convert_delivery: cannot read %s\n", mf.c_str()); return 1; }
    json::Ptr m = json::parse(text, err);
    if (!err.empty() || !m || m->kind != json::Value::OBJECT) { fprintf(stderr, "convert_delivery: %s: %s\n", mf.c_str(), err.empty() ? "not a JSON object" : err.c_str()); return 1; }
    const fs::path sp = src / prog, dp = dst / prog;
    std::error_code ec; fs::create_directories(dp, ec);
    std::map<int, std::vector<std::string>> recs;
    for (const auto& de : fs::directory_iterator(sp, ec)) {
        const std::string n = de.path().filename().string();
        if (is_record(n)) { recs[atoi(n.substr(7, n.find('.') - 7).c_str())].push_back(n); continue; }
        const fs::path to = dp / n;
        fs::remove(to, ec);
        fs::create_symlink(fs::absolute(de.path(), ec), to, ec);
        if (ec) { fprintf(stderr, "convert_delivery: cannot link %s\n", to.c_str()); return 1; }
        st.links++;
    }
    json::Ptr programs = m->get("programs");
    if (programs && programs->kind == json::Value::LIST)
        for (const json::Ptr& p : programs->list) {
            json::Ptr name = p ? p->get("name") : nullptr;
            if (!name || name->kind != json::Value::STRING || name->s != prog) continue;
            json::Ptr wes = p->get("window_entries");
            if (!wes) continue;
            for (const json::Ptr& we : wes->list) {
                if (!we->has("files")) continue;
                st.windows++;
                const json::Ptr ix = we->get("index");
                const int index = int(ix->kind == json::Value::DOUBLE ? int64_t(ix->d) : ix->l);
                const std::string e = convert_window(sp, dp, index, recs.count(index) ? recs[index] : std::vector<std::string>{}, *we->get("files"), st);
                if (!e.empty()) st.errors.push_back("window " + std::to_string(index) + ": " + e);
            }
        }
    std::string orig;
    read_file(mf, orig);
    auto conv = json::Value::object();
    conv->put("by", json::Value::of_string("ConvertDelivery (meshsim WORKER20), v1.25 -> v1.26 records (DECISIONS-v1.26)"));
    conv->put("from_manifest_sha256", json::Value::of_string(sha(orig)));
    m->put("converted", conv);
    st.manifest = dst / (prog + ".manifest.json");
    const std::string mout = json::write(m, "") + "\n";
    write_file(st.manifest, mout);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("ConvertDelivery %s: %d windows, %d records converted, %d links; %s\n", prog.c_str(), st.windows, st.records, st.links,
           st.errors.empty() ? "OK" : (std::to_string(st.errors.size()) + " ERRORS").c_str());
    for (size_t i = 0; i < st.errors.size() && i < 20; i++) printf("  %s\n", st.errors[i].c_str());
    printf("values out: code %lld, read log %lld, write log %lld; words raw %lld, as picked %lld (%.1f s)\n",
           st.vals[0], st.vals[1], st.vals[2], st.raw_words, st.pick_words, secs);
    printf("manifest %s sha256 %s\n", st.manifest.c_str(), sha(mout).c_str());
    return st.errors.empty() ? 0 : 1;
}
