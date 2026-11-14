// p4_java.cpp: Java's %.Nf and %,d (p4_java.h).
#include "p4_java.h"

#include <charconv>
#include <cstdio>

namespace p4 {

std::string jfixed(double x, int prec, int width) {
    const bool neg = x < 0;
    if (neg) x = -x;
    char buf[400];
    auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed);   // the shortest digits, fixed
    std::string s(buf, res.ptr);
    const size_t dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot);
    std::string fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    while (fp.size() < size_t(prec) + 1) fp += '0';
    std::string digits = ip + fp.substr(0, size_t(prec));
    if (fp[size_t(prec)] >= '5') {                                   // HALF_UP on the shortest digits
        int k = int(digits.size()) - 1;
        while (k >= 0) {
            if (digits[size_t(k)] == '9') { digits[size_t(k)] = '0'; k--; }
            else { digits[size_t(k)]++; break; }
        }
        if (k < 0) digits.insert(digits.begin(), '1');
    }
    std::string out = prec ? digits.substr(0, digits.size() - size_t(prec)) + "." + digits.substr(digits.size() - size_t(prec)) : digits;
    bool zero = true; for (char c : out) if (c != '0' && c != '.') zero = false;
    if (neg && !zero) out = "-" + out;
    while (int(out.size()) < width) out = " " + out;
    return out;
}

std::string jgroup(int64_t v, int width) {
    std::string d = std::to_string(v < 0 ? -v : v), out;
    for (size_t i = 0; i < d.size(); i++) { if (i && (d.size() - i) % 3 == 0) out += ','; out += d[i]; }
    if (v < 0) out = "-" + out;
    while (int(out.size()) < width) out = " " + out;
    return out;
}

}  // namespace p4
