// gen_book (WORKER23, P5; D40): v1.3's gen_book.py in C++. Fills RECIPES.tmpl.md's "@@name file@@" lines with the
// RECIPE regions of the recipes' sources (fw/recipes/), writing fw/RECIPES.md; RecipeTest's snippet check confirms
// that every block in the book equals its region (check_book.py's check, which went with regions.py).
//   gen_book [--check] [RECIPES_DIR]    (default fw/recipes, run from sim/)
//   --check: compare with the committed fw/RECIPES.md instead of writing it (exit 1 if they differ)
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "book.h"

namespace {
bool read_file(const std::string& p, std::string& s) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream b; b << f.rdbuf(); s = b.str();
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    bool check = false; std::string dir = "fw/recipes";
    for (int i = 1; i < argc; i++) { const std::string a = argv[i]; if (a == "--check") check = true; else dir = a; }
    std::string tmpl, out, err;
    if (!read_file(dir + "/RECIPES.tmpl.md", tmpl)) { std::fprintf(stderr, "gen_book: cannot read %s/RECIPES.tmpl.md\n", dir.c_str()); return 2; }
    if (!book::generate(tmpl, [&](const std::string& f, std::string& t) { return read_file(dir + "/" + f, t); }, out, err)) {
        std::fprintf(stderr, "gen_book: %s\n", err.c_str());
        return 2;
    }
    const std::string book_path = dir + "/../RECIPES.md";
    if (check) {
        std::string have;
        if (!read_file(book_path, have)) { std::fprintf(stderr, "gen_book: cannot read %s\n", book_path.c_str()); return 2; }
        if (have != out) { std::printf("gen_book --check: %s differs from the template and the sources\n", book_path.c_str()); return 1; }
        std::printf("gen_book --check: %s equals the template filled from the sources\n", book_path.c_str());
        return 0;
    }
    std::ofstream f(book_path, std::ios::binary);
    f << out;
    if (!f) { std::fprintf(stderr, "gen_book: cannot write %s\n", book_path.c_str()); return 2; }
    std::printf("gen_book: wrote %s\n", book_path.c_str());
    return 0;
}
