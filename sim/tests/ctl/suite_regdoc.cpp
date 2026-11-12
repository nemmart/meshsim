// suite_regdoc.cpp (WORKER23, P5, inventory C14): v1.3's RegDocTest (sim/ref/meshsim/test/RegDocTest.java), check for
// check. run_tests.sh's step `regdoc`; its output equals ref's log (tests/ctlsuites/regdoc.log). Run from sim/.
//
// REGISTERS.md and mesh.h against their generators, the draft header and the simulator (v1.2):
//  1. fw/REGISTERS.md is the generator's output (W19's tools/regs, in place of RegDoc.java);
//  2. fw/include/mesh.h is the generator's output (MeshHeader.java's), and the programmer's copy is the same file (v1.35
//     keeps one copy: the check is on it);
//  3. tools/regs/header_diff.sh (v1.3's header_diff.py, D40): every macro and struct offset the generated header shares
//     with the draft the recipes compile against (fw/mesh-v1.2-draft.h) agrees (needs gcc; skipped without it);
//  4. the simulator behaves as the document's register table says.
// STAND-IN (NOTES.md): ref's `core.store(t, reserved word)` returns at once; TileRef::store queues (16 at most), so the
// suite takes each reserved store back off the queue after its reason is known (only the reason is checked) until
// W20's store that returns the reason without queuing lands.
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "base/fatal.h"
#include "../../tools/regs/regs_gen.h"
#include "../mach.h"
#include "units/tile_ctl.h"

namespace {
int pass = 0, fail = 0;
void check(bool ok, const std::string& what) { if (ok) pass++; else { fail++; std::printf("FAIL %s\n", what.c_str()); } }
bool read_file(const std::string& p, std::string& s) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::stringstream b; b << f.rdbuf(); s = b.str(); return true;
}
bool on_path(const char* exe) {
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "";
    size_t at = 0;
    for (;;) {
        const size_t e = p.find(':', at);
        const std::string d = p.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (access((d + "/" + exe).c_str(), X_OK) == 0) return true;
        if (e == std::string::npos) return false;
        at = e + 1;
    }
}
std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%x", v); return b; }
const regs::Reg& reg(const char* n) { for (const regs::Reg& g : regs::ALL) if (std::string(g.name) == n) return g; FATAL("no register %s", n); }
uint32_t reg_addr(const regs::Reg& g) { return regs::REG_BASE + 4u * uint32_t(g.index); }
constexpr int COND_RECV_UNDERFLOW = 14, ST_DONE = 4;

// STAND-IN (above): the store's reason, the queued store taken back
std::string store_reason(mach::Chip& c, unsigned k, int64_t t, uint32_t a, uint32_t v) {
    const std::string e = c.tile(k).store(t, a, v);
    if (e.empty()) { units::TileCore& core = c.unit<units::TileCore>(units::unit_prefix(k) + "core"); if (core.host_n) core.host_n--; }
    return e;
}
}  // namespace

