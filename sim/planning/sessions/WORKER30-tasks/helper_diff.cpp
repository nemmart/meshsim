// The helper switch's differential test: the local copies of Java's %.Nf against jfmt::fixed (tests/java_format.h).
// Build from sim/: g++ -std=c++20 -O2 -Itests planning/sessions/WORKER30/helper_diff.cpp -o /tmp/helper_diff
#include <cstdio>
#include <random>
#include "java_format.h"
namespace tracer_rig {
std::string java_2f(double x) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed);
    std::string s(buf, res.ptr);
    const size_t dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot);
    std::string fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    while (fp.size() < 3) fp += '0';
    std::string digits = ip + fp.substr(0, 2);
    if (fp[2] >= '5') {  // HALF_UP: carry through the digits
        int k = int(digits.size()) - 1;
        while (k >= 0) {
            if (digits[size_t(k)] == '9') digits[size_t(k)] = '0', k--;
            else { digits[size_t(k)]++; break; }
        }
        if (k < 0) digits.insert(digits.begin(), '1');
    }
    return digits.substr(0, digits.size() - 2) + "." + digits.substr(digits.size() - 2);
}
}
namespace stageb {
std::string java_fixed(double v, int digits) {
    // Java's Formatter rounds the shortest decimal that reads back as v (FloatingDecimal's digits), HALF_UP, not the
    // binary value (12946 / 4000 = 3.2365 prints 3.237; printf gives 3.236): as W21's java_2f in tracer_rig.cpp.
    char b[512];
    auto res = std::to_chars(b, b + sizeof b, v < 0 ? -v : v, std::chars_format::fixed);
    std::string s(b, res.ptr);
    size_t dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot), fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    while (fp.size() < size_t(digits) + 1) fp += '0';
    bool up = fp[size_t(digits)] >= '5';                    // HALF_UP: the first dropped digit decides
    std::string keep = ip + fp.substr(0, size_t(digits));
    if (up) {
        int i = int(keep.size()) - 1;
        while (i >= 0 && keep[size_t(i)] == '9') keep[size_t(i--)] = '0';
        if (i < 0) keep.insert(keep.begin(), '1'); else keep[size_t(i)]++;
    }
    std::string r = keep.substr(0, keep.size() - size_t(digits));
    if (digits > 0) r += "." + keep.substr(keep.size() - size_t(digits));
    return (v < 0 && r.find_first_not_of("0.") != std::string::npos ? "-" : "") + r;
}
}
namespace sortkit {
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
}
int main() {
    // The call sites' domains: tracer_rig's pct (a count over a count, x 100: 0 to 100); stageb's cycles per row and
    // seconds (non-negative, below 1e15); any finite value; negatives.
    std::mt19937_64 g(42);
    long n_pct = 0, n_small = 0, n_any = 0, n_neg = 0, rig_pct = 0, rig_small = 0, rig_any = 0, sb_small = 0, sb_any = 0, sb_neg = 0, sk = 0;
    auto sb_differs = [](double x) { for (int p = 0; p <= 4; p++) if (jfmt::fixed(x, p) != stageb::java_fixed(x, p)) return true; return false; };
    auto sk_differs = [](double x) { for (int p = 0; p <= 4; p++) for (int s = 0; s < 2; s++) if (jfmt::fixed(x, p, s) != sortkit::fixed(x, p, s)) return true; return false; };
    for (int i = 0; i < 2000000; i++) {
        const int64_t a = int64_t(g() % 5000000), b = int64_t(g() % 5000000) + 1;
        const double pct = 100.0 * double(a % (b + 1)) / double(b), small = std::ldexp(double(g() >> 11), int(g() % 102) - 102),
                     any = std::ldexp(double(g() >> 11), int(g() % 160) - 120), neg = -std::ldexp(double(g() >> 11), int(g() % 160) - 120);
        n_pct++; rig_pct += jfmt::fixed(pct, 2) != tracer_rig::java_2f(pct); sb_small += sb_differs(pct); sk += sk_differs(pct);
        n_small++; rig_small += jfmt::fixed(small, 2) != tracer_rig::java_2f(small); sb_small += sb_differs(small); sk += sk_differs(small);
        n_any++; rig_any += jfmt::fixed(any, 2) != tracer_rig::java_2f(any); sb_any += sb_differs(any); sk += sk_differs(any);
        n_neg++; sb_neg += sb_differs(neg); sk += sk_differs(neg);
    }
    const double ties[] = {0, 0.125, 0.005, 0.015, 0.025, 1.005, 2.675, 3.2365, 99.995, 99.9949999, 0.0049999, 12946.0 / 4000, 100, 50.5};
    for (double t : ties) { rig_pct += jfmt::fixed(t, 2) != tracer_rig::java_2f(t); sb_small += sb_differs(t); sk += sk_differs(t) + sk_differs(-t); }
    sk += sk_differs(std::nan(""));
    std::printf("tracer_rig java_2f: 0..100 (pct): %ld of %ld differ; below 1e15: %ld of %ld; any non-negative: %ld of %ld\n", rig_pct, n_pct, rig_small, n_small, rig_any, n_any);
    std::printf("stageb java_fixed (digits 0-4): non-negative below 1e15: %ld of %ld differ; any non-negative: %ld of %ld; negative: %ld of %ld\n", sb_small, n_pct + n_small, sb_any, n_any, sb_neg, n_neg);
    std::printf("sort_kit fixed (digits 0-4, both signs, NaN): %ld differ (infinity: jfmt prints Java's Infinity, sort_kit's is undefined)\n", sk);
}
