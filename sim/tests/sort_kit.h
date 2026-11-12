// sort_kit.h (WORKER24, P6, rows D5-D8): what the sort's driver and the kit's tools share, as v1.3's Java does it:
//   - Java's formatting where the outputs use it: %,d (grouping), %+,d, %+.2f (Formatter's HALF_UP on the shortest
//     decimal digits), Path.of(...).toString() for a directory named in a report;
//   - the files: a directory listed in Path order (bytewise), a file's lines (Files.readAllLines), String.split(",", -1);
//   - Inputs (v1.3's test/Inputs.java): the inputs line, its parser, HASHES (readHashes), differences, abbrev.
// Configuration-time code (D6): the STL and strings are fine here; nothing of this runs in the cycle loop.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "sha256.h"

namespace sortkit {

// ---------------------------------------------------------------------------------------------- Java's formatting
// String.format("%,d", v): groups of three with ',' (the en/C locales' separator, as v1.3's runs used).
inline std::string grouped(int64_t v, bool plus = false) {
    const bool neg = v < 0;
    uint64_t a = neg ? uint64_t(0) - uint64_t(v) : uint64_t(v);
    std::string d = std::to_string(a), o;
    for (size_t i = 0; i < d.size(); i++) { if (i && (d.size() - i) % 3 == 0) o += ','; o += d[i]; }
    return (neg ? "-" : plus ? "+" : "") + o;
}
// String.format("%.Nf", d) (and "%+.Nf" with plus): Java's Formatter rounds the shortest decimal digits of d HALF_UP
// (FormattedFloatingDecimal), not d's exact binary value (C's printf): 0.125 is "0.13" in Java, "0.12" in C.
inline std::string fixed(double d, int prec, bool plus = false) {
    if (d != d) return "NaN";
    const bool neg = std::signbit(d);
    const double a = neg ? -d : d;
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, a, std::chars_format::scientific);   // shortest: d.ddde[+-]xx
    std::string t(buf, r.ptr);
    const size_t e = t.find('e');
    int exp = std::atoi(t.c_str() + e + 1);
    std::string digits = t.substr(0, 1) + (e > 2 ? t.substr(2, e - 2) : "");
    if (a == 0) { digits = "0"; exp = 0; }
    // the digits as an integer part and a fraction
    std::string ip, fp;
    if (exp >= 0) {
        while (int(digits.size()) < exp + 1) digits += '0';
        ip = digits.substr(0, size_t(exp) + 1); fp = digits.substr(size_t(exp) + 1);
    } else { ip = "0"; fp = std::string(size_t(-exp - 1), '0') + digits; }
    if (int(fp.size()) > prec) {                    // HALF_UP at prec
        const bool up = fp[size_t(prec)] >= '5';
        fp.resize(size_t(prec));
        if (up) {
            std::string s = ip + fp; int i = int(s.size()) - 1;
            while (i >= 0) { if (s[size_t(i)] == '9') { s[size_t(i)] = '0'; i--; } else { s[size_t(i)]++; break; } }
            if (i < 0) s = "1" + s;
            ip = s.substr(0, s.size() - fp.size()); fp = s.substr(s.size() - fp.size());
        }
    }
    while (int(fp.size()) < prec) fp += '0';
    return std::string(neg ? "-" : plus ? "+" : "") + ip + (prec > 0 ? "." + fp : "");
}

// Path.of(s).toString(): redundant and trailing '/' removed (UnixPath's normalisation; "." and ".." kept).
inline std::string java_path(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '/' && !o.empty() && o.back() == '/') continue; o += c; }
    while (o.size() > 1 && o.back() == '/') o.pop_back();
    return o;
}
// Path.resolve(name) for a relative name.
inline std::string resolve(const std::string& dir, const std::string& name) {
    const std::string d = java_path(dir);
    if (d.empty()) return name;
    return d == "/" ? "/" + name : d + "/" + name;
}