int main() {
    std::printf("RegDocTest: mesh.h 1.2\n");
    // ---- 1, 2. the generated files
    std::string doc, meshh;
    read_file("fw/REGISTERS.md", doc);
    check(doc == regs::registers_md(), "REGISTERS.md is RegDoc's output (run java -cp build RegDoc)");
    read_file("fw/include/mesh.h", meshh);
    check(meshh == regs::mesh_h(), "sw/mesh.h is MeshHeader's output");
    { std::string copy; if (read_file("fw/include/mesh.h", copy)) check(copy == meshh, "../sw/mesh.h (the programmer's copy) is the generated sw/mesh.h"); }
    check(meshh.find("#define MESH_H_VERSION 0x0102") != std::string::npos, "mesh.h: MESH_H_VERSION 1.2");
    for (const char* gone : {"TILE_PULSE", "TILE_STATUS", "TILE_CONDITIONS", "WAKE_EVENTS", "ROUTER_SET_HDR", "DMA_SRAM_REMAINING", "MODE_CM", "MODE_REPLY",
                             "CM_WORD", "SENDER_SEND", "PERIPH_STATUS_0"})
        check(meshh.find(gone) == std::string::npos, std::string("mesh.h 1.2 has no v1.1 name ") + gone);

    // ---- 3. the draft header
    if (access("fw/mesh-v1.2-draft.h", R_OK) == 0 && on_path("gcc")) {
        FILE* p = popen("bash tools/regs/header_diff.sh fw/include/mesh.h fw/mesh-v1.2-draft.h 2>&1", "r");
        std::string out; char b[4096]; size_t n;
        while (p && (n = std::fread(b, 1, sizeof b, p)) > 0) out.append(b, n);
        if (p) pclose(p);
        check(out.find("differing: 0") != std::string::npos && out.find("differing offsets: 0") != std::string::npos,
              "every macro and struct offset shared with the draft agrees:\n" + out);
        check(out.find("ONLY IN DRAFT") == std::string::npos && out.find("MEMBER ONLY IN DRAFT") == std::string::npos,
              "the generated header defines everything the draft does");
    } else std::printf("header_diff skipped (needs python3, gcc and ../sw/mesh-v1.2-draft.h)\n");

    // ---- 4. the simulator
    mach::Chip c(mach::MachConfig::machine());
    c.run(20);
    const unsigned k = 5;
    const int64_t t = c.cycle() + 1;
    for (const regs::rd::Row& r : regs::rd::ROWS) {
        const regs::Reg& g = reg(r.reg);
        if (!regs::readable(g) && regs::load_at(g.index) != nullptr) continue;   // the request port's stores: a load reads the receive side
        if (std::string(g.name) == "REQUEST_RECEIVE") continue;                  // a load pops (underflow on a mesh tile): below
        const std::optional<uint32_t> v = c.tile(k).load(t, reg_addr(g), 4);
        check(bool(v), std::string(g.name) + ": a load is not a bus error");
        if (!v) continue;
        const int32_t x = int32_t(*v);
        if (std::string(r.load) == "0") check(x == 0, std::string(g.name) + ": a load reads 0 (" + std::to_string(x) + ")");
        // PHASE-2 INTERIM (until phase 4's reset): a halted unit shows done in STATUS
        if (std::string(r.reset).compare(0, 1, "0") == 0)
            check(x == 0 || (std::string(g.name) == "STATUS" && x == 1 << ST_DONE), std::string(g.name) + ": 0 after the reset (" + hex(*v) + ")");
        if (std::string(g.name) == "EVENTS_MASK") check(x == 0x180, "EVENTS_MASK: [8] | [7] after the reset (" + hex(*v) + ")");
        if (std::string(g.name) == "PARAMS")
            check(*v == c.tile(k).block().params && (*v >> 20 & 0x7F) == 5, "PARAMS: the constant, endpoint id 5");
        if (std::string(g.name) == "SRAM_RD_ADDR_QUEUE") check(x >= 64, "SRAM_RD_ADDR_QUEUE: at least 64 free entries when empty (" + std::to_string(x) + ")");
        if (!regs::writable(g) && regs::store_at(g.index) == nullptr)
            check(!c.tile(k).store(t, reg_addr(g), 0).empty(), std::string(g.name) + ": a store to a read-only register is a bus error");
    }
    for (int i = 0; i < int(regs::BLOCK_WORDS); i++)
        if (regs::reserved_word(i)) {
            const uint32_t a = regs::REG_BASE + 4u * uint32_t(i);
            check(c.tile(k).load(t, a, 4) == std::optional<uint32_t>(0), "reserved word 0x" + hex(0x800 + 4 * uint32_t(i)) + " reads 0");
            check(store_reason(c, k, t, a, 1).empty(), "and a store there is ignored, not a bus error");
        }
    check(c.tile(k).load(t, reg_addr(reg("REQUEST_RECEIVE")), 4) == std::optional<uint32_t>(0), "REQUEST_RECEIVE on a mesh tile reads 0 (an empty queue)");
    c.step(); c.step();
    check((c.tile(k).block().conditions & 1u << COND_RECV_UNDERFLOW) != 0, "and sets receive underflow (ERRORS [14])");
    std::printf("RegDocTest: %d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
