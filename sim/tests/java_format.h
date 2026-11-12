// java_format.h (WORKER20, P2; the owner, 2026-11-12): Java's own number formatting, for the suites whose logs print
// what String.format made in v1.3. One shared helper (W24's tests/sort_kit.h and W25's tests/stageb.h, and W23's
// tests/ctl/ctl.h, have their own: they switch to this one in P8's cleanup). From W24's, the most general: grouping and
// the sign as well as the rounding.
//
//   jfmt::fixed(d, n[, plus])   String.format("%.nf", d) ("%+.nf" with plus). Java's Formatter rounds the SHORTEST
//                               decimal digits of d (FloatingDecimal) HALF_UP; C's printf rounds d's exact binary value
//                               half-even: 0.125 is "0.13" in Java, "0.12" in C; 129.25 at one digit "129.3" / "129.2".
//   jfmt::grouped(v[, plus])    String.format("%,d", v) ("%+,d"): groups of three with ',' (v1.3's runs' locale).
// Width and padding ("%8.1f", "%-6s") are the caller's: printf("%8s", jfmt::fixed(x, 1).c_str()).
#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace jfmt {

inline std::string grouped(int64_t v, bool plus = false) {
    const bool neg = v < 0;
    const uint64_t a = neg ? uint64_t(0) - uint64_t(v) : uint64_t(v);
    const std::string d = std::to_string(a);
    std::string o;
    for (size_t i = 0; i < d.size(); i++) { if (i && (d.size() - i) % 3 == 0) o += ','; o += d[i]; }
    return (neg ? "-" : plus ? "+" : "") + o;
}

inline std::string fixed(double d, int prec, bool plus = false) {
    if (d != d) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : plus ? "+Infinity" : "Infinity";
    const bool neg = std::signbit(d);
    const double a = neg ? -d : d;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, a, std::chars_format::scientific);   // shortest: d.ddde[+-]xx
    const std::string t(buf, r.ptr);
    const size_t e = t.find('e');
    int exp = std::atoi(t.c_str() + e + 1);
    std::string digits = t.substr(0, 1) + (e > 2 ? t.substr(2, e - 2) : "");
    if (a == 0) { digits = "0"; exp = 0; }
    std::string ip, fp;                                           // the digits as an integer part and a fraction
    if (exp >= 0) {
        while (int(digits.size()) < exp + 1) digits += '0';
        ip = digits.substr(0, size_t(exp) + 1); fp = digits.substr(size_t(exp) + 1);
    } else { ip = "0"; fp = std::string(size_t(-exp - 1), '0') + digits; }
    if (int(fp.size()) > prec) {                                  // HALF_UP at prec
        const bool up = fp[size_t(prec)] >= '5';
        fp.resize(size_t(prec));
        if (up) {
            std::string s = ip + fp;
            int i = int(s.size()) - 1;
            while (i >= 0) { if (s[size_t(i)] == '9') { s[size_t(i)] = '0'; i--; } else { s[size_t(i)]++; break; } }
            if (i < 0) s = "1" + s;
            ip = s.substr(0, s.size() - fp.size()); fp = s.substr(s.size() - fp.size());
        }
    }
    while (int(fp.size()) < prec) fp += '0';
    return std::string(neg ? "-" : plus ? "+" : "") + ip + (prec > 0 ? "." + fp : "");
}

}  // namespace jfmt
