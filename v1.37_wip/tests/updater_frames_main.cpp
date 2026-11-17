// updater_frames: gold's frames fed to the Updater black box directly, with no firmware (v1.37; tests/updater_bench.h).
//
//   updater_frames FRAMES --asics N --segment S [--out PREFIX] [--ref PREFIX] [--quiet]
//
// Plays the frame file for segment S of N and prints each image's SHA-256 and each run's pages and figures. --out writes
// the images as PREFIX.program.img, .table.img, .cycle0.img, .start-S.img, .tracer.img and .final.img, in gold's form.
// --ref compares them, byte for byte, with the files gold's `frames-images FRAMES S --out=PREFIX` wrote (gold writes no
// Tracer's copy: it must equal the start image). It fails (exit 1) on a difference, on a run that leaves PAGE_COUNT
// above 0, on an error bit, or on a read of a page nothing wrote.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sha256.h"
#include "updater_bench.h"

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path, out, ref;
    unsigned asics = 128, s = 0;
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--asics" && i + 1 < argc) asics = unsigned(std::atoi(argv[++i]));
        else if (a == "--segment" && i + 1 < argc) s = unsigned(std::atoi(argv[++i]));
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--ref" && i + 1 < argc) ref = argv[++i];
        else if (a == "--quiet") quiet = true;
        else if (path.empty() && a[0] != '-') path = a;
        else { std::fprintf(stderr, "usage: updater_frames FRAMES --asics N --segment S [--out PREFIX] [--ref PREFIX] [--quiet]\n"); return 2; }
    }
    if (path.empty()) { std::fprintf(stderr, "updater_frames: no frame file\n"); return 2; }
    frames::File file;
    std::string err;
    if (!file.read(path, err)) { std::printf("FAIL updater_frames: %s\n", err.c_str()); return 1; }
    updater_bench::Bench b;
    updater_bench::Proof p;
    int failures = 0;
    if (!updater_bench::play(b, file, asics, s, p, err)) { std::printf("FAIL updater_frames %s, s %u: %s\n", path.c_str(), s, err.c_str()); failures++; }

    const std::string start_name = "start-" + std::to_string(s);
    struct Image { std::string name; const std::vector<uint8_t>* bytes; bool in_gold; };
    const Image images[] = {{"program", &p.program_image, true}, {"table", &p.table_image, true}, {"cycle0", &p.cycle0_image, true},
                            {start_name, &p.start_image, true}, {"tracer", &p.tracer_image, false}, {"final", &p.final_image, true}};
    for (const Image& im : images) {
        if (!quiet) std::printf("%-10s %s  %zu pages\n", im.name.c_str(), test::sha256_hex(im.bytes->data(), im.bytes->size()).c_str(), im.bytes->size() / 4104);
        if (!out.empty() && !write_file(out + "." + im.name + ".img", *im.bytes)) { std::printf("FAIL updater_frames: cannot write %s.%s.img\n", out.c_str(), im.name.c_str()); failures++; }
        if (!ref.empty() && im.in_gold) {
            std::vector<uint8_t> want;
            if (!read_file(ref + "." + im.name + ".img", want)) { std::printf("FAIL updater_frames: cannot read gold's %s.%s.img\n", ref.c_str(), im.name.c_str()); failures++; }
            else if (want != *im.bytes) { std::printf("FAIL updater_frames %s, s %u: the %s image differs from gold's (%zu bytes, gold's %zu)\n", path.c_str(), s, im.name.c_str(), im.bytes->size(), want.size()); failures++; }
        }
    }
    if (p.tracer_image != p.start_image) { std::printf("FAIL updater_frames %s, s %u: the Tracer's copy differs from the start image\n", path.c_str(), s); failures++; }
    uint64_t taken = 0;
    for (size_t r = 0; r < b.accounts.size(); r++) {
        const auto& a = b.accounts[r];
        taken += a.pages_asked - a.pages_left;
        if (!quiet) std::printf("run %-18s %6u pages asked, %u left; %9u cycles: %u waiting on data, %u on pages, %u on memory; %u pages read, %u written\n",
                                p.run_names[r].c_str(), a.pages_asked, a.pages_left, a.cycles, a.stall_data, a.stall_pages, a.stall_memory, a.pages_read, a.pages_written);
        if (a.pages_left != 0) { std::printf("FAIL updater_frames %s, s %u: the run %s ended with PAGE_COUNT %u\n", path.c_str(), s, p.run_names[r].c_str(), a.pages_left); failures++; }
    }
    if (b.u.errors(b.t) != 0) { std::printf("FAIL updater_frames %s, s %u: ERRORS 0x%x\n", path.c_str(), s, unsigned(b.u.errors(b.t))); failures++; }
    if (b.u.unwritten_reads != 0) { std::printf("FAIL updater_frames %s, s %u: %llu reads of a page nothing wrote\n", path.c_str(), s, (unsigned long long)b.u.unwritten_reads); failures++; }
    if (b.u.free_page_entries() != 0 || b.u.input_words() != 0 || b.stream_left()) { std::printf("FAIL updater_frames %s, s %u: words or pages left (%u words in the Updater's input, %u free pages)\n", path.c_str(), s, b.u.input_words(), b.u.free_page_entries()); failures++; }
    std::printf("%s updater_frames %s, N %u, s %u: %zu runs, %llu pages taken from the queue, %zu given out with the roots, %lld cycles\n",
                failures ? "FAIL" : "PASS", path.c_str(), asics, s, b.accounts.size(), (unsigned long long)taken, b.pages_given.size(), (long long)b.t);
    return failures ? 1 : 0;
}
