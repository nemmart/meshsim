// json.h (D42): the JSON reader and writer of v1.3's replay tools, as Java behaves: objects keep their
// keys' order (LinkedHashMap: a repeated key keeps its first place and takes the last value), integers are 64-bit
// (Long.parseLong), numbers with '.', 'e' or 'E' are doubles printed as Java's Double.toString prints them, \uXXXX
// escapes become UTF-8 (a surrogate pair one code point, as Java's UTF-8 writer encodes it). Configuration-time code.
#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value;
using Ptr = std::shared_ptr<Value>;
struct Value {
    enum Kind { NUL, BOOL, LONG, DOUBLE, STRING, LIST, OBJECT } kind = NUL;
    bool b = false; int64_t l = 0; double d = 0; std::string s;
    std::vector<Ptr> list;
    std::vector<std::pair<std::string, Ptr>> obj;
    static Ptr of_long(int64_t v) { auto p = std::make_shared<Value>(); p->kind = LONG; p->l = v; return p; }
    static Ptr of_string(std::string v) { auto p = std::make_shared<Value>(); p->kind = STRING; p->s = std::move(v); return p; }
    static Ptr object() { auto p = std::make_shared<Value>(); p->kind = OBJECT; return p; }
    Ptr get(const std::string& k) const { for (auto& e : obj) if (e.first == k) return e.second; return nullptr; }
    bool has(const std::string& k) const { for (auto& e : obj) if (e.first == k) return true; return false; }
    void put(const std::string& k, Ptr v) { for (auto& e : obj) if (e.first == k) { e.second = std::move(v); return; } obj.emplace_back(k, std::move(v)); }
};

// ---- the reader (ReplayDelivery.Json); malformed input: an error string, as Java's exception would stop the tool
struct Reader {
    const std::string& s; size_t i = 0; std::string err;
    explicit Reader(const std::string& t) : s(t) {}
    static bool space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || (c >= 0x1C && c <= 0x1F); }
    void ws() { while (i < s.size() && space((unsigned char)s[i])) i++; }
    char at(size_t k) { if (k >= s.size()) { if (err.empty()) err = "unexpected end of JSON"; return 0; } return s[k]; }
    static void utf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | cp >> 6); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += char(0xE0 | cp >> 12); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
        else { o += char(0xF0 | cp >> 18); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    }
    Ptr value() {
        ws();
        auto v = std::make_shared<Value>();
        const char c = at(i);
        if (!err.empty()) return v;
        if (c == '{') {
            v->kind = Value::OBJECT; i++; ws();
            if (at(i) == '}') { i++; return v; }
            while (err.empty()) {
                ws(); Ptr k = value(); ws(); i++;
                if (!k || k->kind != Value::STRING) { err = "a JSON key that isn't a string"; return v; }
                Ptr x = value(); v->put(k->s, x); ws();
                if (at(i++) == '}') return v;
            }
            return v;
        }
        if (c == '[') {
            v->kind = Value::LIST; i++; ws();
            if (at(i) == ']') { i++; return v; }
            while (err.empty()) { v->list.push_back(value()); ws(); if (at(i++) == ']') return v; }
            return v;
        }
        if (c == '"') {
            v->kind = Value::STRING; i++;
            uint32_t hi = 0;   // a pending high surrogate
            while (err.empty()) {
                char ch = at(i++);
                if (ch == '"') break;
                if (ch == '\\') {
                    char e = at(i++);
                    if (e == 'u') {
                        uint32_t u = uint32_t(strtoul(s.substr(i, 4).c_str(), nullptr, 16)); i += 4;
                        if (u >= 0xD800 && u < 0xDC00) { if (hi) utf8(v->s, 0xFFFD); hi = u; continue; }
                        if (u >= 0xDC00 && u < 0xE000 && hi) { utf8(v->s, 0x10000 + ((hi - 0xD800) << 10) + (u - 0xDC00)); hi = 0; continue; }
                        if (hi) { utf8(v->s, '?'); hi = 0; }
                        utf8(v->s, u >= 0xD800 && u < 0xE000 ? '?' : u);   // Java's encoder writes a lone surrogate as '?'
                        continue;
                    }
                    if (hi) { utf8(v->s, '?'); hi = 0; }
                    switch (e) { case 'n': v->s += '\n'; break; case 't': v->s += '\t'; break; case 'r': v->s += '\r'; break;
                                 case 'b': v->s += '\b'; break; case 'f': v->s += '\f'; break; default: v->s += e; }
                } else { if (hi) { utf8(v->s, '?'); hi = 0; } v->s += ch; }
            }
            if (hi) utf8(v->s, '?');
            return v;
        }
        if (s.compare(i, 4, "true") == 0) { i += 4; v->kind = Value::BOOL; v->b = true; return v; }
        if (s.compare(i, 5, "false") == 0) { i += 5; v->kind = Value::BOOL; return v; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return v; }
        size_t j = i;
        while (i < s.size() && strchr("+-0123456789.eE", s[i]) && s[i]) i++;
        const std::string n = s.substr(j, i - j);
        if (n.find_first_of(".eE") != std::string::npos) { v->kind = Value::DOUBLE; v->d = strtod(n.c_str(), nullptr); return v; }
        v->kind = Value::LONG;
        const char* p = n.c_str(); if (*p == '+') p++;
        auto r = std::from_chars(p, n.c_str() + n.size(), v->l);
        if (n.empty() || r.ec != std::errc() || r.ptr != n.c_str() + n.size()) err = "bad JSON number '" + n + "'";
        return v;
    }
};
inline Ptr parse(const std::string& text, std::string& err) {
    Reader r(text); Ptr v = r.value(); err = r.err; return v;
}

