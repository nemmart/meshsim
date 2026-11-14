// regs_gen.h: the generators of fw/include/mesh.h and fw/REGISTERS.md, from the register table (regtable.h).
// tests/test_regs.cpp checks the committed files are their output; tools/regs/gen_regs writes them.
// Configuration-time code (D6): std::string is fine here.
//
// What mesh.h makes of the table (the names' rules, which the test relies on):
//   a register          a member of its window's struct, and OFF_<NAME>, its byte offset in its window (a peripheral's
//                       control and DMA blocks P_OFF_<NAME>, its own words <PREFIX>_OFF_<NAME>)
//   a field (Field)     <WORD>_<FIELD>: SET and BOTH (((uint32_t)(v) & mask) << lo); GET (((w) >> lo) & mask); BOTH also
//                       <WORD>_<FIELD>_OF(w), the GET form; FLAG the mask, in hex
//   a bit (Bit)         <GROUP>_<NAME> (1 << bit); n bits <GROUP>_<NAME>(n) (1 << (bit - 1 + (n)))
//   a value (Value)     its macro, a number
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

#include "regtable.h"

namespace regs {

inline std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
inline std::string fmt(const char* f, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}
inline std::string h8(uint32_t v) { return fmt("0x%08X", v); }
inline std::string upper(const char* s) {
    std::string u(s);
    for (char& c : u) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return u;
}
inline std::string acc_name(int a) { return a == R ? "R" : a == W ? "W" : "R/W"; }
inline std::string bits_of(int lo, int width) { return width == 1 ? fmt("[%d]", lo) : fmt("[%d:%d]", lo + width - 1, lo); }
inline std::string field_macro(const Field& f) { return std::string(f.word) + "_" + upper(f.name); }
inline std::string bit_macro(const Bit& b) { return std::string(b.group) + "_" + upper(b.name); }
inline bool is_bytes(const Value& v) { const std::string m(v.macro); return m.size() > 6 && m.compare(m.size() - 6, 6, "_BYTES") == 0; }
inline std::string value_text(const Value& v) { return is_bytes(v) && v.value >= 0x1000 ? fmt("0x%X", v.value) : fmt("%u", v.value); }

// ================================================================ mesh.h
namespace mh {
inline std::string acc(int a) { return a == R ? "MESH_RO" : a == W ? "MESH_WO" : "MESH_RW"; }
inline void def(std::string& b, const std::string& name, const std::string& v, const char* doc) {
    b += fmt("#define %-26s %s%s\n", name.c_str(), v.c_str(), doc ? (std::string("   /* ") + doc + " */").c_str() : "");
}
inline void values(std::string& b, const char* group) {
    for (const Value& v : VALUES) if (same(v.group, group)) def(b, v.macro, value_text(v), v.doc);
}
// the macros of one word's fields
inline void fields(std::string& b, const char* word) {
    for (const Field& f : FIELDS) {
        if (!same(f.word, word) || f.macro == DOC) continue;
        const std::string name = field_macro(f);
        const std::string set = fmt("(((uint32_t)(v) & 0x%Xu) << %d)", f.mask(), f.lo);
        const std::string get = fmt("(((w) >> %d) & 0x%Xu)", f.lo, f.mask());
        const std::string doc = bits_of(f.lo, f.width) + " " + f.doc;
        if (f.macro == FLAG) def(b, name, h8(f.mask() << f.lo), doc.c_str());
        else if (f.macro == GET) def(b, name + "(w)", get, doc.c_str());
        else def(b, name + "(v)", set, doc.c_str());
        if (f.macro == BOTH) def(b, name + "_OF(w)", get, nullptr);
    }
}
// the macros of one group's bits
inline void bits(std::string& b, const char* group) {
    for (const Bit& x : BITS) {
        if (!same(x.group, group)) continue;
        const std::string doc = std::string(x.kind) + ": " + x.doc;
        if (x.n == 1) def(b, bit_macro(x), fmt("(1 << %d)", x.bit), doc.c_str());
        else def(b, bit_macro(x) + "(n)", fmt("(1 << (%d + (n)))", x.bit - 1), fmt("n = 1-%d, bits %d-%d; %s", x.n, x.bit, x.bit + x.n - 1, doc.c_str()).c_str());
    }
}
// the register at window word i of a window: a peripheral's control block and, with a DMA, its DMA block, then its own
inline const Reg* window_reg(const Reg* own, int n_own, bool is_peripheral, bool dma, int i) {
    if (!is_peripheral || i >= PERIPHERAL_OWN) return reg_at(own, n_own, i);
    return i < int(BLOCK_WORDS) || dma ? reg_at(PERIPHERAL, N_PERIPHERAL, i) : nullptr;
}
inline std::string pad(const std::string& s, size_t width) { return s.size() < width ? s + std::string(width - s.size(), ' ') : s + " "; }
// one window's struct: its registers as members, in window-word order, and the words between them, unmapped.
// `generic`: any peripheral's, its own words one member.
inline void window_struct(std::string& b, const char* type, const Reg* own, int n_own, bool is_peripheral, bool dma, bool generic = false) {
    b += "typedef struct {\n";
    for (int i = 0; i < int(WINDOW_WORDS);) {
        if (const Reg* r = window_reg(own, n_own, is_peripheral, dma, i)) {
            b += fmt("    %s %s/* 0x%03X: %s */\n", acc(r->access).c_str(), pad(std::string(r->name) + ";", 21).c_str(), 4 * i, r->doc);
            i++;
        } else if (is_peripheral && !dma && i == int(BLOCK_WORDS)) {
            b += fmt("    uint32_t %s/* 0x040-0x07C: no DMA: every word of block 1 reads 0, and a store is UB (17.1) */\n",
                     pad(fmt("_no_dma[%u];", BLOCK_WORDS), 20).c_str());
            i += int(BLOCK_WORDS);
        } else if (generic && i == PERIPHERAL_OWN) {
            b += fmt("    uint32_t %s/* 0x080-0x1FC: blocks 2-7, the peripheral's own */\n", pad(fmt("_own[%u];", WINDOW_WORDS - PERIPHERAL_OWN), 20).c_str());
            i = int(WINDOW_WORDS);
        } else {
            int j = i;
            while (j < int(WINDOW_WORDS) && !window_reg(own, n_own, is_peripheral, dma, j) && !(generic && j == PERIPHERAL_OWN) &&
                   !(is_peripheral && !dma && j == int(BLOCK_WORDS))) j++;
            b += fmt("    uint32_t %s/* 0x%03X-0x%03X: unmapped, an access fault (5.3) */\n", pad(fmt("_unmapped_%03X[%d];", 4 * i, j - i), 20).c_str(),
                     4 * i, 4 * (j - 1));
            i = j;
        }
    }
    b += fmt("} %s;\n", type);
}
// _Static_assert: the struct's size and the offsets of each block's first and last registers
inline void window_asserts(std::string& b, const char* type, const Reg* own, int n_own, bool is_peripheral, bool dma) {
    b += fmt("_Static_assert(sizeof(%s) == 0x%X", type, WINDOW_BYTES);
    for (int blk = 0; blk < int(BLOCKS); blk++) {
        const Reg *first = nullptr, *last = nullptr;
        for (int i = blk * int(BLOCK_WORDS); i < (blk + 1) * int(BLOCK_WORDS); i++)
            if (const Reg* r = window_reg(own, n_own, is_peripheral, dma, i)) { if (!first) first = r; last = r; }
        if (first) b += fmt(" &&\n               __builtin_offsetof(%s, %s) == 0x%03X", type, first->name, 4 * first->index);
        if (last != first) b += fmt(" && __builtin_offsetof(%s, %s) == 0x%03X", type, last->name, 4 * last->index);
    }
    b += fmt(", \"%s\");\n", type);
}
inline void offsets(std::string& b, const char* prefix, const Reg* list, int n) {
    for (int k = 0; k < n; k++) def(b, std::string(prefix) + "OFF_" + list[k].name, fmt("0x%03X", 4 * list[k].index), nullptr);
}
}  // namespace mh

inline std::string mesh_h() {
    using namespace mh;
    std::string b;
    b += R"TB(/* mesh.h version 1.36 -- a tile program's view of the machine (spec/machine-spec.md: 2.1, 4, 5, 7-13, 17, 19;
 * spec/jolt/replayer.md 17.8; spec/jolt/tracer.md 17.9). GENERATED by tools/regs/gen_regs.cpp from
 * tools/regs/regtable.h; do not edit. Every name is the specification's: a register's is its own; a field's is its
 * register's, or its layout's, and its own; a bit's is STATUS_, EVENT_ or ERROR_ and its own, behind P_ for the bits
 * every peripheral has and IE_, RW_, RP_ or TR_ for Ingress/Egress's, the RW engine's, the replayer's and the Tracer's.
 * One struct per register window, reached through a pointer (tile->EVENTS); every member volatile uint32_t, accessed
 * one word at a time and never copied whole (some loads consume). The parameters are chapter 19's defaults, the 8 x 8
 * mesh's. fw/REGISTERS.md is the reference.
 *
 * Build: riscv-none-elf-gcc -march=rv32ic_zicond_zicsr_zmmul -mabi=ilp32 -std=gnu11. Assembly sees only the #defines,
 * and of those only the numbers, the offsets and the bits (the fields' macros cast). */
#ifndef MESH_H
#define MESH_H
#define MESH_H_VERSION 0x0124   /* 1.36: major << 8 | minor */

/* ---- the parameters software needs (19), at their defaults ---- */
)TB";
    values(b, "PARAM");

