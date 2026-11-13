// book.h (D40): the recipe book's region extractor and generator in C++, in place of v1.3's regions.py
// and gen_book.py (removed; the owner's ruling 2026-11-12). Shared by `gen_book` (fw/recipes/gen_book.cpp, which
// writes fw/RECIPES.md from RECIPES.tmpl.md) and RecipeTest's snippet check (tests/ctl/suite_recipes.cpp).
//
//   region(text, name)   regions.py's region(): the text between "/* RECIPE name */\n" and the first "\n<space>* /* END */"
//                        after it, de-indented by its least indent (blank lines don't count); found = false if absent
//   generate(tmpl, read) gen_book.py: every template line "@@name file@@" replaced by
//                        "<!-- recipe: name (file) -->\n```c|asm\n<region>\n```" (asm for a .S file); others kept
// Test-side and tooling code (the configuration regime, D6).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace book {

inline bool py_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

inline std::vector<std::string> split_lines(const std::string& s) {   // Python's s.split('\n')
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) { size_t e = s.find('\n', at); if (e == std::string::npos) { out.push_back(s.substr(at)); return out; } out.push_back(s.substr(at, e - at)); at = e + 1; }
}

// re.search(r'/\* RECIPE <name> \*/\n(.*?)\n\s*/\* END \*/', text, re.S), then the de-indent
inline std::string region(const std::string& text, const std::string& name, bool& found) {
    const std::string marker = "/* RECIPE " + name + " */\n", end = "/* END */";
    found = false;
    for (size_t m = text.find(marker); m != std::string::npos; m = text.find(marker, m + 1)) {
        const size_t start = m + marker.size();
        for (size_t p = text.find('\n', start); p != std::string::npos; p = text.find('\n', p + 1)) {
            size_t q = p + 1;
            while (q < text.size() && py_space(text[q])) q++;
            if (text.compare(q, end.size(), end) != 0) continue;
            found = true;
            const std::vector<std::string> lines = split_lines(text.substr(start, p - start));
            size_t ind = std::string::npos;
            for (const std::string& l : lines) {
                size_t k = 0; while (k < l.size() && py_space(l[k])) k++;
                if (k < l.size() && k < ind) ind = k;           // a line with something besides whitespace
            }
            if (ind == std::string::npos) ind = 0;              // (Python: min() of nothing raises; no region is all blank)
            std::string r;
            for (size_t i = 0; i < lines.size(); i++) { if (i) r += '\n'; if (lines[i].size() > ind) r += lines[i].substr(ind); }
            return r;
        }
    }
    return "";
}

// re.fullmatch(r'@@(\S+) (\S+)@@', line)
inline bool tmpl_line(const std::string& l, std::string& name, std::string& file) {
    if (l.size() < 7 || l.compare(0, 2, "@@") != 0 || l.compare(l.size() - 2, 2, "@@") != 0) return false;
    const std::string in = l.substr(2, l.size() - 4);
    size_t sp = std::string::npos, n = 0;
    for (size_t i = 0; i < in.size(); i++) if (py_space(in[i])) { if (in[i] != ' ') return false; sp = i; n++; }
    if (n != 1 || sp == 0 || sp + 1 == in.size()) return false;
    name = in.substr(0, sp); file = in.substr(sp + 1);
    return true;
}

// gen_book.py's output; `read(file)` returns the source's text (and false if it can't), `err` says what failed
inline bool generate(const std::string& tmpl, const std::function<bool(const std::string&, std::string&)>& read, std::string& out,
                     std::string& err) {
    const std::vector<std::string> lines = split_lines(tmpl);
    out.clear();
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) out += '\n';
        std::string name, file, text;
        if (!tmpl_line(lines[i], name, file)) { out += lines[i]; continue; }
        if (!read(file, text)) { err = "cannot read " + file; return false; }
        bool found = false;
        const std::string r = region(text, name, found);
        if (!found) { err = "no region " + name + " in " + file; return false; }
        const bool S = file.size() >= 2 && file.compare(file.size() - 2, 2, ".S") == 0;
        out += "<!-- recipe: " + name + " (" + file + ") -->\n```" + (S ? "asm" : "c") + "\n" + r + "\n```";
    }
    return true;
}

}  // namespace book
