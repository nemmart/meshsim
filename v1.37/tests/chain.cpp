// chain.cpp: see chain.h.
#include "chain.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <set>

namespace chain {

namespace {

bool read_file(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
uint64_t le64(const std::vector<uint8_t>& b, size_t at) { return uint64_t(le32(b, at)) | uint64_t(le32(b, at + 4)) << 32; }
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back(uint8_t(v >> (8 * k))); }
void put64(std::vector<uint8_t>& b, uint64_t v) { put32(b, uint32_t(v)); put32(b, uint32_t(v >> 32)); }

// ---- JSON
struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at " + std::to_string(i); return false; }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("a string expected");
        i++;
        out.clear();
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\') {
                if (i >= s.size()) return fail("a bad escape");
                const char e = s[i++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("a bad \\u");
                        const unsigned v = unsigned(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                        i += 4;
                        if (v < 0x80) c = char(v); else { out += '?'; continue; }   // never in our files' keys
                        break;
                    }
                    default: c = e;
                }
            }
            out += c;
        }
        if (i >= s.size()) return fail("an unterminated string");
        i++;
        return true;
    }
    bool value(Json& v) {
        ws();
        if (i >= s.size()) return fail("a value expected");
        const char c = s[i];
        if (c == '{') {
            v.kind = Json::OBJ;
            i++; ws();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("':' expected");
                i++;
                v.keys.push_back(std::move(k));
                v.obj.emplace_back();
                if (!value(v.obj.back())) return false;
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return fail("',' or '}' expected");
            }
        }
        if (c == '[') {
            v.kind = Json::ARR;
            i++; ws();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            for (;;) {
                v.arr.emplace_back();
                if (!value(v.arr.back())) return false;
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return fail("',' or ']' expected");
            }
        }
        if (c == '"') { v.kind = Json::STR; return string(v.str); }
        if (s.compare(i, 4, "true") == 0) { v.kind = Json::BOOL; v.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { v.kind = Json::BOOL; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { v.kind = Json::NUL; i += 4; return true; }
        const size_t a = i;
        while (i < s.size() && (std::strchr("+-.eE", s[i]) || (s[i] >= '0' && s[i] <= '9'))) i++;
        if (i == a) return fail("a value expected");
        v.kind = Json::NUM;
        v.str = s.substr(a, i - a);
        v.num = std::strtod(v.str.c_str(), nullptr);
        return true;
    }
};

}  // namespace

const Json* Json::get(const std::string& k) const {
    for (size_t i = 0; i < keys.size(); i++) if (keys[i] == k) return &obj[i];
    return nullptr;
}
int64_t Json::integer() const { return int64_t(std::strtoll(str.c_str(), nullptr, 0)); }

bool parse_json(const std::string& text, Json& out, std::string& err) {
    Parser p{text, 0, {}};
    out = Json{};
    if (!p.value(out)) { err = "json: " + p.err; return false; }
    p.ws();
    if (p.i != text.size()) { err = "json: trailing text at " + std::to_string(p.i); return false; }
    return true;
}