    b += "\n/* ---- the address map (5.1): an 8-bit region [31:24] and a 24-bit byte offset [23:0]; a C pointer is the packet's address ---- */\n";
    def(b, "DMEM_BASE", h8(DMEM_BASE), "region 0x00: DMEM; the core loads and stores, packets write");
    def(b, "IMEM_BASE", h8(IMEM_BASE), "region 0x01: IMEM; the core fetches only, packets write");
    def(b, "BOOT_ROM_BASE", h8(BOOT_ROM_BASE), "region 0x02: the boot ROM; the core fetches only");
    def(b, "RESET_PC", "BOOT_ROM_BASE", "the reset PC (6.5); a loaded image ends by jumping to it");
    def(b, "SCRATCHPAD_BASE", h8(SCRATCHPAD_BASE), "region 0x03: the scratchpad; packets and the read unit only");
    def(b, "PERIPHERAL_BASE(n)", "((0x03 + (n)) << 24)", "regions 0x04-0x06: peripheral n = 1-3's region; packets only");
    def(b, "DMEM_RESERVED", fmt("%u", DMEM_RESERVED), "DMEM's first 16 bytes, which no program uses (6.3)");
    def(b, "SCRATCHPAD_ADDRESS(w)", "(SCRATCHPAD_BASE | ((uint32_t)(w) << 2))", "a word address's byte address (5.4)");
    def(b, "WORD_ADDRESS(offset)", "((uint32_t)(offset) >> 2)", "a scratchpad byte offset's word address (5.4)");

    b += "\n/* ---- tile ids (2.1, 4.1), at 8 x 8 ---- */\n";
    values(b, "TILE");
    b += "\n/* ---- the packet's header block (4.1): sizes and modes (4.2) ---- */\n";
    values(b, "SIZE");
    values(b, "MODE");