// ---- Java's Double.toString
inline std::string java_double(double d) {
    if (d != d) return "NaN";
    if (d == 1.0 / 0.0) return "Infinity";
    if (d == -1.0 / 0.0) return "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific);   // shortest digits, d.ddde[+-]xx
    std::string t(buf, r.ptr);
    // Java (JDK 19 on) renders at least two digits: when the shortest is one digit, the two-digit decimal closest to d
    if (t.find('e') == (t[0] == '-' ? 2u : 1u)) {
        r = std::to_chars(buf, buf + sizeof buf, d, std::chars_format::scientific, 1);
        t.assign(buf, r.ptr);
    }
    std::string sign; if (t[0] == '-') { sign = "-"; t = t.substr(1); }
    const size_t e = t.find('e');
    const int exp = atoi(t.c_str() + e + 1);
    std::string digits = t.substr(0, 1) + (e > 2 ? t.substr(2, e - 2) : "");
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    const double a = d < 0 ? -d : d;
    if (a >= 1e-3 && a < 1e7) {
        std::string o;
        if (exp >= 0) {
            while ((int)digits.size() < exp + 1) digits += '0';
            o = digits.substr(0, exp + 1) + "." + (digits.size() > size_t(exp + 1) ? digits.substr(exp + 1) : "0");
        } else o = "0." + std::string(-exp - 1, '0') + digits;
        return sign + o;
    }
    return sign + digits.substr(0, 1) + "." + (digits.size() > 1 ? digits.substr(1) : "0") + "E" + std::to_string(exp);
}

// ---- ConvertDelivery's writer (json(o, ind), str(s))
inline std::string str(const std::string& s) {
    std::string b = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': b += "\\\""; break; case '\\': b += "\\\\"; break; case '\n': b += "\\n"; break;
            case '\t': b += "\\t"; break; case '\r': b += "\\r"; break;
            default: if (c < 0x20) { char u[8]; snprintf(u, sizeof u, "\\u%04x", c); b += u; } else b += char(c);
        }
    }
    return b + "\"";
}
inline std::string scalar(const Value& v) {
    switch (v.kind) {
        case Value::NUL: return "null";
        case Value::BOOL: return v.b ? "true" : "false";
        case Value::LONG: return std::to_string(v.l);
        case Value::DOUBLE: return java_double(v.d);
        case Value::STRING: return str(v.s);
        default: return "";
    }
}
inline bool nested(const Ptr& v) { return v && (v->kind == Value::OBJECT || v->kind == Value::LIST); }
inline std::string write(const Ptr& o, const std::string& ind) {
    if (!o) return "null";
    if (o->kind == Value::OBJECT) {
        if (o->obj.empty()) return "{}";
        bool flat = true; for (auto& e : o->obj) if (nested(e.second)) flat = false;
        std::string s = "{"; bool f = true;
        for (auto& e : o->obj) {
            s += f ? "" : ","; s += flat ? (f ? "" : " ") : "\n" + ind + "  "; f = false;
            s += str(e.first) + ": " + write(e.second, ind + "  ");
        }
        return s + (flat ? "" : "\n" + ind) + "}";
    }
    if (o->kind == Value::LIST) {
        if (o->list.empty()) return "[]";
        bool flat = true; for (auto& e : o->list) if (nested(e)) flat = false;
        std::string s = "[";
        for (size_t i = 0; i < o->list.size(); i++) {
            s += i > 0 ? "," : ""; s += flat ? (i > 0 ? " " : "") : "\n" + ind + "  "; s += write(o->list[i], ind + "  ");
        }
        return s + (flat ? "" : "\n" + ind) + "]";
    }
    return scalar(*o);
}

}  // namespace json