// ---- the inputs
bool load_w26(const std::string& dir, FileStore& fs, W26& w, std::string& err) {
    std::vector<uint8_t> b;
    if (!read_file(dir + "/proof.json", b)) { err = "cannot read " + dir + "/proof.json"; return false; }
    Json pj;
    if (!parse_json(std::string(b.begin(), b.end()), pj, err)) { err = "proof.json: " + err; return false; }
    for (const char* k : {"log2_k", "lowest_address", "hash_k", "hash_p1", "hash_p3", "segments"})
        if (!pj.get(k)) { err = std::string("proof.json: no ") + k; return false; }
    w.log2k = int(pj.get("log2_k")->integer());
    w.lowest = pj.get("lowest_address")->integer();
    const int pk = int(pj.get("hash_k")->integer());
    const int32_t pp1 = int32_t(pj.get("hash_p1")->integer());
    const int pp3 = int(pj.get("hash_p3")->integer());
    w.segments = int(pj.get("segments")->integer());

    w.img = fs.create(2);
    if (!read_file(dir + "/initial.pages", b)) { err = "cannot read " + dir + "/initial.pages"; return false; }
    if (b.size() % (16 + 4096)) { err = "initial.pages: not whole entries"; return false; }
    std::vector<uint64_t> pg(512);
    w.img_pages = 0;
    for (size_t at = 0; at < b.size(); at += 16 + 4096) {
        const uint64_t off = le64(b, at);
        const uint32_t flags = le32(b, at + 8);
        if (flags != 0) { err = "initial.pages: flags " + std::to_string(flags); return false; }
        for (int k = 0; k < 512; k++) pg[k] = le64(b, at + 16 + 8 * size_t(k));
        fs.put_page(w.img, int64_t(off / 4096), pg.data());
        w.img_pages++;
    }
    if (!read_file(dir + "/frame6.bin", b) || b.size() < 20) { err = "cannot read " + dir + "/frame6.bin"; return false; }
    const uint32_t len = le32(b, 0), type = le32(b, 4), k = le32(b, 8);
    const int32_t p1 = int32_t(le32(b, 12)), p3 = int32_t(le32(b, 16));
    if (type != 6 || k > 24 || len != 5 + 32u * (1u << k)) { err = "frame6.bin: type " + std::to_string(type) + " length " + std::to_string(len); return false; }
    const size_t nl = size_t(128) << k;
    if (b.size() < 20 + nl) { err = "frame6.bin: short"; return false; }
    std::vector<uint8_t> lines(b.begin() + 20, b.begin() + 20 + long(nl)), ml;
    if (!read_file(dir + "/map.lines", ml) || ml != lines) { err = "map.lines differs from frame 6's lines"; return false; }
    if (int(k) != pk || p1 != pp1 || p3 != pp3) { err = "frame 6's constants differ from proof.json's"; return false; }
    w.hash_k = int(k); w.hash_p1 = p1; w.hash_p3 = p3;
    w.map = fs.create(FileStore::depth_for(int64_t(nl)));
    const size_t npg = std::max<size_t>(1, nl / 4096);
    for (size_t p = 0; p < npg; p++) {
        for (size_t q = 0; q < 512; q++) pg[q] = 8 * (512 * p + q) < nl ? le64(lines, 8 * (512 * p + q)) : 0;
        fs.put_page(w.map, w.map.depth == 0 ? 0 : int64_t(p), pg.data());
    }
    w.written.assign(size_t(w.segments), {});
    for (int s = 0; s < w.segments; s++) {
        char nm[32];
        std::snprintf(nm, sizeof nm, "/seg/%03d.pages", s);
        if (!read_file(dir + nm, b)) { err = "cannot read " + dir + nm; return false; }
        for (size_t at = 0; at + 8 <= b.size(); at += 8) w.written[size_t(s)].push_back(int64_t(le64(b, at) / 4096));
    }
    return true;
}

// ---- the segment file
std::vector<int64_t> Seg::written_pages() const {
    std::set<int64_t> p;
    for (const auto& u : updates) p.insert(int64_t(u.first) / 512);
    return {p.begin(), p.end()};
}

namespace {
void runs(const std::vector<uint64_t>& w, Seg& s) {
    for (size_t i = 0; i < w.size();) {
        const uint64_t cell = w[i] & 0xFFFFFFFFull;
        const int32_t n = int32_t(w[i] >> 32);
        i++;
        for (int32_t k = 0; k < n && i < w.size(); k++) s.updates.push_back({uint32_t(cell + uint64_t(k)), w[i++]});
    }
}
int bit(const std::vector<uint8_t>& b, uint64_t pos) { return (b[size_t(pos >> 3)] >> (pos & 7)) & 1; }
uint64_t bits(const std::vector<uint8_t>& b, uint64_t pos, int n) {
    uint64_t v = 0;
    for (int k = 0; k < n; k++) v |= uint64_t(bit(b, pos + uint64_t(k))) << k;
    return v;
}
}  // namespace