    b += R"TB(
/* ---- the register windows (5.2, 5.3): region 0xFF's top 8 KiB; a window is 8 blocks of 16 words ---- */
#define MESH_RO const volatile uint32_t   /* R: load only; a store is an access fault (and doesn't compile) */
#define MESH_WO volatile uint32_t         /* W: store only; a load reads 0 */
#define MESH_RW volatile uint32_t         /* R/W */

#ifndef __ASSEMBLER__
#include <stdint.h>

/* the tile window, 0xFFFFF800 (5.5): block 0 tile_control_unit (7), 1 dma_unit (8), 2 write_unit (9), 3 read_unit (10),
 * 4 request_unit (11), 5 receive_unit (12), 6 gather_unit (13); block 7 unused */
)TB";
    window_struct(b, "TileRegs", TILE, N_TILE, false, true);
    b += "\n/* any peripheral's window, 0xFFFFF800 + 0x200 n (17.1): the control block, the DMA block (which reads 0 on a peripheral\n"
         " * without a DMA), and blocks 2-7, the peripheral's own */\n";
    window_struct(b, "PeripheralRegs", nullptr, 0, true, true, true);
    for (const Peripheral& p : PERIPHERALS) {
        b += fmt("\n/* %s, peripheral type %d (%s): %s's peripheral 1%s */\n", p.name, p.type, p.section, p.tile, p.dma ? "" : "; it has no DMA");
        window_struct(b, p.c_struct, p.regs, p.n_regs, true, p.dma);
    }
    b += "\n/* the tables, 0xFFFFE000 (5.2): entry i of a table is the word at + 4 i; a table stores only its fields' bits */\n";
    b += "typedef struct {\n";
    for (const Table& t : TABLES) {
        const std::string member = t.words == 1 ? fmt("%s[%d];", t.name, t.entries) : fmt("%s[%d][%d];", t.name, t.entries, t.words);
        std::string doc = t.doc;
        for (size_t p; (p = doc.find('`')) != std::string::npos;) doc.erase(p, 1);
        b += fmt("    MESH_RW %s%*s/* %s: %s (%s) */\n", member.c_str(), int(24 - member.size()) > 1 ? int(24 - member.size()) : 1, "", h8(t.base).c_str(),
                 doc.c_str(), t.section);
    }
    b += "} TableRegs;\n\n";
    window_asserts(b, "TileRegs", TILE, N_TILE, false, true);
    window_asserts(b, "PeripheralRegs", nullptr, 0, true, true);
    for (const Peripheral& p : PERIPHERALS) window_asserts(b, p.c_struct, p.regs, p.n_regs, true, p.dma);
    b += fmt("_Static_assert(sizeof(TableRegs) == 0x%X", TABLE_END - WINDOWS_BASE);
    for (const Table& t : TABLES) b += fmt(" && __builtin_offsetof(TableRegs, %s) == 0x%04X", t.name, t.base - WINDOWS_BASE);
    b += ", \"TableRegs\");\n";

    b += "\n/* the pointers: tile->EVENTS. A whole-window copy needs an explicit *tile and would consume events */\n";
    b += fmt("#define tile           ((volatile TileRegs *)0x%08Xu)\n", TILE_WINDOW_BASE);
    b += fmt("#define peripheral(n)  ((volatile PeripheralRegs *)(0x%08Xu + 0x%Xu * (n)))   /* n = 1-3 */\n", TILE_WINDOW_BASE, WINDOW_BYTES);
    for (const Peripheral& p : PERIPHERALS)
        b += fmt("#define %-14s ((volatile %s *)0x%08Xu)   /* peripheral 1 on %s */\n", p.c_pointer, p.c_struct, peripheral_window_base(1),
                 same(p.tile, "every compute tile") ? "a compute tile" : p.tile);
    b += fmt("#define tables         ((volatile TableRegs *)0x%08Xu)\n", WINDOWS_BASE);
    b += "#endif /* __ASSEMBLER__ */\n";

    b += "\n/* ---- the windows' addresses and the registers' offsets, for assembly ---- */\n";
    def(b, "WINDOWS_BASE", h8(WINDOWS_BASE), "region 0xFF's mapped part: 16 windows of 512 bytes (5.2)");
    def(b, "TILE_WINDOW_BASE", h8(TILE_WINDOW_BASE), "window 12");
    def(b, "PERIPHERAL_WINDOW_BASE(n)", fmt("(0x%08X + 0x%X * (n))", TILE_WINDOW_BASE, WINDOW_BYTES), "windows 13-15: peripheral n = 1-3's");
    def(b, "X0REL(off)", "((off) - 0x800)", "the 12-bit immediate reaching TILE_WINDOW_BASE + off from x0: one lw or sw");
    def(b, "P_X0REL(n, off)", "(0x200 * (n) + (off) - 0x800)", "the same for peripheral n's window");
    for (const Table& t : TABLES) def(b, std::string(t.name) + "_BASE", h8(t.base), fmt("%d entries of %d word%s; a table needs a base register", t.entries, t.words, t.words == 1 ? "" : "s").c_str());
    b += "/* the tile window (5.5) */\n";
    offsets(b, "", TILE, N_TILE);
    def(b, "OFF_COUNTER(n)", fmt("(0x%03X + 4 * (n))", 4 * reg_named(TILE, N_TILE, "RECEIVE_CONFIG")->index), "COUNTER_n, n = 1-7");
    b += "/* a peripheral's window (17.1) */\n";
    offsets(b, "P_", PERIPHERAL, N_PERIPHERAL);
    for (const Peripheral& p : PERIPHERALS) {
        b += fmt("/* %s's words (%s) */\n", p.name, p.section);
        offsets(b, (std::string(p.prefix) + "_").c_str(), p.regs, p.n_regs);
    }

    b += "\n/* ---- the tile's PARAMS (7.1): the straps ---- */\n";
    fields(b, "PARAMS");
    b += "\n/* ---- the tile's STATUS (7.1): states ---- */\n";
    bits(b, "STATUS");
    b += "\n/* ---- the tile's EVENTS, EVENTS_PEEK and EVENTS_MASK (7.2): counted, with a depth, or live ---- */\n";
    bits(b, "EVENT");
    def(b, "EVENTS_MASK_RESET", "(EVENT_RELEASE | EVENT_ALL_COUNTERS_DONE)", "EVENTS_MASK's reset value (18)");
    b += "\n/* ---- the tile's ERRORS (7.4): P a program error, H a hardware-fault detector; sticky until the reset; any bit raises\n"
         " * the NMI and the tile's error line ---- */\n";
    bits(b, "ERROR");

    b += "\n/* ---- header registers (5.4): the header block [7:0], dst [14:8], the register's own fields [31:15] ---- */\n";
    fields(b, "HDR");
    def(b, "HEADER(mode, counter, size)", "(HDR_MODE(mode) | HDR_COUNTER(counter) | HDR_SIZE(size))", "the header block, as in the packet (4.1)");
    b += "/* WRITE_HDR (9), STREAM_HDR (8.3), a peripheral's DMA_HDR (17.1) */\n";
    fields(b, "WRITE_HDR");
    fields(b, "STREAM_HDR");
    fields(b, "DMA_HDR");
    b += "\n/* ---- DMA_CONFIG (8.1), GROUP's two words (8.4), RECEIVE_CONFIG (12.1), GATHER_CONFIG (13) ---- */\n";
    fields(b, "DMA_CONFIG");
    values(b, "DMA_KIND");
    fields(b, "GROUP_W0");
    fields(b, "GROUP_W1");
    fields(b, "RECEIVE_CONFIG");
    fields(b, "GATHER_CONFIG");

    b += "\n/* ---- requests (11.2, 11.5): word 0 is the requester [6:0] (software writes 0), the target [9:7], op [17:10] and the\n"
         " * operation's own fields [31:18]; words 1-3 are the operation's ---- */\n";
    fields(b, "REQUEST");
    values(b, "TARGET");
    values(b, "OP");
    def(b, "REQUEST_WORD_0(target, op)", "(REQUEST_TARGET(target) | REQUEST_OP(op))", "word 0, its own fields to be or-ed in");
    b += "/* the master tile's REPORT (word 0), the memory tile's READ and WRITE (words 0 and 3), a WRITE's reply (its word 1) */\n";
    fields(b, "REPORT");
    def(b, "REPORT_WORD_0(number)", "(REQUEST_WORD_0(TARGET_MASTER, OP_REPORT) | REPORT_NUMBER(number))", nullptr);
    fields(b, "MEMORY");
    fields(b, "MEMORY_W3");
    fields(b, "MEMORY_REPLY_W1");

    b += "\n/* ---- every peripheral (17.1, 17.2): PARAMS, the standard STATUS and EVENTS bits, the types ---- */\n";
    fields(b, "P_PARAMS");
    bits(b, "P_STATUS");
    bits(b, "P_EVENT");
    values(b, "PTYPE");
    auto parts = [&](const char* prefix) {
        for (const RegionPart& r : REGION_PARTS)
            if (same(r.prefix, prefix)) def(b, r.macro, fmt("(PERIPHERAL_BASE(1) + 0x%06X)", r.offset), r.doc);
    };
    b += "\n/* ---- Ingress/Egress (17.6) ---- */\n";
    bits(b, "IE_STATUS");
    bits(b, "IE_ERROR");
    bits(b, "IE_EVENT");
    fields(b, "UART_RX");
    fields(b, "UART_STATUS");
    b += "\n/* ---- the RW engine (17.7): its bits, RW_CONFIG, its region ---- */\n";
    bits(b, "RW_ERROR");
    fields(b, "RW_CONFIG");
    values(b, "RW_KIND");
    parts("RW");
    b += "\n/* ---- the replayer (17.8.1, 17.8.9) and the Tracer (17.9.1, 17.9.2): RUN_CONFIG and EXPANSION_TEMPLATE, their bits,\n"
         " * their regions (128-bit WRITEs, 16-byte aligned) ---- */\n";
    fields(b, "RUN_CONFIG");
    fields(b, "EXPANSION_TEMPLATE");
    bits(b, "RP_EVENT");
    bits(b, "RP_ERROR");
    parts("RP");
    bits(b, "TR_EVENT");
    bits(b, "TR_ERROR");
    parts("TR");

    b += R"TB(
