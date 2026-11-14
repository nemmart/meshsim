// cstrip: finds the comments of a source file and rewrites only them; everything else is copied byte for byte.
//
//   cstrip list  LANG FILE          every comment line: "line<TAB>text" (the text without its // or # or /*)
//   cstrip code  LANG FILE          the file with every comment's text removed: two files with the same output differ
//                                   in comments only
//   cstrip apply LANG RULES FILE    the file with RULES applied to its comments, on stdout; exit 1 if a rule would
//                                   join or split lines, or touch anything outside a comment
//
// LANG: c (C, C++, Java, linker scripts and the C comments of .S files: //, /* */, with string and character
// literals, raw strings R"d(...)d" and their prefixes, line continuations, and C++14 digit separators 1'000), or sh
// (shell, make and Python: # outside quotes, at a word's start; here-documents; Python's triple quotes). In sh, a
// # that might not start a comment (inside $(...), after a word character) is left as code: a tag there is reported
// by the strip's residual check rather than rewritten.
//
// RULES: one rule per line, "regex<TAB>replacement" (ECMAScript; $1.. in the replacement), applied in order to each
// line of each comment; lines starting with # and empty lines are ignored. A comment whose text the rules make blank is
// removed with the spaces before it; its line stays, so line numbers (and __LINE__) never change.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Seg {
    bool comment = false;
    std::string open, body, close;   // a comment: its opener (// /* #), text, closer (*/ or empty); code: body only
};