// ------------------------------------------------------------------------------------------------------- the files
inline bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
inline bool is_file(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
// Files.list(dir) sorted (Path order: bytewise on the names here), the names only.
inline std::vector<std::string> list_sorted(const std::string& dir) {
    std::vector<std::string> v;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n != "." && n != "..") v.push_back(n); }
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    return v;
}
inline bool ends_with(const std::string& s, const std::string& x) { return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0; }
inline bool starts_with(const std::string& s, const std::string& x) { return s.rfind(x, 0) == 0; }
// Files.readAllLines: lines split at \n, \r\n or \r, the terminators dropped, no line after a final terminator.
inline std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), cur;
    std::vector<std::string> v;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\n') { v.push_back(cur); cur.clear(); }
        else if (s[i] == '\r') { v.push_back(cur); cur.clear(); if (i + 1 < s.size() && s[i + 1] == '\n') i++; }
        else cur += s[i];
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}
inline std::string read_file_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
inline bool write_file(const std::string& path, const std::string& s) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    return std::fclose(f) == 0 && ok;
}
// String.split(",", -1): every field, the empty ones included.
inline std::vector<std::string> split_all(const std::string& s, char sep) {
    std::vector<std::string> v; std::string cur;
    for (char c : s) { if (c == sep) { v.push_back(cur); cur.clear(); } else cur += c; }
    v.push_back(cur);
    return v;
}
// String.replace(target, ""): every occurrence removed.
inline std::string remove_all(std::string s, const std::string& t) {
    for (size_t p; (p = s.find(t)) != std::string::npos; ) s.erase(p, t.size());
    return s;
}
// String.strip() (ASCII whitespace; the files here are ASCII).
inline std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}
inline std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> v; std::istringstream in(s);
    for (std::string w; in >> w; ) v.push_back(w);
    return v;
}

// ---------------------------------------------------------------------------------------------- Inputs (Inputs.java)
// An ordered name -> value map (LinkedHashMap: a repeated name keeps its first place, takes the last value).
struct Ordered {
    std::vector<std::pair<std::string, std::string>> v;
    const std::string* get(const std::string& k) const { for (auto& e : v) if (e.first == k) return &e.second; return nullptr; }
    void put(const std::string& k, const std::string& x) { for (auto& e : v) if (e.first == k) { e.second = x; return; } v.emplace_back(k, x); }
    bool contains(const std::string& k) const { return get(k) != nullptr; }
};
inline std::string sha256_file(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return test::sha256_hex(d.data(), d.size());
}
// Inputs.parse: name -> hash from an "inputs:" line (false if it isn't one)
inline bool parse_inputs(const std::string& line, Ordered& m) {
    const std::string l = strip(line);
    if (!starts_with(l, "inputs:")) return false;
    m = Ordered{};
    for (const std::string& t : split_ws(l.substr(7))) {
        const size_t i = t.rfind('=');
        if (i != std::string::npos && i > 0) m.put(t.substr(0, i), t.substr(i + 1));
    }
    return true;
}
// Inputs.readHashes: sha256sum's format ("sha  name", "sha *name"), '#' comments.
inline Ordered read_hashes(const std::string& f) {
    Ordered m;
    for (std::string l : read_lines(f)) {
        l = strip(l);
        if (l.empty() || l[0] == '#') continue;
        size_t a = 0; while (a < l.size() && !std::isspace(static_cast<unsigned char>(l[a]))) a++;
        size_t b = a; while (b < l.size() && std::isspace(static_cast<unsigned char>(l[b]))) b++;
        if (a == l.size() || b == l.size()) continue;
        std::string sha = l.substr(0, a), name = l.substr(b);
        for (char& c : sha) c = char(std::tolower(static_cast<unsigned char>(c)));
        m.put(name[0] == '*' ? name.substr(1) : name, sha);
    }
    return m;
}
inline std::string abbrev(const std::string& h) { return h.size() == 64 ? h.substr(0, 12) + "..." : h; }
// Inputs.differences: one entry per file whose hash differs from the reference (names it doesn't list not compared).
inline std::vector<std::string> differences(const Ordered& run, const Ordered& ref) {
    std::vector<std::string> d;
    for (auto& e : run.v) {
        const std::string* r = ref.get(e.first);
        if (r && *r != e.second) d.push_back(e.first + ": " + abbrev(e.second) + " (reference " + abbrev(*r) + ")");
    }
    return d;
}
inline std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; }
    return o;
}

}  // namespace sortkit