#ifndef __ASSEMBLER__
/* ---- helpers ---- */
static inline uint32_t tile_id(void) { return PARAMS_TILE_ID(tile->PARAMS); }
static inline volatile uint32_t *counter(unsigned n) { return (volatile uint32_t *)tile + OFF_COUNTER(n) / 4; }   /* COUNTER_n, n = 1-7 */
static inline void set_stream(unsigned i, uint32_t src_addr, uint32_t dest_addr, uint32_t hdr)
    { tables->STREAM_SRC_ADDR[i] = src_addr; tables->STREAM_DEST_ADDR[i] = dest_addr; tables->STREAM_HDR[i] = hdr; }
static inline void set_group(unsigned g, uint32_t w0, uint32_t w1) { tables->GROUP[g][0] = w0; tables->GROUP[g][1] = w1; }

/* WFI, then a load of EVENTS: returns every event and consumes one count from each counted one EVENTS_MASK selects
 * (7.2). A wake consumes nothing, and software re-checks after every wake (17.2). mstatus.MIE stays 0 (6.2). */
static inline uint32_t wfi(void) { __asm__ volatile ("wfi" ::: "memory"); return tile->EVENTS; }
/* the barrier (7.3): ARRIVE and the canonical wait, a WFI right after it, which the release alone ends (the arrival
 * gate); then the load of EVENTS, which consumes the release if EVENTS_MASK selects it (its reset value does). Every
 * counter must be at 0 (an early arrival). Only where PARAMS.barrier is 1. */
static inline uint32_t arrive(void) {
    __asm__ volatile ("sw %0, %1(zero)\n\twfi" :: "r"(1u), "i"(X0REL(OFF_ARRIVE)) : "memory");
    return tile->EVENTS;
}
/* start the transfer DMA_CONFIG describes (8.2); clears EVENTS.dma_done */
static inline void dma_start(void) { tile->DMA_START = 1; }

/* the write unit (9): WRITE_HDR and WRITE_DEST_ADDR, then one store per 32 bits, in ascending order, the last sending */
static inline void write_to(uint32_t hdr, uint32_t dest_addr) { tile->WRITE_HDR = hdr; tile->WRITE_DEST_ADDR = dest_addr; }
static inline void write32(uint32_t w0) { tile->WRITE_SEND_32 = w0; }
static inline void write64(uint32_t w1, uint32_t w0) { tile->WRITE_W0 = w0; tile->WRITE_SEND_64 = w1; }
static inline void write128(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->WRITE_W0 = w0; tile->WRITE_W1 = w1; tile->WRITE_W2 = w2; tile->WRITE_SEND_128 = w3; }
/* the request unit (11.1): words 0-2, then REQUEST_SEND, word 3, which pushes */
static inline void request_send(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->REQUEST_OUT_W0 = w0; tile->REQUEST_OUT_W1 = w1; tile->REQUEST_OUT_W2 = w2; tile->REQUEST_SEND = w3; }
/* a report to the master tile (11.5): no reply */
static inline void report(uint32_t number, uint32_t w1, uint32_t w2, uint32_t w3) { request_send(w3, w2, w1, REPORT_WORD_0(number)); }

/* the cycle counter (the core's mcycle): its low 32 bits */
static inline uint32_t mcycle(void) { uint32_t c; __asm__ volatile ("csrr %0, mcycle" : "=r"(c)); return c; }
#endif /* __ASSEMBLER__ */
#endif /* MESH_H */
)TB";
    return b;
}

// ================================================================ REGISTERS.md
namespace rd {
inline std::string code(const std::string& s) { return "`" + s + "`"; }
inline std::string macro_cell(const Field& f) {
    if (f.macro == DOC) return "";
    const std::string m = field_macro(f);
    return f.macro == FLAG ? code(m) : f.macro == GET ? code(m + "(w)") : f.macro == SET ? code(m + "(v)") : code(m + "(v)") + ", " + code(m + "_OF(w)");
}
inline void fields(std::string& b, const char* word, const char* title) {
    b += fmt("| %s | field | meaning | `mesh.h` |\n|---|---|---|---|\n", title);
    for (const Field& f : FIELDS)
        if (same(f.word, word)) b += fmt("| %s | `%s` | %s | %s |\n", bits_of(f.lo, f.width).c_str(), f.name, f.doc, macro_cell(f).c_str());
}
inline void bits(std::string& b, const char* group, const char* what) {
    b += fmt("| bit | %s | kind | %s | `mesh.h` |\n|---|---|---|---|---|\n", what, same(what, "error") ? "raised by" : same(what, "event") ? "set by" : "meaning");
    for (const Bit& x : BITS) {
        if (!same(x.group, group)) continue;
        if (x.n == 1) b += fmt("| %d | `%s` | %s | %s | `%s` |\n", x.bit, x.name, x.kind, x.doc, bit_macro(x).c_str());
        else {   // a run: peripheral_1-peripheral_3; the counters' events are counter_1_done-counter_7_done
            const bool counters = same(x.name, "counter_done");
            const char *stem = counters ? "counter" : x.name, *tail = counters ? "_done" : "";
            b += fmt("| %d-%d | `%s_1%s`-`%s_%d%s` | %s | %s | `%s(n)` |\n", x.bit, x.bit + x.n - 1, stem, tail, stem, x.n, tail, x.kind, x.doc,
                     bit_macro(x).c_str());
        }
    }
}
inline void regs(std::string& b, const Reg* list, int n, const char* off_prefix) {
    b += "| offset | block, word | register | access | reset | function | section | `mesh.h` offset |\n|---|---|---|---|---|---|---|---|\n";
    for (int k = 0; k < n; k++) {
        const Reg& r = list[k];
        b += fmt("| `0x%03X` | %d, %d | `%s` | %s | %s | %s | %s | `%sOFF_%s` |\n", 4 * r.index, r.index >> 4, r.index & 15, r.name, acc_name(r.access).c_str(),
                 r.reset, r.doc, r.section, off_prefix, r.name);
    }
}
inline void values(std::string& b, const char* group, const char* what) {
    b += fmt("| value | `mesh.h` | %s |\n|---|---|---|\n", what);
    for (const Value& v : VALUES) if (same(v.group, group)) b += fmt("| %s | `%s` | %s |\n", value_text(v).c_str(), v.macro, v.doc);
}
inline void parts(std::string& b, const char* prefix) {
    b += "| offset | contents | `mesh.h` |\n|---|---|---|\n";
    for (const RegionPart& r : REGION_PARTS) if (same(r.prefix, prefix)) b += fmt("| `0x%06X` | %s | `%s` |\n", r.offset, r.doc, r.macro);
}
}  // namespace rd