bool read_segments(const std::string& path, std::vector<Seg>& out, std::string& err) {
    std::vector<uint8_t> b;
    if (!read_file(path, b)) { err = "cannot read " + path; return false; }
    out.clear();
    Seg* cur = nullptr;
    size_t pos = 0;
    while (b.size() - pos >= 8) {
        const uint32_t len = le32(b, pos), type = le32(b, pos + 4);
        if (len < 2) { err = "segments.seg: frame length " + std::to_string(len); return false; }
        const size_t at = pos + 8, data = 4 * size_t(len - 2);
        if (at + data > b.size()) { err = "segments.seg: a frame past the end"; return false; }
        if (type != 1 && type != 6 && type != 7 && type != 2 && !cur) { err = "segments.seg: a frame before frame 2"; return false; }
        switch (type) {
            case 1: case 6: case 7: break;
            case 2:
                if (data < 131 * 8) { err = "segments.seg: frame 2 short"; return false; }
                out.emplace_back();
                cur = &out.back();
                for (int i = 0; i < 131; i++) cur->state[i] = le64(b, at + 8 * size_t(i));
                break;
            case 3:
                cur->advice.resize(data / 8);
                for (size_t i = 0; i < cur->advice.size(); i++) cur->advice[i] = le64(b, at + 8 * i);
                break;
            case 4: {
                std::vector<uint64_t> w(data / 8);
                for (size_t i = 0; i < w.size(); i++) w[i] = le64(b, at + 8 * i);
                runs(w, *cur);
                break;
            }
            case 5: {
                const uint32_t n = le32(b, at);
                const uint64_t end = 8 * uint64_t(at + data);
                uint64_t e[32] = {};
                std::vector<uint64_t> w(n);
                uint64_t p = 8 * uint64_t(at + 4);
                for (uint32_t k = 0; k < n; k++) {
                    if (p + 2 > end) { err = "segments.seg: frame 5 past its end"; return false; }
                    uint64_t v;
                    if (bit(b, p) == 1) {
                        if (p + 6 > end) { err = "segments.seg: frame 5 past its end"; return false; }
                        const int ix = int(bits(b, p + 1, 5));
                        p += 6;
                        v = e[ix];
                        std::memmove(e + 1, e, sizeof(uint64_t) * size_t(ix));
                        e[0] = v;
                    } else {
                        const int m = bit(b, p + 1) == 1 ? 64 : 32;
                        if (p + 2 + uint64_t(m) > end) { err = "segments.seg: frame 5 past its end"; return false; }
                        v = bits(b, p + 2, m);
                        p += 2 + uint64_t(m);
                        std::memmove(e + 1, e, sizeof(uint64_t) * 31);
                        e[0] = v;
                    }
                    w[k] = v;
                }
                runs(w, *cur);
                break;
            }
            default: err = "segments.seg: frame type " + std::to_string(type); return false;
        }
        pos = at + data;
    }
    return true;
}

std::vector<uint8_t> write_segments(const std::vector<Seg>& segs, bool lru) {
    std::vector<uint8_t> b;
    auto frame = [&](uint32_t type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> d = data;
        while (d.size() % 4) d.push_back(0);
        put32(b, uint32_t(2 + d.size() / 4));
        put32(b, type);
        b.insert(b.end(), d.begin(), d.end());
    };
    frame(1, {1, 2, 3, 4});
    for (const Seg& s : segs) {
        std::vector<uint8_t> d;
        for (int i = 0; i < 131; i++) put64(d, s.state[i]);
        frame(2, d);
        if (!s.advice.empty()) { d.clear(); for (uint64_t a : s.advice) put64(d, a); frame(3, d); }
        // the runs: a header (start cell, count) then the values, for each maximal run of consecutive cells
        std::vector<uint64_t> w;
        for (size_t i = 0; i < s.updates.size();) {
            size_t j = i + 1;
            while (j < s.updates.size() && s.updates[j].first == s.updates[j - 1].first + 1) j++;
            w.push_back(uint64_t(s.updates[i].first) | uint64_t(j - i) << 32);
            for (size_t k = i; k < j; k++) w.push_back(s.updates[k].second);
            i = j;
        }
        if (w.empty()) continue;
        d.clear();
        if (!lru) { for (uint64_t x : w) put64(d, x); frame(4, d); continue; }
        put32(d, uint32_t(w.size()));
        std::vector<uint8_t> bitsv;
        auto emit = [&](uint64_t v, int n) { for (int k = 0; k < n; k++) bitsv.push_back(uint8_t((v >> k) & 1)); };
        uint64_t e[32] = {};
        for (uint64_t v : w) {
            int ix = -1;
            for (int q = 0; q < 32; q++) if (e[q] == v) { ix = q; break; }
            if (ix >= 0) { emit(1, 1); emit(uint64_t(ix), 5); std::memmove(e + 1, e, sizeof(uint64_t) * size_t(ix)); }
            else { emit(0, 1); const bool wide = (v >> 32) != 0; emit(wide, 1); emit(v, wide ? 64 : 32); std::memmove(e + 1, e, sizeof(uint64_t) * 31); }
            e[0] = v;
        }
        for (size_t k = 0; k < bitsv.size(); k += 8) {
            uint8_t x = 0;
            for (size_t q = 0; q < 8 && k + q < bitsv.size(); q++) x |= uint8_t(bitsv[k + q] << q);
            d.push_back(x);
        }
        frame(5, d);
    }
    frame(7, {});
    return b;
}

}  // namespace chain