bool ident(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

void push_code(std::vector<Seg>& v, const std::string& s, size_t a, size_t b) {
    if (b <= a) return;
    if (v.empty() || v.back().comment) v.push_back(Seg{});
    v.back().body.append(s, a, b - a);
}

// the C family
std::vector<Seg> lex_c(const std::string& s) {
    std::vector<Seg> v;
    size_t i = 0, start = 0;
    const size_t n = s.size();
    auto skip_quoted = [&](char q) {   // i at the opening quote; ends after the closing one (or at a newline: unterminated)
        i++;
        while (i < n && s[i] != q && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n) i++; i++; }
        if (i < n && s[i] == q) i++;
    };
    while (i < n) {
        const char c = s[i];
        if (c == '/' && i + 1 < n && (s[i + 1] == '/' || s[i + 1] == '*')) {
            push_code(v, s, start, i);
            Seg g; g.comment = true;
            if (s[i + 1] == '/') {
                g.open = "//"; i += 2; const size_t b = i;
                while (i < n && !(s[i] == '\n' && !(i > b && s[i - 1] == '\\'))) i++;   // a trailing \ continues it
                g.body = s.substr(b, i - b);
            } else {
                g.open = "/*"; i += 2; const size_t b = i;
                while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
                if (i + 1 >= n) { g.body = s.substr(b); i = n; }
                else { g.body = s.substr(b, i - b); g.close = "*/"; i += 2; }
            }
            v.push_back(g);
            start = i;
            continue;
        }
        if (c == '"') { skip_quoted('"'); continue; }
        if (c == '\'') { skip_quoted('\''); continue; }
        if (c >= '0' && c <= '9' && !(i > 0 && ident(s[i - 1]))) {   // a pp-number, with ' digit separators
            while (i < n) {
                if (ident(s[i]) || s[i] == '.') i++;
                else if (s[i] == '\'' && i + 1 < n && ident(s[i + 1])) i += 2;
                else if ((s[i] == '+' || s[i] == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E' || s[i - 1] == 'p' || s[i - 1] == 'P')) i++;
                else break;
            }
            continue;
        }
        if (ident(c)) {
            const size_t b = i;
            while (i < n && ident(s[i])) i++;
            const std::string w = s.substr(b, i - b);
            if (i < n && s[i] == '"' && (w == "R" || w == "u8R" || w == "uR" || w == "UR" || w == "LR")) {   // a raw string
                const size_t p = s.find('(', i + 1);
                if (p == std::string::npos) { i = n; continue; }
                const std::string end = ")" + s.substr(i + 1, p - i - 1) + "\"";
                const size_t e = s.find(end, p + 1);
                i = e == std::string::npos ? n : e + end.size();
            }
            continue;
        }
        i++;
    }
    push_code(v, s, start, n);
    return v;
}

// shell, make, Python
std::vector<Seg> lex_sh(const std::string& s) {
    std::vector<Seg> v;
    size_t i = 0, start = 0, line_start = 0;
    const size_t n = s.size();
    std::vector<std::string> heredocs;   // pending here-document terminators, read from the next line on
    int paren = 0;                       // inside $( ... ): a # there is left as code
    while (i < n) {
        const char c = s[i];
        if (c == '\n') {
            i++; line_start = i;
            while (!heredocs.empty() && i < n) {   // the here-documents' bodies are text, not code
                const std::string term = heredocs.front();
                heredocs.erase(heredocs.begin());
                while (i < n) {
                    size_t e = s.find('\n', i); if (e == std::string::npos) e = n;
                    std::string l = s.substr(i, e - i);
                    size_t k = 0; while (k < l.size() && l[k] == '\t') k++;
                    i = e < n ? e + 1 : n;
                    if (l.substr(k) == term) break;
                }
                line_start = i;
            }
            continue;
        }
        if (c == '\\' && i + 1 < n) { i += 2; continue; }
        if (c == '\'' || c == '"') {
            if (i + 2 < n && s[i + 1] == c && s[i + 2] == c) {   // Python's triple quotes
                const std::string q(3, c);
                const size_t e = s.find(q, i + 3);
                i = e == std::string::npos ? n : e + 3;
                continue;
            }
            i++;
            while (i < n && s[i] != c) { if (s[i] == '\\' && c == '"' && i + 1 < n) i++; i++; }
            if (i < n) i++;
            continue;
        }
        if (c == '$' && i + 1 < n && s[i + 1] == '(') { paren++; i += 2; continue; }
        if (c == ')' && paren > 0) { paren--; i++; continue; }
        if (c == '<' && i + 1 < n && s[i + 1] == '<' && !(i + 2 < n && s[i + 2] == '<')) {   // <<WORD, <<-'WORD'
            size_t k = i + 2;
            if (k < n && s[k] == '-') k++;
            while (k < n && (s[k] == ' ' || s[k] == '\t')) k++;
            const bool quoted = k < n && (s[k] == '\'' || s[k] == '"');
            const char qc = quoted ? s[k] : 0;
            if (quoted) k++;
            const size_t b = k;
            while (k < n && (ident(s[k]) || s[k] == '.' || s[k] == '-')) k++;
            if (k > b) { heredocs.push_back(s.substr(b, k - b)); if (quoted && k < n && s[k] == qc) k++; }
            i = k;
            continue;
        }
        if (c == '#' && paren == 0 && (i == line_start || s[i - 1] == ' ' || s[i - 1] == '\t' || s[i - 1] == ';')) {
            push_code(v, s, start, i);
            Seg g; g.comment = true; g.open = "#";
            const size_t b = i + 1;
            i = b;
            while (i < n && s[i] != '\n') i++;
            g.body = s.substr(b, i - b);
            v.push_back(g);
            start = i;
            continue;
        }
        i++;
    }
    push_code(v, s, start, n);
    return v;
}

std::string read_all(const char* p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cstrip: cannot read %s\n", p); std::exit(2); }
    std::ostringstream o; o << f.rdbuf(); return o.str();
}


// ---- @untag: worker names and planning codes out of a comment line
// A worker's name: WORKERn, Wn (1-30) and a ticket Wn-m; never W0. W1-W3 are also a request's or a packet's words
// ("W1 the reply address", "W2-W3", "word 1 (W1)"), so alone they count only as a possessive ("W3's"), or as one
// item of several in a parenthetical, or before a colon there; elsewhere a W1-W3 is left for the residual list.
const char* TAG = R"((?:WORKER[0-9]+|W(?:[4-9]|[12][0-9]|30)(?:-[0-9]+)?|W[1-3]-[0-9]+))";
const char* TAGA = R"((?:WORKER[0-9]+|W(?:[1-9]|[12][0-9]|30)(?:-[0-9]+)?))";
bool is_tag(const std::string& t, bool any = false) { return std::regex_match(t, std::regex(std::string("^") + (any ? TAGA : TAG) + "$")); }
// a planning code: a piece, an inventory row, a unit group, a finding, a question, a test-plan case; never a decision (Dn)
bool is_code(const std::string& t) {
    static const std::regex c(R"(^(?:P[1-9][ab]?|(?:inventory )?(?:rows?|items?|row) [A-J][0-9]+[a-z]?(?:[-/,]\s?[A-J]?[0-9]+[a-z]?)*|inventory [A-J][0-9]+[a-z]?(?:[-/][A-J]?[0-9]+)*|[A-CE-J][0-9]{1,2}[a-z]?(?:/[A-CE-J]?[0-9]{1,2})?|[FQ][0-9]+|[0-9][a-z]|step [0-9]+|stage [0-9]+)$)");
    return std::regex_match(t, c);
}
std::string trim(const std::string& x) { size_t a = x.find_first_not_of(" \t"), b = x.find_last_not_of(" \t"); return a == std::string::npos ? "" : x.substr(a, b - a + 1); }
// an item of a parenthetical that is only attribution: a tag, a tag's code ("W22's F4"), a tag with its codes
bool attribution(const std::string& item, bool any = false) {
    static const std::regex poss(std::string("^") + TAGA + R"('s [A-Z][0-9]+[a-z]?$)");
    static const std::regex lead(std::string("^") + TAG + R"(\s*\(([^()]*)\)$)");
    std::smatch m;
    if (is_tag(item, any) || std::regex_match(item, poss)) return true;
    if (std::regex_match(item, m, lead)) {
        std::string in = m[1]; size_t a = 0;
        while (true) {
            const size_t e = in.find_first_of(",;", a);
            const std::string it = trim(in.substr(a, e == std::string::npos ? std::string::npos : e - a));
            if (!(is_code(it) || is_tag(it))) return false;
            if (e == std::string::npos) break;
            a = e + 1;
        }
        return true;
    }
    return false;
}
// a parenthetical's text without its attribution; "" if nothing else is left, "\x02" if it holds no tag (unchanged)
std::string strip_items(const std::string& in) {
    std::vector<std::string> items; std::vector<char> seps;
    int depth = 0; size_t a = 0;
    for (size_t i = 0; i <= in.size(); i++) {
        if (i < in.size() && in[i] == '(') depth++;
        else if (i < in.size() && in[i] == ')') depth--;
        else if (i == in.size() || (depth == 0 && (in[i] == ',' || in[i] == ';'))) {
            items.push_back(in.substr(a, i - a)); seps.push_back(i < in.size() ? in[i] : 0); a = i + 1;
        }
    }
    bool any_tag = false;
    static const std::regex colon(std::string("^") + TAGA + R"(:\s*(.*)$)");
    const bool several = items.size() > 1;
    for (std::string& it : items) {
        std::smatch m; const std::string t = trim(it);
        if (attribution(t, several)) any_tag = true;
        else if (std::regex_match(t, m, colon)) { any_tag = true; it = " " + std::string(m[1]); }
    }
    if (!any_tag) return "\x02";
    std::string out; char sep = 0;
    for (size_t k = 0; k < items.size(); k++) {
        const std::string t = trim(items[k]);
        if (t.empty() || attribution(t, several) || is_code(t)) { if (!sep) sep = seps[k]; continue; }
        if (!out.empty()) out += std::string(1, sep ? sep : ',') + " ";
        out += t; sep = seps[k];
    }
    return out;
}
std::string untag(const std::string& line) {
    std::string l = line;
    // 0. a tag's own codes in the parenthetical after it ("W4 (E3)", "W11 (G3)") go first, the tag staying for below
    {
        static const std::regex tp(std::string(TAG) + R"(\s*\(([^()]*)\))");
        std::string out; std::smatch m; auto it = l.cbegin();
        while (std::regex_search(it, l.cend(), m, tp)) {
            out.append(it, m[0].first);
            bool codes = true; const std::string in = m[1]; size_t a = 0;
            while (true) {
                const size_t e = in.find_first_of(",;", a);
                const std::string t = trim(in.substr(a, e == std::string::npos ? std::string::npos : e - a));
                if (!is_code(t)) codes = false;
                if (e == std::string::npos) break;
                a = e + 1;
            }
            const std::string whole = m[0];
            out += codes ? whole.substr(0, whole.find_first_of(" (")) : whole;
            it = m[0].second;
        }
        out.append(it, l.cend());
        l = out;
    }
    // 1. each parenthetical holding a tag: the tags and the codes go
    for (size_t i = 0; i < l.size(); i++) {
        if (l[i] != '(') continue;
        int depth = 0; size_t j = i;
        for (; j < l.size(); j++) { if (l[j] == '(') depth++; else if (l[j] == ')' && --depth == 0) break; }
        const bool closed = j < l.size();
        const std::string in = l.substr(i + 1, (closed ? j : l.size()) - i - 1);
        const std::string r = strip_items(in);
        if (r == "\x02") continue;
        if (r.empty()) {
            size_t b = i; if (b > 0 && l[b - 1] == ' ') b--;
            l.erase(b, (closed ? j + 1 : l.size()) - b); i = b == 0 ? 0 : b - 1;
        } else l = l.substr(0, i + 1) + r + (closed ? l.substr(j) : "");
    }
    // 2. a leading tag ("W20: ...", "W20 (harness-only): ...", "---- WORKER26's inputs")
    static const std::regex lead_colon(std::string(R"(^(\s*(?:---- )?))") + TAG + R"((?:, )" + TAG + R"()*:\s*)");
    static const std::regex lead_paren(std::string(R"(^(\s*(?:---- )?))") + TAG + R"(\s+(?=\())");
    l = std::regex_replace(l, lead_colon, "$1");
    l = std::regex_replace(l, lead_paren, "$1");
    // 3. a tag's possessive is the thing itself; "by W11", "from W20" go
    static const std::regex poss(std::string(R"((^|[^A-Za-z0-9_\-]))") + TAGA + R"('s\b)");
    l = std::regex_replace(l, poss, "$1the");
    static const std::regex prep(std::string(R"( (?:by|from|with|per|after|see|ask) )") + TAG + R"(\b(?!-))");
    l = std::regex_replace(l, prep, "");
    // 4. what is only a tag: a whole comment ("// W10"), a tag closing a parenthetical ("...: W4)"), a tag ending a
    // sentence (", WORKER15."), an owner column's value after two spaces
    static const std::regex only(std::string(R"(^\s*(?:)") + TAG + R"((?:, )" + TAG + R"()*)\s*$)");
    if (std::regex_match(l, only)) return "";
    static const std::regex lead_comma(std::string(R"(^(\s*))") + TAG + R"(, )");
    l = std::regex_replace(l, lead_comma, "$1");
    static const std::regex close(std::string(R"(: )") + TAG + R"(\))");
    l = std::regex_replace(l, close, ")");
    static const std::regex end(std::string(R"(, )") + TAG + R"(\.(\s*)$)");
    l = std::regex_replace(l, end, ".$1");
    static const std::regex column(std::string(R"(\s{2,})") + TAG + R"(\s*$)");
    l = std::regex_replace(l, column, "");
    return l;
}

struct Rule { std::regex re; std::string with; std::string src; bool untag = false; std::string file, old; };

std::vector<Rule> read_rules(const char* p) {
    std::vector<Rule> r;
    std::istringstream in(read_all(p));
    std::string l;
    int k = 0;
    while (std::getline(in, l)) {
        k++;
        if (l.empty() || l[0] == '#') continue;
        if (l == "@untag") { Rule u; u.untag = true; u.src = l; r.push_back(u); continue; }
        if (l[0] == '=') {   // =FILE<TAB>old<TAB>new: literal, in that file's comments only
            const size_t t1 = l.find('\t'), t2 = l.find('\t', t1 + 1);
            if (t1 == std::string::npos || t2 == std::string::npos) { std::fprintf(stderr, "cstrip: %s:%d: =FILE<TAB>old<TAB>new\n", p, k); std::exit(2); }
            Rule h; h.file = l.substr(1, t1 - 1); h.old = l.substr(t1 + 1, t2 - t1 - 1); h.with = l.substr(t2 + 1); h.src = l; r.push_back(h); continue;
        }
        const size_t t = l.find('\t');
        if (t == std::string::npos) { std::fprintf(stderr, "cstrip: %s:%d: no tab\n", p, k); std::exit(2); }
        Rule g; g.re = std::regex(l.substr(0, t), std::regex::ECMAScript); g.with = l.substr(t + 1); g.src = l.substr(0, t);
        r.push_back(g);
    }
    return r;
}

bool blank(const std::string& s) { for (char c : s) if (c != ' ' && c != '\t' && c != '\\') return false; return true; }

std::string code_of(const std::vector<Seg>& v) {   // the code, each comment reduced to its newlines
    std::string o;
    for (const Seg& g : v) {
        if (!g.comment) { o += g.body; continue; }
        o += "\x01";
        for (char c : g.body) if (c == '\n') o += '\n';
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: cstrip list|code LANG FILE; cstrip apply LANG RULES FILE\n"); return 2; }
    const std::string mode = argv[1], lang = argv[2];
    if (lang != "c" && lang != "sh") { std::fprintf(stderr, "cstrip: LANG is c or sh\n"); return 2; }
    const char* file = mode == "apply" ? (argc > 4 ? argv[4] : nullptr) : argv[3];
    if (!file) { std::fprintf(stderr, "cstrip apply: LANG RULES FILE\n"); return 2; }
    const std::string src = read_all(file);
    std::vector<Seg> v = lang == "c" ? lex_c(src) : lex_sh(src);
    {   // the lexer loses nothing
        std::string back;
        for (const Seg& g : v) back += g.open + g.body + g.close;
        if (back != src) { std::fprintf(stderr, "cstrip: %s: the lexer lost bytes\n", file); return 1; }
    }
    if (mode == "list") {
        int line = 1;
        for (const Seg& g : v) {
            if (g.comment) {
                size_t a = 0;
                for (int k = line;; k++) {
                    const size_t e = g.body.find('\n', a);
                    std::printf("%d\t%s\n", k, g.body.substr(a, e == std::string::npos ? std::string::npos : e - a).c_str());
                    if (e == std::string::npos) break;
                    a = e + 1;
                }
            }
            for (char c : g.open + g.body + g.close) if (c == '\n') line++;
        }
        return 0;
    }
    if (mode == "code") { std::fputs(code_of(v).c_str(), stdout); return 0; }
    if (mode != "apply") { std::fprintf(stderr, "cstrip: no mode %s\n", mode.c_str()); return 2; }
    const std::vector<Rule> rules = read_rules(argv[3]);
    const std::string before = code_of(v);
    for (size_t k = 0; k < v.size(); k++) {
        Seg& g = v[k];
        if (!g.comment) continue;
        std::string out;
        size_t a = 0;
        while (true) {
            const size_t e = g.body.find('\n', a);
            std::string l = g.body.substr(a, e == std::string::npos ? std::string::npos : e - a);
            for (const Rule& r : rules) {
                if (r.untag) l = untag(l);
                else if (!r.file.empty()) {
                    if (r.file != file) continue;
                    const size_t at = l.find(r.old);
                    if (at != std::string::npos) { l.replace(at, r.old.size(), r.with); std::fprintf(stderr, "USED %s\n", r.src.c_str()); }
                } else l = std::regex_replace(l, r.re, r.with);
            }
            if (l.find('\n') != std::string::npos) { std::fprintf(stderr, "cstrip: %s: a rule made a newline\n", file); return 1; }
            out += l;
            if (e == std::string::npos) break;
            out += '\n';
            a = e + 1;
        }
        const bool was_blank = blank(g.body);
        g.body = out;
        if (!was_blank && blank(g.body) && g.body.find('\n') == std::string::npos && g.body.find('\\') == std::string::npos) {
            g.open.clear(); g.close.clear(); g.body.clear();   // the comment goes; then the spaces before it
            if (k > 0 && !v[k - 1].comment) {
                std::string& p = v[k - 1].body;
                while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
            }
            g.comment = false;
        }
    }
    auto squash = [](const std::string& s) {   // trailing spaces off every line (a removed comment takes those before it)
        std::string o, line;
        for (size_t i = 0; i <= s.size(); i++) {
            if (i == s.size() || s[i] == '\n') {
                while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
                o += line; if (i < s.size()) o += '\n'; line.clear();
            } else line += s[i];
        }
        return o;
    };
    std::string out;
    for (const Seg& g : v) out += g.open + g.body + g.close;
    const std::string re = code_of(lang == "c" ? lex_c(out) : lex_sh(out));
    auto unmark = [](std::string s) { std::string o; for (char c : s) if (c != '\x01') o += c; return o; };
    if (squash(unmark(re)) != squash(unmark(before))) { std::fprintf(stderr, "cstrip: %s: the code changed\n", file); return 1; }
    std::fputs(out.c_str(), stdout);
    return 0;
}