inline std::string registers_md() {
    std::string b;
    b += R"TB(# Registers: the programmer's reference

**v1.36 (`mesh.h` 1.36).** Everything a tile's program reaches with loads and stores, and the words it builds: the
address map, region 0xFF, the tile window, the tables, the peripherals' windows and regions, the packet and the
requests. `spec/machine-spec.md` is the source of truth (chapters 2, 4, 5, 7-13, 17 and 19; `spec/jolt/replayer.md`
and `spec/jolt/tracer.md` for the replayer's and the Tracer's, 17.8 and 17.9); where the two differ, this document is
wrong. Every name is the specification's, and every section number below is its. **This file is generated** by
`tools/regs/gen_regs.cpp`, from the register table in `tools/regs/regtable.h`, which also generates
`fw/include/mesh.h`; edit the table, not the file. `tests/test_regs.cpp` checks both files against the generator, the
table against the simulator's decoding (`src/units/regmap.h`), and the table against the specification's tables.

## 1. The address map

An address is an 8-bit region [31:24] and a 24-bit byte offset [23:0] (5.1). One map serves the core's own accesses
and the addresses that `WRITE` packets carry.

| region | contents | the core | a `WRITE` packet | `mesh.h` |
|---|---|---|---|---|
)TB";
    for (const Region& r : REGIONS)
        b += fmt("| %s | %s | %s | %s | %s |\n", r.lo == r.hi ? fmt("`0x%02X`", r.lo).c_str() : fmt("`0x%02X`-`0x%02X`", r.lo, r.hi).c_str(), r.contents, r.core,
                 r.packet, r.macro ? rd::code(r.macro).c_str() : "");
    b += R"TB(
**Access rules** (5.1, 5.4, 6.4):
- **Access faults** (`ERRORS.access_fault`, and a trap): a load or store to IMEM or the boot ROM; an unmapped address,
  region 0xFF's included (window 11, an absent peripheral's window, a word a block does not define, a block a window
  does not use); a sub-word access in region 0xFF; a store to a read-only register.
- **A misaligned load or store** traps before it reaches the map, and sets no bit in `ERRORS` (6.2).
- **A load from a write-only register reads 0.** Reserved bits, above a word's last field, ignore a store and read 0.
- **An address past the end of its memory** is undefined behaviour (1.3), and so is a `WRITE` there.
- **Timing.** A load from a window takes `WINDOW_LOAD_LATENCY` = 2 cycles, a load from a table `TABLE_LOAD_LATENCY`
  = 1: issued in cycle *t* with latency *L*, it returns the state at the end of *t* + *L* - 2 and takes its side
  effect (a consuming load of `EVENTS`, a pop) at the end of *t* + *L* - 1. A store takes effect at the end of
  *t* + *L* - 1: the end of *t* + 1 in a window, of *t* in a table.
- **Stalls.** Only a `WRITE_SEND_*` on a full holding register that is not being injected, a `REQUEST_SEND` on a full
  request queue from which nothing leaves, and Ingress/Egress's `UART_TX` and `STREAM_SEND` on a full FIFO stall.

## 2. Region 0xFF

Region 0xFF maps only its top 8 KiB, `0xFFFFE000`-`0xFFFFFFFF`: 16 windows of 512 bytes, window *w* at
`0xFFFFE000 + 0x200 w`, picked by address bits [12:9] (5.2). The tile window and the peripherals' are in the top
2 KiB, so each register is one `lw` or `sw` relative to `x0` (`X0REL(off)`, `P_X0REL(n, off)`); a table needs a base
register.

| window | address | contents | in C |
|---|---|---|---|
)TB";
    for (const Table& t : TABLES) {
        const int w = int((t.base - WINDOWS_BASE) / WINDOW_BYTES), nw = int(uint32_t(t.entries * t.words * 4) / WINDOW_BYTES);
        b += fmt("| %s | `%s` | `%s` (%s) | `tables->%s` |\n", nw == 1 ? fmt("%d", w).c_str() : fmt("%d, %d", w, w + nw - 1).c_str(), h8(t.base).c_str(), t.name,
                 t.section, t.name);
    }
    b += fmt("| 11 | `%s` | unmapped | |\n", h8(TABLE_END).c_str());
    b += fmt("| 12 | `%s` | the tile window (section 3) | `tile->` (`TileRegs`) |\n", h8(TILE_WINDOW_BASE).c_str());
    for (int n = 1; n <= 3; n++)
        b += fmt("| %d | `%s` | peripheral %d's window, if present (section 5) | `peripheral(%d)->` (`PeripheralRegs`) |\n", 12 + n,
                 h8(peripheral_window_base(n)).c_str(), n, n);
    b += R"TB(
A register window is eight blocks of 16 words: block *b* at + `0x40 b`, picked by address bits [8:6], and a word by
bits [5:2] (5.3). A block holds one module's registers.

| block | the tile window | a peripheral's window |
|---|---|---|
)TB";
    for (int blk = 0; blk < int(BLOCKS); blk++)
        b += fmt("| %d | %s | %s |\n", blk, TILE_BLOCK[blk] ? fmt("`%s` (%d)", TILE_BLOCK[blk], TILE_BLOCK_CHAPTER[blk]).c_str() : "unused",
                 blk == 0 ? "its control block (17.1)" : blk == 1 ? "its DMA, if it has one (17.4)" : "its own");
    b += R"TB(
Block 0, the control block, is the same in every register window: `PARAMS`, `STATUS`, `ERRORS`, `EVENTS`,
`EVENTS_PEEK`, `EVENTS_MASK`, then the start, `ARRIVE` in the tile window and `START` in a peripheral's. `STATUS` [0]
and `EVENTS` [0] are the start's state and its end, and `STATUS` [1] and `EVENTS` [1] the DMA's. A word a block does
not define, and a block a window does not use, is unmapped: an access fault. The one exception is block 1 of a
peripheral without a DMA, every word of which reads 0.

