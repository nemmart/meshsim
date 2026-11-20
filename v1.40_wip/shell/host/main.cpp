// main.cpp: the shell on the host, on the emulated file system (v1.40_wip/fs/).
//
//   shell_host [-s test.seed] [-i test.in] [-l calls.log]
//
// Seeds the file system (seed.cpp), then runs shell_main as the shell's caller (FS-API 6): a call the shell may not
// make stops the emulation. Keystrokes come from -i (a test's .in, escapes decoded), else from the terminal or stdin.
// At the end of input: every file the shell opened must be closed again (each streamed file is opened to append and
// closed, which a reader left open would make FS_EBUSY), and the emulation's invariants hold (fsemu::check).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../fs/fs.h"
#include "../../fs/fs_emu.h"
#include "console_host.h"
#include "seed.h"

using namespace fsemu;

// a .in file: \r \n \t \e \\ \xHH; a literal newline is ignored
static std::vector<unsigned char> decode(const char* file) {
    FILE* f = std::fopen(file, "rb");
    if (!f) { std::fprintf(stderr, "shell_host: cannot open %s\n", file); std::exit(3); }
    std::vector<unsigned char> raw, out;
    int c;
    while ((c = std::fgetc(f)) != EOF) raw.push_back((unsigned char)c);
    std::fclose(f);
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '\n') continue;
        if (raw[i] != '\\') { out.push_back(raw[i]); continue; }
        if (++i == raw.size()) break;
        switch (raw[i]) {
        case 'r': out.push_back('\r'); break;
        case 'n': out.push_back('\n'); break;
        case 't': out.push_back('\t'); break;
        case 'e': out.push_back(0x1B); break;
        case '\\': out.push_back('\\'); break;
        case 'x': {
            if (i + 2 >= raw.size()) { std::fprintf(stderr, "%s: bad \\x\n", file); std::exit(3); }
            const char h[3] = {char(raw[i + 1]), char(raw[i + 2]), 0};
            out.push_back((unsigned char)std::strtoul(h, nullptr, 16));
            i += 2;
            break;
        }
        default: std::fprintf(stderr, "%s: unknown escape \\%c\n", file, raw[i]); std::exit(3);
        }
    }
    return out;
}

// every streamed file closed again: open to append and close (a COW file is FS_EROFS, which is fine)
static int leaks = 0;
static void closed_check(const std::string& dir) {
    struct fs_dirent e;
    u32 pos = 0;
    std::vector<std::pair<std::string, u32>> v;
    while (fs_list(dir.c_str(), &pos, &e) == 1) v.push_back({(dir == "/" ? "" : dir) + "/" + e.name, e.type});
    for (auto& [p, t] : v) {
        if (t == FS_T_DIR) { closed_check(p); continue; }
        if (std::find(g_seed_open.begin(), g_seed_open.end(), p) != g_seed_open.end()) continue;
        const int fd = fs_open(p.c_str(), FS_APPEND);
        if (fd == FS_EBUSY) { std::fprintf(stderr, "shell_host: %s left open\n", p.c_str()); leaks++; }
        if (fd >= 0) fs_close(fd);
    }
}

int main(int argc, char** argv) {
    const char *seed = nullptr, *in = nullptr, *log = nullptr;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "-s")) seed = argv[i + 1];
        else if (!std::strcmp(argv[i], "-i")) in = argv[i + 1];
        else if (!std::strcmp(argv[i], "-l")) log = argv[i + 1];
        else { std::fprintf(stderr, "usage: shell_host [-s seed] [-i in] [-l log]\n"); return 3; }
    }
    FILE* lf = log ? std::fopen(log, "w") : nullptr;
    set_log(lf);
    seed_run(seed);
    static std::vector<unsigned char> keys;
    if (in) { keys = decode(in); con_host_script(keys.data(), keys.size()); }
    con_host_init();
    set_caller(Caller::SHELL);
    note("--- the shell");
    shell_main();
    std::fflush(stdout);
    note("--- the end: every file closed, the invariants");
    set_caller(Caller::TILE, 61);
    closed_check("/");
    if (!g_seed_known_bug.empty()) std::fprintf(stderr, "known bug, check() skipped: %s\n", g_seed_known_bug.c_str());
    else if (!g_seed_writer) check();               // check() wants no file open for writing
    if (lf) std::fclose(lf);
    return leaks ? 4 : 0;
}
