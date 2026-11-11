// gen_regs (WORKER19, P1): writes fw/include/mesh.h and fw/REGISTERS.md from the register table (regtable.h), the
// output of v1.3's Registers/MeshHeader and RegDoc byte for byte. tests/test_regs.cpp checks the committed files.
//   gen_regs mesh.h FILE | gen_regs registers FILE | gen_regs check MESH_H REGISTERS_MD (exit 1 on a difference)
#include <cstdio>
#include <cstring>
#include <string>

#include "regs_gen.h"

static bool read_file(const char* p, std::string& s) {
    FILE* f = fopen(p, "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    s.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return true;
}
static bool write_file(const char* p, const std::string& s) {
    FILE* f = fopen(p, "wb");
    if (!f) return false;
    const bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    return fclose(f) == 0 && ok;
}

int main(int argc, char** argv) {
    if (argc == 3 && (!strcmp(argv[1], "mesh.h") || !strcmp(argv[1], "registers"))) {
        const std::string s = !strcmp(argv[1], "mesh.h") ? regs::mesh_h() : regs::registers_md();
        if (!write_file(argv[2], s)) { fprintf(stderr, "gen_regs: cannot write %s\n", argv[2]); return 2; }
        printf("gen_regs: wrote %s\n", argv[2]);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "check")) {
        int bad = 0;
        const char* files[2] = {argv[2], argv[3]};
        const std::string want[2] = {regs::mesh_h(), regs::registers_md()};
        for (int i = 0; i < 2; i++) {
            std::string got;
            if (!read_file(files[i], got) || got != want[i]) { printf("gen_regs: %s differs from the generator\n", files[i]); bad = 1; }
        }
        if (!bad) printf("gen_regs: %s and %s equal to the generator's output\n", files[0], files[1]);
        return bad;
    }
    fprintf(stderr, "usage: gen_regs mesh.h FILE | gen_regs registers FILE | gen_regs check MESH_H REGISTERS_MD\n");
    return 2;
}