## 3. The tile window

At `0xFFFFF800`, the same on every tile (5.5). In C: `tile->NAME` (`TileRegs`); in assembly `X0REL(OFF_NAME)(zero)`.

)TB";
    rd::regs(b, TILE, N_TILE, "");
    b += R"TB(
`COUNTER_n` is at word *n* of block 5: `OFF_COUNTER(n)`, `*counter(n)`. "Undefined" at the reset means undefined
until written (18). Every other word of the window is unmapped.

### 3.1 `PARAMS`

`PARAMS` holds the straps alone (7.1). The memories' sizes are the chip's parameters, which its software is built with
(section 8).

)TB";
    rd::fields(b, "PARAMS", "bits");
    b += R"TB(
| tile | `tile_id` | `peripherals` | `receiver` | `request_target` | `attach` | `barrier` | `boot_local` |
|---|---|---|---|---|---|---|---|
| a compute tile | 0-63 | 0b001, the replayer | 0 | 0 | the special tiles attached to it: tile 0 0b001, tile 7 0b010, tile 15 0b100, the others 0 | 1 | 0 |
| the master tile | 64 | 0b001, Ingress/Egress | 1 | 0 | 0b001 | 0 | 1 |
| the memory tile | 65 | 0b001, the RW engine | 1 | 1 | 0b010 | 0 | 1 |
| the tracer tile | 66 | 0b001, the Tracer | 1 | 2 | 0b100 | 0 | 0 |

### 3.2 `STATUS`

`STATUS` holds states, and `EVENTS` what has happened (7.1).

)TB";
    rd::bits(b, "STATUS", "state");
    b += R"TB(
### 3.3 `EVENTS`, `EVENTS_PEEK` and `EVENTS_MASK`

)TB";
    rd::bits(b, "EVENT", "event");
    b += R"TB(
- **One model for every event**, the tile's and each peripheral's (7.2). A counted event of depth *d* holds a count
  from 0 to *d*: its bit reads 1 while the count is above 0, its cause adds 1, up to *d*, and a consuming load
  subtracts 1. A live event's bit follows its source, and no load changes it.
- **A load of `EVENTS`** returns every bit, and consumes one count from each counted bit that it returns set and that
  `EVENTS_MASK` selects. `EVENTS_PEEK` returns the same value and consumes nothing. An event set after the load's
  sample point survives.
- **The wake** is `(EVENTS & EVENTS_MASK) != 0`, except under the arrival gate, from `ARRIVE` until the cycle after
  the release, when it is `EVENTS.release` alone. A wake consumes nothing.
- **`EVENTS_MASK`** resets to `release` and `all_counters_done` (`EVENTS_MASK_RESET`, 0x5); a peripheral's to 0.
- **The tie rule.** A clear that starts a new wait wins over a set, in the same cycle, that ends the wait before it:
  `DMA_START`'s and `ARRIVE`'s clears.

### 3.4 `ERRORS`

One bit per error (7.4). A correct program on correct hardware never sets one. P is a program error, H a
hardware-fault detector. Each bit is sticky until the reset, except the peripherals', which are live. A non-zero
`ERRORS` raises the NMI, taken at most once, and the tile's error line.

)TB";
    rd::bits(b, "ERROR", "error");
    b += R"TB(
### 3.5 Header registers

Every register that carries a packet header (`_HDR`) shares one layout (5.4): the header block in [7:0], as in the
packet, `dst` in [14:8], and the register's own fields in [31:15]. `HEADER(mode, counter, size)` builds the header
block.

)TB";
    rd::fields(b, "HDR", "bits");
    b += "\n| register | bits | field | meaning | `mesh.h` |\n|---|---|---|---|---|\n";
    for (const char* w : {"WRITE_HDR", "STREAM_HDR", "DMA_HDR"})
        for (const Field& f : FIELDS)
            if (same(f.word, w)) b += fmt("| `%s` | %s | `%s` | %s | %s |\n", w, bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
    b += R"TB(
A field the unit fills itself is ignored on a store and reads 0: the size in `WRITE_HDR` and in a peripheral's
`DMA_HDR`, and the mode and `dst` in `READ_HDR`. `WRITE_HDR`'s `dst` is an absolute tile id. `STREAM_HDR`'s `dst`
below `TILE_COUNT` is a compute tile: on a compute tile, tile (me + `dst`) mod `TILE_COUNT`, so that 0 is the tile
itself, and on a special tile, tile `dst`; otherwise it is an absolute tile id, a special tile or broadcast (8.3).

A scratchpad address inside the units is a 21-bit word address, in bits [20:0] (`STREAM_SRC_ADDR`, a `PTR` entry,
`READ_SEND`, `GATHER_CONFIG.record_base`): word address *w* is the byte address `0x03000000 | w << 2`
(`SCRATCHPAD_ADDRESS(w)`). Every `_DEST_ADDR` is a byte address of 32 bits.

### 3.6 The units' fields

| register | bits | field | meaning | `mesh.h` |
|---|---|---|---|---|
)TB";
    for (const char* w : {"DMA_CONFIG", "READ_SEND", "READ_FREE", "RECEIVE_CONFIG", "COUNTER", "GATHER_CONFIG"})
        for (const Field& f : FIELDS)
            if (same(f.word, w)) b += fmt("| `%s` | %s | `%s` | %s | %s |\n", same(w, "COUNTER") ? "COUNTER_n" : w, bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
    b += R"TB(
- **The write unit** (9). Software stores a packet's words in ascending order, and the send, the last store, puts the
  top word above `WRITE_W0`-`W2`: `WRITE_SEND_32` alone, `WRITE_W0` then `WRITE_SEND_64`, or `WRITE_W0`-`W2` then
  `WRITE_SEND_128`. At the send a `WRITE` advances `WRITE_DEST_ADDR` by the size unless `no_increment` is set, and
  `WRITE_W0`-`W2` clear. A packet of any other mode carries address 0. The write unit checks nothing.
- **The read unit** (10). A store to `READ_SEND` queues a read of `READ_HDR`'s size at a word address; then
  `READ_DEST_ADDR` advances by the size. Each read comes back as one `WRITE` to the tile itself, at its destination
  in DMEM, counted on `READ_HDR`'s counter. With no free entry the read is dropped, `ERRORS.read_overflow` is set, and
  `READ_DEST_ADDR` still advances.
- **The counters** (12.2). A packet's counter field names one; the traffic director decrements it once the packet's
  write has landed. A store of *N* adds *N*, and *N* > 0 arms the counter: it clears its `counter_n_done` and
  `all_counters_done`, and arms both. A decrement of a counter at 0 sets `ERRORS.counter_underflow`.
- **The DMA unit** (8). `DMA_START` starts the transfer `DMA_CONFIG` describes; a start during a transfer is undefined
  behaviour. `EVENTS.dma_done` says the transfer has read its last table entry.
- **The request unit** (11.1). Words 0-2 in `REQUEST_OUT_W0`-`W2`, then `REQUEST_SEND`, word 3, which pushes the
  request and clears them. On a special tile `REQUEST_IN_W0`-`W2` read the oldest received request and
  `REQUEST_RECEIVE` pops it; a tile whose `PARAMS.receiver` is 0 reads an empty queue.

## 4. The tables

Entry *i* of a table is the word at + 4 *i*; `GROUP[g]` is two words, at + 8 *g* and + 8 *g* + 4 (5.2). A table stores
only its fields' bits: a core store ignores the others, and a core load reads them as 0. In C: `tables->HIST[i]`,
`set_stream(i, src_addr, dest_addr, hdr)`, `set_group(g, w0, w1)`.

| table | addresses | entries | fields | section | `mesh.h` |
|---|---|---|---|---|---|
)TB";
    for (const Table& t : TABLES)
        b += fmt("| `%s` | `%s`-`%s` | %d%s | %s | %s | `%s_BASE` |\n", t.name, h8(t.base).c_str(), h8(t.base + uint32_t(t.entries * t.words * 4) - 1).c_str(),
                 t.entries, t.words == 1 ? "" : " of 2 words", t.doc, t.section, t.name);
    b += "\n| word | bits | field | meaning | `mesh.h` |\n|---|---|---|---|---|\n";
    for (const char* w : {"GROUP_W0", "GROUP_W1"})
        for (const Field& f : FIELDS)
            if (same(f.word, w)) b += fmt("| `GROUP[g]` word %c | %s | `%s` | %s | %s |\n", w[7], bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
    b += R"TB(
**When the core may touch them.** `STREAM` and `GROUP` (8.6): the core may load any entry at any time, and store any
entry the running transfer does not read. `PTR` and `HIST` (12.5): only while their engine is idle; in a cycle the
engine uses a port, a core store is lost and a core load returns an undefined value. `EVENTS.counter_n_done` says the
arrivals' `PTR` and `HIST` updates are done.

## 5. The peripherals' windows

Peripheral *n* (1-3) has region 0x03 + *n* and window 12 + *n*, at `0xFFFFF800 + 0x200 n`, if present
(`PARAMS.peripherals`); any word of an absent peripheral's window is an access fault, and a `WRITE` to its region a
bad packet (17.1). Blocks 0 and 1 are the same on every peripheral; blocks 2-7 are its own. In C:
`peripheral(n)->NAME` (`PeripheralRegs`), and one struct and pointer for each peripheral below.

)TB";
    rd::regs(b, PERIPHERAL, N_PERIPHERAL, "P_");
    b += "\n";
    rd::fields(b, "P_PARAMS", "`PARAMS`");
    b += "\n";
    rd::bits(b, "P_STATUS", "state");
    b += "\n";
    rd::bits(b, "P_EVENT", "event");
    b += "\n";
    rd::values(b, "PTYPE", "`PARAMS.type`");
    b += R"TB(
- **Start** (17.2). `START` starts the peripheral's work; `STATUS.busy` is 1 until the work ends, which posts
  `EVENTS.done`. A `START` while busy is undefined behaviour.
- **Events.** A peripheral's events are consumed only by a load of its own `EVENTS`: no store clears one. The tile's
  `EVENTS.peripheral_n` is live: 1 while the peripheral's `EVENTS & EVENTS_MASK` is non-zero; it may lag a consuming
  load by a cycle, so software re-checks after every wake.
- **Errors.** The tile's `ERRORS.peripheral_n` is live: 1 while the peripheral's `ERRORS` is non-zero.
- **The DMA** (17.4), on a special tile's peripheral: `DMA_START` sends `DMA_HDR.count` 128-bit packets from the
  output queue `DMA_HDR.queue` to `DMA_HDR.dst`, each stamped with `DMA_HDR`'s counter and mode. A `WRITE` carries
  `DMA_DEST_ADDR`, 16-byte aligned, which then advances by 16. `STATUS.dma_busy` is 1 until the last packet has left,
  and then `EVENTS.dma_done` is posted. A peripheral without a DMA reads 0 in every word of block 1.
)TB";
    int sub = 1;
    for (const Peripheral& p : PERIPHERALS) {
        std::string title = p.name;
        if (title.compare(0, 4, "the ") == 0) title = "The " + title.substr(4);
        b += fmt("\n### 5.%d %s\n\n", sub++, title.c_str());
        b += fmt("Peripheral 1 of %s, type %d, version 0 (%s); %s. In C: `%s->NAME` (`%s`).\n\n", p.tile, p.type, p.section,
                 p.dma ? "it has a DMA" : "it has no DMA: its block 1 reads 0", p.c_pointer, p.c_struct);
        rd::regs(b, p.regs, p.n_regs, (std::string(p.prefix) + "_").c_str());
        b += "\nEvery other word of blocks 2-7 is unmapped.\n";
        const std::string pre = p.prefix;
        if (pre == "IE") {
            b += "\n| register | bits | field | meaning | `mesh.h` |\n|---|---|---|---|---|\n";
            for (const char* w : {"LED", "UART_TX", "UART_RX", "UART_DIVISOR", "UART_STATUS"})
                for (const Field& f : FIELDS)
                    if (same(f.word, w)) b += fmt("| `%s` | %s | `%s` | %s | %s |\n", w, bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
            b += "\n";
            rd::bits(b, "IE_STATUS", "state");
            b += "\n";
            rd::bits(b, "IE_ERROR", "error");
            b += "\n";
            rd::bits(b, "IE_EVENT", "event");
            b += R"TB(
It does no work and sends nothing: a `START` is undefined behaviour, and its region takes no write. Bit *i* of
`ERROR_LINES` is tile *i*'s error line; the master tile's own bit, 64, and the bits past 66 read 0. Bit *i* of
`ARRIVALS` is set when compute tile *i*'s arrival reaches it, and cleared by a `RELEASE`. A beat of the host interface
is `STREAM_BYTES` bytes, `STREAM_BEAT_WORDS` = ceil(`STREAM_BYTES` / 4) words: software stores the words below the
beat's top in `STREAM_OUT_W0`-`W2`, then its top word in `STREAM_SEND` or `STREAM_SEND_LAST`; `STREAM_IN_W0`-`W2` read
the words below the oldest beat's top, and `STREAM_RECEIVE` its top word, which pops it.
)TB";
        } else if (pre == "RW") {
            b += "\n| register | bits | field | meaning | `mesh.h` |\n|---|---|---|---|---|\n";
            for (const char* w : {"RW_CONFIG", "RW_LPDDR_ADDR", "RW_BUFFER_ADDR"})
                for (const Field& f : FIELDS)
                    if (same(f.word, w)) b += fmt("| `%s` | %s | `%s` | %s | %s |\n", w, bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
            b += "\n";
            rd::values(b, "RW_KIND", "`RW_CONFIG.kind`");
            b += "\n`STATUS` and `EVENTS` have the standard bits alone.\n\n";
            rd::bits(b, "RW_ERROR", "error");
            b += "\nIts region, 0x04 on the memory tile:\n\n";
            rd::parts(b, "RW");
            b += R"TB(
`START` starts the job `RW_CONFIG` describes. A read's beats enter output queue 0, which a DMA transfer of
`DMA_HDR.queue` 0, mode `WRITE` and `count` `beats` sends. A store to `RW_REPLY_SEND` pushes the reply
{0, `RW_REPLY_SEND`, `RW_REPLY_W0`} into output queue 1, which holds one entry and which a transfer of `count` 1
sends.
)TB";
        } else {
            const bool tr = pre == "TR";
            b += "\n| register | bits | field | meaning | `mesh.h` |\n|---|---|---|---|---|\n";
            for (const char* w : {"RUN_CONFIG", "ROWS_RUN", "EXPANSION_OFFSET", "HASH_STEP", "HASH_LINES", "EXPANSION_TEMPLATE"}) {
                if (!tr && (same(w, "HASH_STEP") || same(w, "HASH_LINES") || same(w, "EXPANSION_TEMPLATE"))) continue;
                for (const Field& f : FIELDS)
                    if (same(f.word, w)) b += fmt("| `%s` | %s | `%s` | %s | %s |\n", w, bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
            }
            b += "\n`STATUS` has the standard bits alone.\n\n";
            rd::bits(b, (pre + "_EVENT").c_str(), "event");
            b += "\n";
            rd::bits(b, (pre + "_ERROR").c_str(), "error");
            b += fmt("\nIts region, 0x04 on %s, takes 128-bit `WRITE`s, 16-byte aligned; a queue's part is one address in effect:\n\n",
                     tr ? "the tracer tile" : "a compute tile");
            rd::parts(b, pre.c_str());
            if (tr) {
                b += "\nThe files word, the record's word 66 (17.9.2), 128 bits:\n\n";
                rd::fields(b, "FILES", "bits");
            } else {
                b += "\nA credit is half a queue, 128 entries (`REPLAY_QUEUE_ENTRIES` / 2); the state write fills all four (17.8.8).\n";
            }
        }
    }
    b += R"TB(
## 6. The packet

Every link carries one 256-bit packet a cycle in each direction (4.1); its reserved bits, [255:182] at 8 x 8, are 0.

)TB";
    rd::fields(b, "PACKET", "bits");
    b += "\n";
    rd::values(b, "SIZE", "size");
    b += "\n";
    rd::values(b, "MODE", "what the receiver does (4.2)");
    b += "\n";
    rd::values(b, "TILE", "tile id (2.1, 4.1), at 8 x 8");
    b += R"TB(
**Bad packets.** The traffic director drops a bad packet, uncounted, and sets `ERRORS.bad_packet`: a `WRITE` to the
boot ROM, to region 0xFF, to an unmapped region or to an absent peripheral; and an `AUGMENT` at 128 bits. A packet
misaligned to its size, a `WRITE` past the end of its memory and a `GATHER` from another tile are undefined behaviour.
A `COUNT` packet writes nothing: a fence, or "done" between tiles.

## 7. Requests

A request is 128 bits, from any tile to a special tile over the request tree (11). Word 0 holds the hardware's fields
and, on the special tiles, the operation (11.2, 11.5); `REQUEST_WORD_0(target, op)` builds it.

)TB";
    rd::fields(b, "REQUEST", "word 0");
    b += "\n";
    rd::values(b, "TARGET", "target (11.2)");
    b += "\n";
    rd::values(b, "OP", "operation, report number or counter (11.5, 6.5)");
    b += "\n| request | word | bits | field | meaning | `mesh.h` |\n|---|---|---|---|---|---|\n";
    struct { const char* w; const char* req; const char* word; } rows[] = {{"REPORT", "`REPORT`", "0"}, {"MEMORY", "`READ`, `WRITE`", "0"},
        {"MEMORY_W3", "`READ`, `WRITE`", "3"}, {"MEMORY_REPLY_W1", "a `WRITE`'s reply", "1"}};
    for (const auto& r : rows)
        for (const Field& f : FIELDS)
            if (same(f.word, r.w)) b += fmt("| %s | %s | %s | `%s` | %s | %s |\n", r.req, r.word, bits_of(f.lo, f.width).c_str(), f.name, f.doc, rd::macro_cell(f).c_str());
    b += R"TB(
- **`REPORT`** (the master tile's): words 1-3 are three data words; it asks for no reply. `REPORT_WORD_0(number)`.
- **`LOAD`** (the master tile's): the boot ROM's; the tile waits in the ROM, counter 7 armed, for its next image.
- **`READ`** (the memory tile's): word 1 the first beat's LPDDR beat address, word 2 the data's byte address at the
  requester, word 3 the reply. The data comes as 128-bit `WRITE`s counted on `data_counter`, then, if `reply_counter`
  is non-zero, a 128-bit reply of 0.
- **`WRITE`** (the memory tile's): word 1 the first beat's LPDDR beat address, word 2 0, word 3 the reply,
  `reply_counter` non-zero. The 128-bit reply holds the part's address, in the memory tile's region 0x04, in [31:0],
  and in [34:32] the memory tile's counter that counts the part's packets.
- **Answers** (11.4): the requester arms the counters for the request's data and reply before sending it. A reply is
  one `WRITE` to the reply address, in the requester's DMEM, aligned to its size.
- Every other `op` is the target's firmware's.

## 8. Parameters

Chapter 19's parameters that software needs, at their defaults, the 8 x 8 mesh's.

)TB";
    rd::values(b, "PARAM", "parameter (19)");
    b += R"TB(
## 9. `mesh.h`'s helpers

- `tile_id()`: `PARAMS.tile_id`. `counter(n)`: a pointer to `COUNTER_n`.
- `wfi()`: `WFI`, then a load of `EVENTS`, which it returns.
- `arrive()`: `ARRIVE` and the canonical wait, a `WFI` right after the store, then the load of `EVENTS` that consumes
  the release (7.3).
- `dma_start()`, `set_stream()`, `set_group()`: the DMA unit and its tables.
- `write_to(hdr, dest_addr)`, `write32()`, `write64()`, `write128()`: the write unit.
- `request_send(w3, w2, w1, w0)`, `report(number, w1, w2, w3)`: the request unit.
- `mcycle()`: the core's cycle counter, its low 32 bits.
)TB";
    return b;
}

}  // namespace regs
